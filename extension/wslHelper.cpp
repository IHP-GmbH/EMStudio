#include "wslHelper.h"

#include <QFileInfo>
#include <QDir>

#ifdef Q_OS_WIN
#include <QProcess>
#include <QStandardPaths>

namespace {

static QString runWslCmdCapture(const QString &distro,
                                const QStringList &cmd,
                                int timeoutMs)
{
    QProcess p;

    QStringList args;
    args << "-d" << distro << "--";
    args << cmd;

    p.start(QStringLiteral("wsl"), args);

    if (!p.waitForStarted(timeoutMs))
        return QString();

    if (!p.waitForFinished(timeoutMs)) {
        p.terminate();
        if (!p.waitForFinished(500)) {
            p.kill();
            p.waitForFinished(500);
        }
        return QString();
    }

    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return QString();

    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

static QString shellQuoteSingle(const QString &s)
{
    QString out = s;
    out.replace('\'', "'\\''");
    return "'" + out + "'";
}

static bool wslTest(const QString &distro, const QString &flag, const QString &linuxPath, int timeoutMs)
{
    const QString cmd = QString("test %1 %2 && echo 1 || echo 0")
    .arg(flag, shellQuoteSingle(linuxPath));
    const QString out = runWslCmdCapture(distro, QStringList() << "bash" << "-lc" << cmd, timeoutMs);
    return out.trimmed() == QLatin1String("1");
}

} // namespace
#endif // Q_OS_WIN

namespace WslHelper {

bool isReadableLocalThenWsl(const QString &path,
                                       const QString &distro,
                                       int timeoutMs)
{
    const QString p = path.trimmed();
    if (p.isEmpty())
        return false;

    // 1) Try host filesystem first
    if (QFileInfo(p).isReadable())
        return true;

#ifdef Q_OS_WIN
    // 2) Fallback to WSL only for Linux-style absolute paths
    if (p.startsWith('/')) {
        if (!isWslAvailable())
            return false;

        return isReadablePortable(p, distro, timeoutMs);
    }
#else
    Q_UNUSED(distro);
    Q_UNUSED(timeoutMs);
#endif

    return false;
}

bool isWslAvailable()
{
#ifdef Q_OS_WIN
    return !QStandardPaths::findExecutable(QStringLiteral("wsl")).isEmpty();
#else
    return false;
#endif
}

bool existsPortable(const QString &path, const QString &distro, int timeoutMs)
{
    const QString p = path.trimmed();
    if (p.isEmpty())
        return false;

#ifdef Q_OS_WIN
    if (p.startsWith('/')) {
        if (!isWslAvailable())
            return false;
        return wslTest(distro, "-e", p, timeoutMs);
    }
#endif
    return QFileInfo::exists(p);
}

bool isReadablePortable(const QString &path, const QString &distro, int timeoutMs)
{
    const QString p = path.trimmed();
    if (p.isEmpty())
        return false;

#ifdef Q_OS_WIN
    if (p.startsWith('/')) {
        if (!isWslAvailable())
            return false;
        return wslTest(distro, "-r", p, timeoutMs);
    }
#endif
    return QFileInfo(p).isReadable();
}

bool isExecutablePortable(const QString &path, const QString &distro, int timeoutMs)
{
    const QString p = path.trimmed();
    if (p.isEmpty())
        return false;

#ifdef Q_OS_WIN
    if (p.startsWith('/')) {
        if (!isWslAvailable())
            return false;
        return wslTest(distro, "-x", p, timeoutMs);
    }
#endif
    return QFileInfo(p).isExecutable();
}

QString toLinuxPathPortable(const QString &path, const QString &distro, int timeoutMs)
{
    const QString p = path.trimmed();
    if (p.isEmpty())
        return QString();

#ifdef Q_OS_WIN
    if (p.startsWith('/'))
        return p;

    if (!isWslAvailable())
        return QString();

    const QString cmd = QString("wslpath -a %1").arg(shellQuoteSingle(QDir::toNativeSeparators(p)));
    return runWslCmdCapture(distro, QStringList() << "bash" << "-lc" << cmd, timeoutMs).trimmed();
#else
    Q_UNUSED(distro);
    Q_UNUSED(timeoutMs);
    return p;
#endif
}

} // namespace WslHelper
