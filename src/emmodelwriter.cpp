/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 ************************************************************************/

#include "emmodelwriter.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

#include <algorithm>

#ifdef EMSTUDIO_HAS_ROOM
#include "database.h"
#include "em_lookalike_symbol.h"
#include "em_model_data.h"
#include "room_paths.h"
#endif

namespace {

QString fileSha1Hex(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&f))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

QString toNativeFwd(const QString &path)
{
    return QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
}

} // namespace

bool emModelWriterAvailable()
{
#ifdef EMSTUDIO_HAS_ROOM
    return true;
#else
    return false;
#endif
}

EmModelPublishResult writeEmModelRoomFile(const EmModelPublishFields &fields)
{
    EmModelPublishResult result;
#ifndef EMSTUDIO_HAS_ROOM
    Q_UNUSED(fields);
    result.error = QObject::tr(
        "This EMStudio build has no CommonDB ROOM support.\n"
        "Configure with EMSTUDIO_ROOM_SOURCE_DIR pointing at CommonDB and rebuild.");
    return result;
#else
    if (fields.outputPath.trimmed().isEmpty()) {
        result.error = QObject::tr("Output file path is empty.");
        return result;
    }
    if (fields.cellName.trimmed().isEmpty()) {
        result.error = QObject::tr("Cell name is empty.");
        return result;
    }
    if (fields.snpPath.trimmed().isEmpty() || !QFileInfo::exists(fields.snpPath)) {
        result.error = QObject::tr("Touchstone file not found:\n%1").arg(fields.snpPath);
        return result;
    }

    const QFileInfo outFi(fields.outputPath);
    QDir().mkpath(outFi.absolutePath());

    room::EmModelViewData model;
    model.defaultVariant = fields.variant.trimmed().toStdString();
    model.snpPath = toNativeFwd(fields.snpPath).toStdString();
    model.tool = fields.tool.trimmed().toStdString();
    model.emstudioPath = toNativeFwd(fields.modelPath).toStdString();
    model.z0 = fields.z0;
    model.snpHash = fileSha1Hex(fields.snpPath).toStdString();
    model.publishedAt =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();

    for (int i = 0; i < fields.ports.size(); ++i) {
        const EmModelPublishPort &src = fields.ports.at(i);
        room::EmPort p;
        p.name = src.name.trimmed().toStdString();
        if (p.name.empty())
            p.name = ("P" + QString::number(i + 1)).toStdString();
        const int idx = src.index > 0 ? src.index : (i + 1);
        p.index = static_cast<std::uint16_t>(idx);
        model.ports.push_back(p);
    }

    model.topology.layoutPath = toNativeFwd(fields.layoutPath).toStdString();
    model.topology.layoutHash = fileSha1Hex(fields.layoutPath).toStdString();
    model.topology.topCell = fields.cellName.trimmed().toStdString();

    model.setup.variant = model.defaultVariant;
    model.setup.modelPath = model.emstudioPath;
    model.setup.modelHash = fileSha1Hex(fields.modelPath).toStdString();
    model.setup.substratePath = toNativeFwd(fields.substratePath).toStdString();
    model.setup.substrateHash = fileSha1Hex(fields.substratePath).toStdString();
    model.setup.tool = model.tool;

    try {
        room::Database db;
        db.setGenerator("EMStudio");
        const std::string cell = fields.cellName.trimmed().toStdString();
        room::Cell &c = db.lib().getOrCreateCell(cell);
        room::CellContent &content = c.getOrCreateContent(room::ViewType::EmModel);
        content.setEmModel(model);
        db.saveToFile(toNativeFwd(fields.outputPath).toStdString(), room::ViewType::EmModel);
        result.emmodelPath = toNativeFwd(fields.outputPath);
    } catch (const std::exception &ex) {
        result.error = QObject::tr("Failed to write EmModel:\n%1").arg(QString::fromUtf8(ex.what()));
        return result;
    } catch (...) {
        result.error = QObject::tr("Failed to write EmModel (unknown error).");
        return result;
    }

    if (!fields.writeLookalikeSymbol)
        return result;

    room::EmLookalikeInput look;
    look.cellName = fields.cellName.trimmed().toStdString();
    const QString symbolPath = QDir(outFi.absolutePath()).filePath(
        QString::fromStdString(room::roomFileName(look.cellName, room::ViewType::Symbol)));
    look.outputPath = toNativeFwd(symbolPath).toStdString();

    for (int i = 0; i < fields.ports.size(); ++i) {
        const EmModelPublishPort &src = fields.ports.at(i);
        room::EmLookalikePort p;
        p.name = src.name.trimmed().toStdString();
        if (p.name.empty())
            p.name = ("P" + QString::number(i + 1)).toStdString();
        p.index = static_cast<std::uint16_t>(src.index > 0 ? src.index : (i + 1));
        p.xUm = src.xUm;
        p.yUm = src.yUm;
        p.hasPosition = src.hasPosition;
        look.ports.push_back(p);
    }

    // Prefer GDS flatten polys (µm, same frame as port XY). layout.room is fallback —
    // its dbuPerMicron is sometimes wrong (e.g. 1e6) and then pins explode off-scale.
    bool haveOutline = false;
    if (!fields.outlinePolysUm.isEmpty()) {
        for (const QPolygonF &poly : fields.outlinePolysUm) {
            if (poly.size() < 2)
                continue;
            std::vector<std::pair<double, double>> ring;
            ring.reserve(static_cast<std::size_t>(poly.size()));
            for (const QPointF &pt : poly) {
                ring.emplace_back(pt.x(), pt.y());
                if (!look.hasBBox) {
                    look.bboxLlxUm = look.bboxUrxUm = pt.x();
                    look.bboxLlyUm = look.bboxUryUm = pt.y();
                    look.hasBBox = true;
                } else {
                    look.bboxLlxUm = std::min(look.bboxLlxUm, pt.x());
                    look.bboxLlyUm = std::min(look.bboxLlyUm, pt.y());
                    look.bboxUrxUm = std::max(look.bboxUrxUm, pt.x());
                    look.bboxUryUm = std::max(look.bboxUryUm, pt.y());
                }
            }
            look.outlinePolysUm.push_back(std::move(ring));
        }
        haveOutline = look.hasBBox;
    }
    if (!haveOutline && !fields.layoutPath.trimmed().isEmpty()) {
        std::string layoutErr;
        haveOutline = room::fillLookalikeOutlineFromLayoutRoom(
            toNativeFwd(fields.layoutPath).toStdString(),
            look.cellName,
            look,
            &layoutErr);
    }

    const std::string symErr = room::writeLookalikeSymbolRoom(look);
    if (!symErr.empty()) {
        // EmModel is already on disk; report symbol failure without rolling back.
        result.error = QObject::tr("EmModel written, but lookalike symbol failed:\n%1")
                           .arg(QString::fromStdString(symErr));
        return result;
    }
    result.symbolPath = symbolPath;
    return result;
#endif
}
