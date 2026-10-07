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

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "emmodelwriter.h"
#include "layoutfile.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSignalBlocker>

#ifdef EMSTUDIO_HAS_ROOM
#include "room_paths.h"
#endif

namespace {

bool pathLooksLikeEmSetupModel(const QString &modelPath)
{
    const QString n = QDir::fromNativeSeparators(modelPath).toLower();
    return n.contains(QLatin1String(".emsetup/")) || n.contains(QLatin1String(".emsetup\\"));
}

QString newestTouchstoneUnder(const QString &root)
{
    if (root.isEmpty() || !QDir(root).exists())
        return {};
    static const QRegularExpression re(
        QStringLiteral(R"(^.*\.s\d+p$)"), QRegularExpression::CaseInsensitiveOption);
    QString best;
    QDateTime bestTime;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi(path);
        const QString rel = QDir::fromNativeSeparators(QDir(root).relativeFilePath(path));
        bool skip = false;
        for (const QString &part : rel.split(QLatin1Char('/'))) {
            if (part.startsWith(QLatin1Char('.'))) {
                skip = true;
                break;
            }
        }
        if (skip)
            continue;
        if (!re.match(fi.fileName()).hasMatch())
            continue;
        if (best.isEmpty() || fi.lastModified() > bestTime) {
            best = fi.absoluteFilePath();
            bestTime = fi.lastModified();
        }
    }
    return best;
}

} // namespace

bool MainWindow::outputPageContextAvailable() const
{
    if (!m_curPythonData.layoutRoomFilename.trimmed().isEmpty())
        return true;
    const QString script = currentPythonScriptPath();
    if (pathLooksLikeEmSetupModel(script))
        return true;
    if (m_ui && m_ui->txtGdsFile && isRoomLayoutPath(m_ui->txtGdsFile->text().trimmed())
        && pathLooksLikeEmSetupModel(script))
        return true;
    return false;
}

void MainWindow::updateOutputPageVisibility()
{
    if (!m_ui || !m_ui->lstRunControl)
        return;
    const bool show = outputPageContextAvailable();
    for (int i = 0; i < m_ui->lstRunControl->count(); ++i) {
        QListWidgetItem *it = m_ui->lstRunControl->item(i);
        if (it && it->text() == QLatin1String("Output"))
            it->setHidden(!show);
    }
    if (!show && m_ui->tabSettings && m_ui->tabSettings->count() > 0
        && m_ui->tabSettings->tabText(0).compare(QStringLiteral("Output"), Qt::CaseInsensitive) == 0) {
        showRunControlPage(QStringLiteral("Main"));
    }
}

QString MainWindow::resolveEmSetupCellDirectory(QString *cellNameOut, QString *variantOut) const
{
    if (cellNameOut)
        cellNameOut->clear();
    if (variantOut)
        variantOut->clear();

    const QString script = currentPythonScriptPath();
    if (!script.isEmpty()) {
        QDir variantDir = QFileInfo(script).absoluteDir();
        QDir emDir = variantDir;
        if (emDir.cdUp()) {
            QString emName = emDir.dirName();
            if (emName.endsWith(QLatin1String(".emsetup"), Qt::CaseInsensitive)) {
                if (variantOut)
                    *variantOut = variantDir.dirName();
                QString stem = emName;
                stem.chop(QStringLiteral(".emsetup").size());
                if (cellNameOut)
                    *cellNameOut = stem;
                QDir cellDir = emDir;
                if (cellDir.cdUp())
                    return cellDir.absolutePath();
            }
        }
    }

    QString layoutRoom = m_curPythonData.layoutRoomFilename.trimmed();
    if (layoutRoom.isEmpty() && m_ui && m_ui->txtGdsFile && isRoomLayoutPath(m_ui->txtGdsFile->text()))
        layoutRoom = m_ui->txtGdsFile->text().trimmed();
    if (!layoutRoom.isEmpty()) {
        if (QFileInfo(layoutRoom).isRelative() && !script.isEmpty())
            layoutRoom = QDir(QFileInfo(script).absolutePath()).filePath(layoutRoom);
        const QFileInfo fi(layoutRoom);
        if (fi.exists()) {
            QString stem = fi.fileName();
            if (stem.endsWith(QLatin1String(".layout.room"), Qt::CaseInsensitive))
                stem.chop(QStringLiteral(".layout.room").size());
            else if (stem.endsWith(QLatin1String(".room"), Qt::CaseInsensitive))
                stem.chop(QStringLiteral(".room").size());
            if (cellNameOut)
                *cellNameOut = stem;
            return fi.absolutePath();
        }
    }
    return {};
}

void MainWindow::refreshOutputPage()
{
    if (!m_ui || !m_ui->tabOutput)
        return;

    updateOutputPageVisibility();
    if (!outputPageContextAvailable())
        return;

    QString cellName;
    QString variant;
    const QString cellDir = resolveEmSetupCellDirectory(&cellName, &variant);
    const QString script = currentPythonScriptPath();

    QString layoutPath = m_ui->txtGdsFile ? m_ui->txtGdsFile->text().trimmed() : QString();
    if (!m_curPythonData.layoutRoomFilename.trimmed().isEmpty() && !script.isEmpty()) {
        const QString resolved = resolveModelInputFile(
            m_curPythonData.layoutRoomFilename, QFileInfo(script).absoluteDir());
        if (!resolved.isEmpty())
            layoutPath = resolved;
    }

    const QString substrate = m_ui->txtSubstrate ? m_ui->txtSubstrate->text().trimmed() : QString();
    const QString tool = currentSimToolKey();

    QString snp;
    if (!script.isEmpty())
        snp = newestTouchstoneUnder(QFileInfo(script).absolutePath());
    if (snp.isEmpty())
        snp = newestTouchstoneUnder(resolveResultsDirectory());

    QString outPath;
    if (!cellDir.isEmpty() && !cellName.isEmpty()) {
#ifdef EMSTUDIO_HAS_ROOM
        const QString fileName = QString::fromStdString(
            room::roomFileName(cellName.toStdString(), room::ViewType::EmModel));
        outPath = QDir(cellDir).filePath(fileName);
#else
        outPath = QDir(cellDir).filePath(cellName + QStringLiteral(".emmodel.room"));
#endif
    }

    QStringList portSummary;
    double z0FromPorts = 50.0;
    bool haveZ0 = false;
    if (m_ui->tblPorts) {
        for (int r = 0; r < m_ui->tblPorts->rowCount(); ++r) {
            const auto *numItem = m_ui->tblPorts->item(r, 0);
            const int n = numItem ? numItem->text().trimmed().toInt() : (r + 1);
            const int index = n > 0 ? n : (r + 1);
            const QString name = QStringLiteral("P%1").arg(index);
            portSummary << QStringLiteral("%1→%2").arg(name).arg(index);
            if (!haveZ0) {
                if (const auto *zItem = m_ui->tblPorts->item(r, 2)) {
                    bool ok = false;
                    const double z = zItem->text().trimmed().toDouble(&ok);
                    if (ok && z > 0.0) {
                        z0FromPorts = z;
                        haveZ0 = true;
                    }
                }
            }
        }
    }

    auto setEdit = [](QLineEdit *ed, const QString &v) {
        if (!ed)
            return;
        QSignalBlocker b(ed);
        ed->setText(v);
    };

    setEdit(m_ui->txtOutputEmModel, QDir::toNativeSeparators(outPath));
    setEdit(m_ui->txtOutputSnp, QDir::toNativeSeparators(snp));
    setEdit(m_ui->txtOutputVariant, variant);
    setEdit(m_ui->txtOutputTool, tool);
    setEdit(m_ui->txtOutputPorts, portSummary.join(QStringLiteral(", ")));
    setEdit(m_ui->txtOutputLayout, QDir::toNativeSeparators(layoutPath));
    setEdit(m_ui->txtOutputModel, QDir::toNativeSeparators(script));
    setEdit(m_ui->txtOutputSubstrate, QDir::toNativeSeparators(substrate));
    if (m_ui->spnOutputZ0) {
        if (haveZ0 || m_ui->spnOutputZ0->value() <= 0)
            m_ui->spnOutputZ0->setValue(haveZ0 ? z0FromPorts : 50.0);
    }

    QString status;
    if (!emModelWriterAvailable())
        status = tr("Create requires a build with CommonDB ROOM (EMSTUDIO_ROOM_SOURCE_DIR).");
    else if (outPath.isEmpty())
        status = tr("Could not resolve cell directory for .emmodel.room.");
    else if (snp.isEmpty())
        status = tr("No Touchstone (.sNp) found yet — run a simulation first.");
    else
        status = tr("Ready to create %1").arg(QDir::toNativeSeparators(outPath));
    if (m_ui->lblOutputStatus)
        m_ui->lblOutputStatus->setText(status);
    if (m_ui->btnOutputCreate)
        m_ui->btnOutputCreate->setEnabled(emModelWriterAvailable() && !outPath.isEmpty() && !snp.isEmpty());
}

void MainWindow::on_btnOutputRefresh_clicked()
{
    refreshOutputPage();
}

void MainWindow::on_btnOutputEmModel_clicked()
{
    if (!m_ui || !m_ui->txtOutputEmModel)
        return;
    const QString start = m_ui->txtOutputEmModel->text().trimmed();
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("EmModel output file"),
        start.isEmpty() ? QDir::homePath() : start,
        tr("EmModel ROOM (*.emmodel.room);;All files (*)"));
    if (!path.isEmpty())
        m_ui->txtOutputEmModel->setText(QDir::toNativeSeparators(path));
}

void MainWindow::on_btnOutputSnp_clicked()
{
    if (!m_ui || !m_ui->txtOutputSnp)
        return;
    const QString start = m_ui->txtOutputSnp->text().trimmed();
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Touchstone file"),
        start.isEmpty() ? QDir::homePath() : QFileInfo(start).absolutePath(),
        tr("Touchstone (*.s*p *.s2p *.s3p *.s4p);;All files (*)"));
    if (!path.isEmpty())
        m_ui->txtOutputSnp->setText(QDir::toNativeSeparators(path));
}

void MainWindow::on_btnOutputCreate_clicked()
{
    if (!m_ui)
        return;

    EmModelPublishFields f;
    f.outputPath = m_ui->txtOutputEmModel ? m_ui->txtOutputEmModel->text().trimmed() : QString();
    f.snpPath = m_ui->txtOutputSnp ? m_ui->txtOutputSnp->text().trimmed() : QString();
    f.variant = m_ui->txtOutputVariant ? m_ui->txtOutputVariant->text().trimmed() : QString();
    f.tool = m_ui->txtOutputTool ? m_ui->txtOutputTool->text().trimmed() : currentSimToolKey();
    f.modelPath = m_ui->txtOutputModel ? m_ui->txtOutputModel->text().trimmed() : currentPythonScriptPath();
    f.layoutPath = m_ui->txtOutputLayout ? m_ui->txtOutputLayout->text().trimmed() : QString();
    f.substratePath = m_ui->txtOutputSubstrate ? m_ui->txtOutputSubstrate->text().trimmed() : QString();
    f.z0 = m_ui->spnOutputZ0 ? m_ui->spnOutputZ0->value() : 50.0;
    f.writeLookalikeSymbol = true;

    QString cellName;
    resolveEmSetupCellDirectory(&cellName, nullptr);
    if (cellName.isEmpty() && !f.layoutPath.isEmpty()) {
        QString stem = QFileInfo(f.layoutPath).fileName();
        if (stem.endsWith(QLatin1String(".layout.room"), Qt::CaseInsensitive))
            stem.chop(QStringLiteral(".layout.room").size());
        cellName = stem;
    }
    if (cellName.isEmpty())
        cellName = m_ui->cbxTopCell ? m_ui->cbxTopCell->currentText().trimmed() : QString();
    f.cellName = cellName;

    // Port XY from flattened GDS markers; outline fallback when layout.room is missing.
    QHash<int, QRectF> portBoundsByGds;
    constexpr int kMaxOutlinePolys = 200;
    for (const GdsFlatPolygon &poly : qAsConst(m_flatPolys)) {
        if (poly.pointsUm.size() < 2)
            continue;
        const bool portLayer = m_ui->layoutView && m_ui->layoutView->isPortLayerNumber(poly.layer);
        if (portLayer) {
            portBoundsByGds[poly.layer] |= poly.pointsUm.boundingRect();
            continue;
        }
        if (f.outlinePolysUm.size() < kMaxOutlinePolys)
            f.outlinePolysUm.append(poly.pointsUm);
    }

    if (m_ui->tblPorts) {
        for (int r = 0; r < m_ui->tblPorts->rowCount(); ++r) {
            const auto *numItem = m_ui->tblPorts->item(r, 0);
            const int n = numItem ? numItem->text().trimmed().toInt() : (r + 1);
            EmModelPublishPort port;
            port.index = n > 0 ? n : (r + 1);
            port.name = QStringLiteral("P%1").arg(port.index);

            auto *srcBox = qobject_cast<QComboBox *>(m_ui->tblPorts->cellWidget(r, 3));
            if (srcBox) {
                const QString src = srcBox->currentText().trimmed();
                bool ok = false;
                int gds = src.toInt(&ok);
                if (!ok)
                    gds = m_subNameToGds.value(src, -1);
                if (gds >= 0 && portBoundsByGds.contains(gds)) {
                    const QRectF bb = portBoundsByGds.value(gds);
                    port.xUm = bb.center().x();
                    port.yUm = bb.center().y();
                    port.hasPosition = true;
                }
            }
            f.ports.append(port);
        }
    }

    const EmModelPublishResult pub = writeEmModelRoomFile(f);
    if (!pub.error.isEmpty()) {
        error(pub.error, false);
        if (m_ui->lblOutputStatus)
            m_ui->lblOutputStatus->setText(pub.error);
        return;
    }
    QString msg = tr("Created EmModel:\n%1").arg(QDir::toNativeSeparators(pub.emmodelPath));
    if (!pub.symbolPath.isEmpty())
        msg += tr("\nLookalike symbol:\n%1").arg(QDir::toNativeSeparators(pub.symbolPath));
    info(msg);
    if (m_ui->lblOutputStatus)
        m_ui->lblOutputStatus->setText(msg);
}
