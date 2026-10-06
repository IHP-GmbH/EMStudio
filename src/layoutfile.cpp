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

#include "layoutfile.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

LayoutFileKind layoutFileKind(const QString &path)
{
    const QString name = QFileInfo(path).fileName().toLower();
    if (name.endsWith(QLatin1String(".gds")) || name.endsWith(QLatin1String(".gdsii")))
        return LayoutFileKind::Gds;
    if (name.endsWith(QLatin1String(".layout.room")))
        return LayoutFileKind::Room;
    if (name.endsWith(QLatin1String(".room")))
        return LayoutFileKind::Room; // layout-typed *.room; sniff fails → convert may fail
    return LayoutFileKind::Unknown;
}

bool isRoomLayoutPath(const QString &path)
{
    return layoutFileKind(path) == LayoutFileKind::Room;
}

bool isGdsLayoutPath(const QString &path)
{
    return layoutFileKind(path) == LayoutFileKind::Gds;
}

QString layoutFileDialogFilter()
{
    return QObject::tr("Layout files (*.gds *.gdsii *.layout.room *.room);;"
                       "GDS (*.gds *.gdsii);;"
                       "ROOM (*.layout.room *.room);;"
                       "All files (*)");
}

static QString layoutCacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString dir = QDir(base).filePath(QStringLiteral("layout_from_room"));
    QDir().mkpath(dir);
    return dir;
}

static QString contentHashHex(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&f))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

QString materializeRoomLayoutGds(const QString &roomPath,
                                 const QString &roomToGdsExe,
                                 QString *errorMsg,
                                 const QString &outGdsPath)
{
    const QFileInfo roomFi(roomPath);
    if (!roomFi.exists() || !roomFi.isFile()) {
        if (errorMsg)
            *errorMsg = QObject::tr("ROOM layout file does not exist:\n%1").arg(roomPath);
        return {};
    }
    if (roomToGdsExe.trimmed().isEmpty() || !QFileInfo::exists(roomToGdsExe)) {
        if (errorMsg) {
            *errorMsg = QObject::tr(
                "ROOM layout requires room_to_gds (CommonDB converter).\n"
                "Set preference ROOM_TO_GDS or install room_to_gds on PATH.\n"
                "Layout file:\n%1").arg(roomPath);
        }
        return {};
    }

    QString outGds;
    if (!outGdsPath.trimmed().isEmpty()) {
        outGds = QFileInfo(outGdsPath).absoluteFilePath();
        QDir().mkpath(QFileInfo(outGds).absolutePath());
        const QFileInfo outFi(outGds);
        if (outFi.exists() && outFi.isFile() && outFi.size() > 0
            && outFi.lastModified() >= roomFi.lastModified()) {
            return outFi.absoluteFilePath();
        }
    } else {
        const QString hash = contentHashHex(roomFi.absoluteFilePath());
        if (hash.isEmpty()) {
            if (errorMsg)
                *errorMsg = QObject::tr("Cannot read ROOM layout:\n%1").arg(roomPath);
            return {};
        }
        outGds = QDir(layoutCacheDir()).filePath(hash + QLatin1String(".gds"));
        const QFileInfo outFi(outGds);
        if (outFi.exists() && outFi.isFile() && outFi.size() > 0)
            return outFi.absoluteFilePath();
    }

    QFile::remove(outGds);

    QProcess proc;
    proc.setProgram(roomToGdsExe);
    proc.setArguments({roomFi.absoluteFilePath(), outGds});
    proc.setWorkingDirectory(roomFi.absolutePath());
    proc.start();
    if (!proc.waitForFinished(120000) || proc.exitStatus() != QProcess::NormalExit
        || proc.exitCode() != 0) {
        if (errorMsg) {
            const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
            *errorMsg = QObject::tr("room_to_gds failed for:\n%1\n%2")
                            .arg(roomPath, err.isEmpty() ? proc.errorString() : err);
        }
        QFile::remove(outGds);
        return {};
    }
    if (!QFileInfo::exists(outGds)) {
        if (errorMsg)
            *errorMsg = QObject::tr("room_to_gds did not produce:\n%1").arg(outGds);
        return {};
    }
    return QFileInfo(outGds).absoluteFilePath();
}

int rewriteLayoutPathInScript(QString *script, const QString &fromPath, const QString &toPath)
{
    if (!script || fromPath.isEmpty() || toPath.isEmpty() || fromPath == toPath)
        return 0;

    int n = 0;
    const QFileInfo fromFi(fromPath);
    const QString absFwd = QDir::fromNativeSeparators(fromFi.absoluteFilePath());
    const QString absNat = QDir::toNativeSeparators(absFwd);
    const QStringList forms{fromPath.trimmed(), absFwd, absNat, fromFi.fileName()};
    for (const QString &form : forms) {
        if (form.isEmpty())
            continue;
        n += script->count(form);
        script->replace(form, toPath);
    }
    return n;
}
