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

#include "toolautodetect.h"

#include "wslHelper.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace ToolAutoDetect {
namespace {

bool isUsableFile(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QFileInfo fi(path);
    return fi.exists() && fi.isFile();
}

QString firstExistingFile(const QStringList &candidates)
{
    for (const QString &c : candidates) {
        const QString p = QDir::fromNativeSeparators(c.trimmed());
        if (isUsableFile(p))
            return p;
    }
    return {};
}

bool prefEmpty(const QMap<QString, QVariant> &prefs, const QString &key)
{
    return prefs.value(key).toString().trimmed().isEmpty();
}

void setIfEmpty(QMap<QString, QVariant> &prefs, const QString &key, const QString &value, int *count)
{
    if (value.isEmpty() || !prefEmpty(prefs, key))
        return;
    prefs.insert(key, value);
    if (count)
        ++(*count);
}

QString installRootFromBinExe(const QString &exePath)
{
    // .../bin/palace → install root
    const QFileInfo fi(exePath);
    if (!fi.exists())
        return {};
    QDir bin = fi.dir();
    if (bin.dirName().compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0) {
        bin.cdUp();
        return QDir::fromNativeSeparators(bin.absolutePath());
    }
    return QDir::fromNativeSeparators(fi.absolutePath());
}

#ifdef Q_OS_WIN
QString runWslCapture(const QString &distro, const QStringList &remoteArgs, int timeoutMs = 4000)
{
    const QString wsl = wslExePath();
    if (wsl.isEmpty())
        return {};

    QStringList args;
    if (!distro.isEmpty())
        args << QStringLiteral("-d") << distro;
    args << QStringLiteral("--") << remoteArgs;

    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(wsl, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        return {};
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};
    return QString::fromUtf8(p.readAll()).trimmed().split(QLatin1Char('\n')).value(0).trimmed();
}
#endif

} // namespace

QString findHostPython()
{
#ifdef Q_OS_WIN
    // Prefer real python.exe over the Windows Store stub when both exist.
    const QStringList names = {QStringLiteral("python"), QStringLiteral("python3")};
    for (const QString &n : names) {
        const QString hit = QStandardPaths::findExecutable(n);
        if (isUsableFile(hit) && !hit.contains(QStringLiteral("WindowsApps"), Qt::CaseInsensitive))
            return QDir::fromNativeSeparators(hit);
    }
    const QString py = QStandardPaths::findExecutable(QStringLiteral("py"));
    if (isUsableFile(py))
        return QDir::fromNativeSeparators(py);
    // Fall back to whatever findExecutable returned (including Store stub).
    for (const QString &n : names) {
        const QString hit = QStandardPaths::findExecutable(n);
        if (isUsableFile(hit))
            return QDir::fromNativeSeparators(hit);
    }
#else
    const QString py3 = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (isUsableFile(py3))
        return QDir::fromNativeSeparators(py3);
    const QString py = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (isUsableFile(py))
        return QDir::fromNativeSeparators(py);
#endif
    return {};
}

QString findKlayoutExe()
{
    QStringList candidates;

#ifdef Q_OS_WIN
    candidates << qEnvironmentVariable("KLAYOUT_EXE");
    candidates << qEnvironmentVariable("APPDATA") + QStringLiteral("/KLayout/klayout_app.exe");
    candidates << qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/KLayout/klayout_app.exe");
    candidates << qEnvironmentVariable("ProgramFiles") + QStringLiteral("/KLayout/klayout_app.exe");
    candidates << qEnvironmentVariable("ProgramFiles") + QStringLiteral("/KLayout/klayout.exe");
    candidates << QStandardPaths::findExecutable(QStringLiteral("klayout_app"));
    candidates << QStandardPaths::findExecutable(QStringLiteral("klayout"));
#else
    candidates << qEnvironmentVariable("KLAYOUT_EXE");
    candidates << QStandardPaths::findExecutable(QStringLiteral("klayout"));
    candidates << QStringLiteral("/usr/bin/klayout");
    candidates << QStringLiteral("/usr/local/bin/klayout");
#endif

    return firstExistingFile(candidates);
}

QString findElmerSolver()
{
    QStringList candidates;
#ifdef Q_OS_WIN
    candidates << QStandardPaths::findExecutable(QStringLiteral("ElmerSolver"));
    candidates << QStandardPaths::findExecutable(QStringLiteral("ElmerSolver.exe"));
    // Common Windows portable layouts
    const QStringList roots = {
        QStringLiteral("C:/ElmerFEM-gui-nompi-Windows-AMD64"),
        QStringLiteral("C:/ElmerFEM"),
        QStringLiteral("C:/Program Files/Elmer"),
    };
    for (const QString &root : roots) {
        candidates << root + QStringLiteral("/bin/ElmerSolver.exe");
        candidates << root + QStringLiteral("/bin/ElmerSolver");
    }
#else
    candidates << QStandardPaths::findExecutable(QStringLiteral("ElmerSolver"));
    candidates << QStringLiteral("/usr/bin/ElmerSolver");
    candidates << QStringLiteral("/usr/local/bin/ElmerSolver");
#endif
    return firstExistingFile(candidates);
}

QString findPalaceInstallRoot()
{
    // If `palace` is on PATH as .../bin/palace, derive install root.
#ifdef Q_OS_WIN
    const QString palaceExe = QStandardPaths::findExecutable(QStringLiteral("palace"));
#else
    const QString palaceExe = QStandardPaths::findExecutable(QStringLiteral("palace"));
#endif
    if (isUsableFile(palaceExe)) {
        const QString root = installRootFromBinExe(palaceExe);
        if (isUsableFile(QDir(root).filePath(QStringLiteral("bin/palace")))
            || isUsableFile(QDir(root).filePath(QStringLiteral("bin/palace.exe"))))
            return root;
    }

    QStringList roots;
#ifdef Q_OS_WIN
    // Native Windows installs are rare; still check a few places.
    roots << QStringLiteral("C:/palace-install");
    roots << QDir::homePath() + QStringLiteral("/palace-install");
#else
    roots << QDir::homePath() + QStringLiteral("/palace-install");
    roots << QStringLiteral("/usr/local");
    roots << QStringLiteral("/opt/palace");
    roots << QStringLiteral("/opt/palace-install");
#endif

    for (const QString &root : roots) {
        if (isUsableFile(QDir(root).filePath(QStringLiteral("bin/palace")))
            || isUsableFile(QDir(root).filePath(QStringLiteral("bin/palace.exe"))))
            return QDir::fromNativeSeparators(root);
    }

#ifdef Q_OS_WIN
    // WSL: ~/palace-install is the usual layout for EMStudio on Windows.
    const QString wslRoot = runWslCapture(
        QString(),
        {QStringLiteral("bash"),
         QStringLiteral("-lc"),
         QStringLiteral("for d in \"$HOME/palace-install\" /home/*/palace-install; do "
                        "if [ -x \"$d/bin/palace\" ]; then echo \"$d\"; break; fi; done")});
    if (!wslRoot.isEmpty() && wslRoot.startsWith(QLatin1Char('/')))
        return wslRoot;
#endif
    return {};
}

QString findOpenemsInstallRoot()
{
#ifdef Q_OS_WIN
    const QString exe = firstExistingFile({
        QStandardPaths::findExecutable(QStringLiteral("openEMS")),
        QStandardPaths::findExecutable(QStringLiteral("openEMS.exe")),
    });
#else
    const QString exe = firstExistingFile({
        QStandardPaths::findExecutable(QStringLiteral("openEMS")),
        QStringLiteral("/usr/bin/openEMS"),
        QStringLiteral("/usr/local/bin/openEMS"),
    });
#endif
    if (isUsableFile(exe))
        return QDir::fromNativeSeparators(QFileInfo(exe).absolutePath());

    QStringList roots;
#ifdef Q_OS_WIN
    roots << QStringLiteral("C:/Work/OpenEMS/openEMS");
    roots << QStringLiteral("C:/OpenEMS/openEMS");
    roots << QDir::homePath() + QStringLiteral("/OpenEMS/openEMS");
#else
    roots << QDir::homePath() + QStringLiteral("/openEMS");
    roots << QStringLiteral("/usr/local/openEMS");
#endif
    for (const QString &root : roots) {
        if (isUsableFile(QDir(root).filePath(QStringLiteral("openEMS.exe")))
            || isUsableFile(QDir(root).filePath(QStringLiteral("openEMS"))))
            return QDir::fromNativeSeparators(root);
    }
    return {};
}

#ifdef Q_OS_WIN
QString findWslPython(const QString &distro)
{
    const QString which = runWslCapture(
        distro, {QStringLiteral("bash"), QStringLiteral("-lc"), QStringLiteral("command -v python3 || command -v python")});
    if (which.startsWith(QLatin1Char('/')))
        return which;
    return {};
}
#endif

int fillEmptyPreferences(QMap<QString, QVariant> &prefs)
{
    int n = 0;

#ifdef Q_OS_WIN
    if (prefEmpty(prefs, QStringLiteral("WSL_DISTRO"))) {
        const QStringList distros = listWslDistrosFromSystem(5000);
        if (!distros.isEmpty())
            setIfEmpty(prefs, QStringLiteral("WSL_DISTRO"), distros.first(), &n);
    }
#endif

    const QString hostPy = findHostPython();
#ifdef Q_OS_WIN
    // OpenEMS / Elmer / Field Viewer are typically native Windows Pythons.
    setIfEmpty(prefs, QStringLiteral("Python Path"), hostPy, &n);
    setIfEmpty(prefs, QStringLiteral("ELMER_PYTHON"), hostPy, &n);
    setIfEmpty(prefs, QStringLiteral("FIELD_VIEWER_PYTHON"), hostPy, &n);

    const QString distro = prefs.value(QStringLiteral("WSL_DISTRO")).toString().trimmed();
    const QString wslPy = findWslPython(distro);
    setIfEmpty(prefs, QStringLiteral("PALACE_PYTHON"), wslPy.isEmpty() ? hostPy : wslPy, &n);
#else
    setIfEmpty(prefs, QStringLiteral("Python Path"), hostPy, &n);
    setIfEmpty(prefs, QStringLiteral("PALACE_PYTHON"), hostPy, &n);
    setIfEmpty(prefs, QStringLiteral("ELMER_PYTHON"), hostPy, &n);
    setIfEmpty(prefs, QStringLiteral("FIELD_VIEWER_PYTHON"), hostPy, &n);
#endif

    setIfEmpty(prefs, QStringLiteral("KLAYOUT_EXE"), findKlayoutExe(), &n);
    setIfEmpty(prefs, QStringLiteral("ELMER_SOLVER_PATH"), findElmerSolver(), &n);
    setIfEmpty(prefs, QStringLiteral("PALACE_INSTALL_PATH"), findPalaceInstallRoot(), &n);
    setIfEmpty(prefs, QStringLiteral("OPENEMS_INSTALL_PATH"), findOpenemsInstallRoot(), &n);

    return n;
}

} // namespace ToolAutoDetect
