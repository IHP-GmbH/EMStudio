#include <QDir>
#include <QProcess>
#include <QFileInfo>

#include "mainwindow.h"

#ifdef Q_OS_WIN
/*!*******************************************************************************************************************
 * \brief Executes a command inside a WSL distribution and captures its standard output.
 *
 * Runs the given command using \c wsl.exe in the specified WSL distribution and returns
 * the captured standard output as a UTF-8 string.
 *
 * The function waits synchronously for completion and returns an empty string if:
 *  - the process fails to start,
 *  - the timeout expires,
 *  - the command exits with a non-zero status.
 *
 * \param distro    Name of the WSL distribution (e.g. "Ubuntu").
 * \param cmd       Command and arguments to execute inside WSL.
 * \param timeoutMs Maximum time to wait for process completion, in milliseconds.
 *
 * \return Captured standard output on success; empty string on failure.
 **********************************************************************************************************************/
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

/*!*******************************************************************************************************************
 * \brief Quotes a string for safe use as a single-quoted literal in bash.
 *
 * Produces a string safe to embed into a \c bash -lc '...' command.
 * If the input contains single quotes, they are escaped using the standard
 * bash pattern: close quote, insert '\'' , reopen quote.
 *
 * \param s Input string to quote.
 * \return Single-quoted string safe for bash.
 **********************************************************************************************************************/
static QString shellQuoteSingle(const QString &s)
{
    QString out = s;
    out.replace('\'', "'\\''");
    return "'" + out + "'";
}

/*!*******************************************************************************************************************
 * \brief Checks whether a file system path exists inside a WSL distribution.
 *
 * Executes \c test -e <path> inside the given WSL distro and returns true
 * if the path exists (file or directory).
 *
 * \param distro    WSL distribution name (e.g. "Ubuntu").
 * \param linuxPath Absolute Linux path inside WSL (e.g. "/home/...", "/mnt/c/...").
 * \param timeoutMs Maximum time to wait for completion, in milliseconds.
 * \return True if the path exists inside WSL; false otherwise.
 **********************************************************************************************************************/
static bool wslPathExists(const QString &distro, const QString &linuxPath, int timeoutMs)
{
    const QString cmd = QString("test -e %1 && echo 1 || echo 0").arg(shellQuoteSingle(linuxPath));
    const QString out = runWslCmdCapture(distro, QStringList() << "bash" << "-lc" << cmd, timeoutMs);
    return out.trimmed() == QLatin1String("1");
}

/*!*******************************************************************************************************************
 * \brief Checks whether a file system path is executable inside a WSL distribution.
 *
 * Executes \c test -x <path> inside the given WSL distro and returns true
 * if the path exists and is executable.
 *
 * \param distro    WSL distribution name (e.g. "Ubuntu").
 * \param linuxPath Absolute Linux path inside WSL.
 * \param timeoutMs Maximum time to wait for completion, in milliseconds.
 * \return True if the path is executable inside WSL; false otherwise.
 **********************************************************************************************************************/
static bool wslPathIsExecutable(const QString &distro, const QString &linuxPath, int timeoutMs)
{
    const QString cmd = QString("test -x %1 && echo 1 || echo 0").arg(shellQuoteSingle(linuxPath));
    const QString out = runWslCmdCapture(distro, QStringList() << "bash" << "-lc" << cmd, timeoutMs);
    return out.trimmed() == QLatin1String("1");
}
#endif // Q_OS_WIN

/*!*******************************************************************************************************************
 * \brief Checks whether a path exists in a platform-portable way (Windows / Linux / WSL).
 *
 * On Linux, this function delegates to \c QFileInfo::exists().
 *
 * On Windows:
 *  - If \a path starts with '/', it is treated as a WSL absolute path and checked
 *    inside the WSL distribution via \c test -e.
 *  - Otherwise it is treated as a Windows path and checked via \c QFileInfo::exists().
 *
 * \param path      Path to check (Windows path or WSL absolute path).
 * \param distro    WSL distribution name (Windows only; ignored on Linux).
 * \param timeoutMs Timeout for WSL checks in milliseconds (Windows only; ignored on Linux).
 * \return True if the path exists in the respective environment; false otherwise.
 **********************************************************************************************************************/
bool MainWindow::pathExistsPortable(const QString &path, const QString &distro, int timeoutMs) const
{
#ifdef Q_OS_WIN
    const QString p = path.trimmed();
    if (p.isEmpty())
        return false;

    // WSL absolute path: /home/... or /mnt/c/...
    if (p.startsWith('/'))
        return wslPathExists(distro, p, timeoutMs);

    return QFileInfo::exists(p);
#else
    Q_UNUSED(distro);
    Q_UNUSED(timeoutMs);
    return QFileInfo::exists(path);
#endif
}

/*!*******************************************************************************************************************
 * \brief Checks whether a path is executable in a platform-portable way (Windows / Linux / WSL).
 *
 * On Linux, this function delegates to \c QFileInfo::isExecutable().
 *
 * On Windows:
 *  - If \a path starts with '/', it is treated as a WSL absolute path and checked
 *    inside the WSL distribution via \c test -x.
 *  - Otherwise it is treated as a Windows path and checked via \c QFileInfo::isExecutable().
 *
 * \param path      Path to check (Windows path or WSL absolute path).
 * \param distro    WSL distribution name (Windows only; ignored on Linux).
 * \param timeoutMs Timeout for WSL checks in milliseconds (Windows only; ignored on Linux).
 * \return True if the path is executable in the respective environment; false otherwise.
 **********************************************************************************************************************/
bool MainWindow::pathIsExecutablePortable(const QString &path, const QString &distro, int timeoutMs) const
{
#ifdef Q_OS_WIN
    const QString p = path.trimmed();
    if (p.isEmpty())
        return false;

    if (p.startsWith('/'))
        return wslPathIsExecutable(distro, p, timeoutMs);

    return QFileInfo(p).isExecutable();
#else
    Q_UNUSED(distro);
    Q_UNUSED(timeoutMs);
    return QFileInfo(path).isExecutable();
#endif
}

/*!*******************************************************************************************************************
 * \brief Converts a Windows path to a Linux path suitable for WSL, preserving Linux paths.
 *
 * On Linux, returns \a path unchanged.
 *
 * On Windows:
 *  - If \a path starts with '/', it is assumed to be already a Linux path (WSL) and returned as-is.
 *  - Otherwise, \c wslpath -a is used inside the selected WSL distro to convert the Windows path
 *    into an absolute Linux path.
 *
 * \param path      Input path (Windows path or WSL absolute path).
 * \param distro    WSL distribution name (Windows only; ignored on Linux).
 * \param timeoutMs Timeout for WSL conversion in milliseconds (Windows only; ignored on Linux).
 * \return Linux/WSL absolute path on Windows; original \a path on Linux; empty string on failure.
 **********************************************************************************************************************/
QString MainWindow::toLinuxPathPortable(const QString &path, const QString &distro, int timeoutMs) const
{
#ifdef Q_OS_WIN
    const QString p = path.trimmed();
    if (p.isEmpty())
        return QString();

    if (p.startsWith('/'))
        return p;

    const QString cmd =
        QString("wslpath -a %1").arg(shellQuoteSingle(QDir::toNativeSeparators(p)));

    const QString out =
        runWslCmdCapture(distro, QStringList() << "bash" << "-lc" << cmd, timeoutMs);

    return out.trimmed();
#else
    Q_UNUSED(distro);
    Q_UNUSED(timeoutMs);
    return path;
#endif
}
