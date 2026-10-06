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

#ifdef EMSTUDIO_HAS_ROOM
#include "database.h"
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

QString writeEmModelRoomFile(const EmModelPublishFields &fields)
{
#ifndef EMSTUDIO_HAS_ROOM
    Q_UNUSED(fields);
    return QObject::tr(
        "This EMStudio build has no CommonDB ROOM support.\n"
        "Configure with EMSTUDIO_ROOM_SOURCE_DIR pointing at CommonDB and rebuild.");
#else
    if (fields.outputPath.trimmed().isEmpty())
        return QObject::tr("Output file path is empty.");
    if (fields.cellName.trimmed().isEmpty())
        return QObject::tr("Cell name is empty.");
    if (fields.snpPath.trimmed().isEmpty() || !QFileInfo::exists(fields.snpPath))
        return QObject::tr("Touchstone file not found:\n%1").arg(fields.snpPath);

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

    for (int i = 0; i < fields.portNames.size(); ++i) {
        room::EmPort p;
        p.name = fields.portNames.at(i).trimmed().toStdString();
        if (p.name.empty())
            p.name = ("P" + QString::number(i + 1)).toStdString();
        p.index = static_cast<std::uint16_t>(i + 1);
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
    } catch (const std::exception &ex) {
        return QObject::tr("Failed to write EmModel:\n%1").arg(QString::fromUtf8(ex.what()));
    } catch (...) {
        return QObject::tr("Failed to write EmModel (unknown error).");
    }
    return {};
#endif
}
