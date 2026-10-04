#include <QDir>
#include <QDebug>
#include <QTimer>
#include <QProcess>
#include <QFileInfo>
#include <QStandardPaths>

#include "wslHelper.h"
#include "mainwindow.h"

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
QString shellQuoteSingle(const QString &s)
{
    QString out = s;
    out.replace('\'', "'\\''");
    return "'" + out + "'";
}

#ifdef Q_OS_WIN

/*!*******************************************************************************************************************
 * \brief Checks whether a file system path is readable inside a WSL distribution.
 *
 * Executes \c test -r <path> inside the given WSL distro and returns true
 * if the path exists and is readable.
 *
 * \param distro    WSL distribution name (e.g. "Ubuntu").
 * \param linuxPath Absolute Linux path inside WSL.
 * \param timeoutMs Maximum time to wait for completion, in milliseconds.
 * \return True if the path is readable inside WSL; false otherwise.
 **********************************************************************************************************************/
static bool wslPathIsReadable(const QString &distro, const QString &linuxPath, int timeoutMs)
{
    const QString cmd = QString("test -r %1 && echo 1 || echo 0").arg(shellQuoteSingle(linuxPath));
    const QString out = runWslCmdCapture(distro, QStringList() << "bash" << "-lc" << cmd, timeoutMs);
    return out.trimmed() == QLatin1String("1");
}

/*!*******************************************************************************************************************
 * \brief Converts a Windows path to a Linux path suitable for WSL, preserving Linux paths.
 *
 * On Windows:
 *  - If \a path starts with '/', it is assumed to be already a Linux path (WSL) and returned as-is.
 *  - Otherwise, \c wslpath -a is used inside the selected WSL distro to convert the Windows path
 *    into an absolute Linux path.
 *
 * \param path      Input path (Windows path or WSL absolute path).
 * \param distro    WSL distribution name.
 * \param timeoutMs Timeout for WSL conversion in milliseconds.
 * \return Linux/WSL absolute path on success; empty string on failure.
 **********************************************************************************************************************/
static QString toLinuxPathForWsl(const QString &path, const QString &distro, int timeoutMs)
{
    const QString p = path.trimmed();
    if (p.isEmpty())
        return QString();

    if (p.startsWith('~') || p.startsWith('/'))
        return p;

    const QString cmd =
        QString("wslpath -a %1").arg(shellQuoteSingle(QDir::toNativeSeparators(p)));

    const QString out =
        runWslCmdCapture(distro, QStringList() << "bash" << "-lc" << cmd, timeoutMs);

    return out.trimmed();
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

/*!*******************************************************************************************************************
 * \brief Finds the absolute path to wsl.exe on Windows.
 *
 * Tries common locations in order:
 *  - C:\Windows\System32\wsl.exe
 *  - C:\Windows\Sysnative\wsl.exe
 *  - PATH lookup via QStandardPaths::findExecutable("wsl")
 *
 * \return Absolute path to wsl.exe if found; empty string otherwise.
 **********************************************************************************************************************/
static QString wslExecutablePathImpl()
{
    const QString sys32 = QStringLiteral("C:\\Windows\\System32\\wsl.exe");
    if (QFileInfo::exists(sys32))
        return sys32;

    const QString sysnative = QStringLiteral("C:\\Windows\\Sysnative\\wsl.exe");
    if (QFileInfo::exists(sysnative))
        return sysnative;

    return QStandardPaths::findExecutable(QStringLiteral("wsl"));
}

/*!*******************************************************************************************************************
 * \brief Decodes raw output bytes produced by wsl.exe into a QString.
 *
 * In some Windows configurations, wsl.exe writes UTF-16LE text when stdout/stderr
 * is redirected to a pipe (as with QProcess). This helper detects such output by
 * the presence of NUL bytes and decodes it as UTF-16 (handling optional BOM).
 * Otherwise it falls back to QString::fromLocal8Bit().
 *
 * \param ba Raw output bytes (stdout or stderr).
 * \return Decoded text (may be empty if input is empty or cannot be decoded).
 **********************************************************************************************************************/
QString decodeWslOutput(const QByteArray &ba)
{
    if (ba.isEmpty())
        return {};

    if (ba.contains('\0')) {
        int offset = 0;

        if (ba.size() >= 2) {
            const uchar b0 = static_cast<uchar>(ba[0]);
            const uchar b1 = static_cast<uchar>(ba[1]);
            if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF))
                offset = 2;
        }

        const int bytes = ba.size() - offset;
        const int u16len = bytes / 2;
        if (u16len > 0) {
            return QString::fromUtf16(
                reinterpret_cast<const ushort*>(ba.constData() + offset),
                u16len
                );
        }
        return {};
    }

    return QString::fromLocal8Bit(ba);
}

#endif

/*!*******************************************************************************************************************
 * \brief Lists installed WSL distributions available on the system (Windows only).
 *
 * Runs: \c wsl.exe -l -v, then parses the output and returns distro names.
 * The parser:
 *  - skips the header line starting with "NAME"
 *  - removes a leading '*' marker from the default distro line
 *  - extracts the distro name from the first column
 *
 * If WSL is not available, the command fails, or a timeout occurs, returns an empty list.
 *
 * \param timeoutMs Maximum time to wait for process start and completion, in milliseconds.
 * \return List of distro names; empty on failure or non-Windows platforms.
 **********************************************************************************************************************/
QStringList listWslDistrosFromSystem(int timeoutMs)
{
#ifdef Q_OS_WIN
    const QString wslExe = wslExePath();
    if (wslExe.isEmpty())
        return {};

    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);

    p.start(wslExe, QStringList() << "-l" << "-v");

    if (!p.waitForStarted(timeoutMs))
        return {};

    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(200);
        return {};
    }

    const QByteArray outBa = p.readAllStandardOutput();
    const QByteArray errBa = p.readAllStandardError();

    const QString out  = decodeWslOutput(outBa);
    const QString err  = decodeWslOutput(errBa);

    const QString text = (out + "\n" + err);

    QStringList result;

    const QStringList lines = text.split(QRegExp("[\r\n]+"), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith("NAME", Qt::CaseInsensitive))
            continue;

        if (line.startsWith('*')) {
            line.remove(0, 1);
            line = line.trimmed();
        }

        int sep = line.indexOf(QRegExp("\\s{2,}"));
        QString name = (sep > 0) ? line.left(sep).trimmed()
                                 : line.section(' ', 0, 0).trimmed();

        if (!name.isEmpty() && !result.contains(name))
            result << name;
    }

    return result;
#else
    Q_UNUSED(timeoutMs);
    return {};
#endif
}

/*!*******************************************************************************************************************
 * \brief Exports the selected WSL distribution to a process environment variable.
 *
 * Reads the WSL distribution name from the preferences map and stores it in the
 * process-local environment variable \c EMSTUDIO_WSL_DISTRO. This allows parts of
 * the application that do not have direct access to the preferences map to determine
 * the selected WSL distribution via \c qgetenv().
 *
 * On Windows:
 *  - If the stored distribution name is empty, the environment variable is cleared.
 *  - Otherwise, the distribution name is exported as-is.
 *
 * On non-Windows platforms, this function has no effect.
 *
 * \param prefs Preferences map containing the \c WSL_DISTRO entry.
 **********************************************************************************************************************/
void exportWslDistroToEnv(const QMap<QString, QVariant>& prefs)
{
#ifdef Q_OS_WIN
    const QString distro = prefs.value("WSL_DISTRO").toString().trimmed();

    if (distro.isEmpty()) {
        qputenv("EMSTUDIO_WSL_DISTRO", QByteArray());
    } else {
        qputenv("EMSTUDIO_WSL_DISTRO", distro.toLocal8Bit());
    }
#else
    Q_UNUSED(prefs);
#endif
}

/*!*******************************************************************************************************************
 * \brief Returns absolute path to wsl.exe (System32/Sysnative preferred).
 **********************************************************************************************************************/
QString wslExePath()
{
#ifdef Q_OS_WIN
    return wslExecutablePathImpl();
#else
    return QString();
#endif
}

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
QString runWslCmdCapture(const QString &distro,
                         const QStringList &cmd,
                         int timeoutMs)
{
    QProcess p;

    QStringList args;

    // Use explicit distro only if provided; otherwise default WSL distro is used
    if (!distro.trimmed().isEmpty()) {
        args << "-d" << distro;
    }

    args << "--";
    args << cmd;

    const QString wslExe = wslExePath();
    if (wslExe.isEmpty())
        return QString();

    p.start(wslExe, args);

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

#ifdef Q_OS_WIN
    // wsl.exe may emit UTF-16LE on redirected stdout; also ignore leading warning lines.
    const QString decoded = decodeWslOutput(p.readAllStandardOutput()).trimmed();
    if (decoded.isEmpty())
        return QString();

    const QStringList lines = decoded.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return lines.isEmpty() ? decoded : lines.last().trimmed();
#else
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
#endif
}

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
    if (p.startsWith('/') || p.startsWith('~'))
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

    if (p.startsWith('/') || p.startsWith('~'))
        return wslPathIsExecutable(distro, p, timeoutMs);

    return QFileInfo(p).isExecutable();
#else
    Q_UNUSED(distro);
    Q_UNUSED(timeoutMs);
    return QFileInfo(path).isExecutable();
#endif
}

#ifdef Q_OS_WIN

/*!*******************************************************************************************************************
 * \brief Returns the result of the asynchronous WSL executable check of \p base + \p suffix.
 *
 * Results come from startWslToolProbe() and stay cached until refreshSimToolOptions().
 * Without wsl.exe the answer is No.
 *
 * \param base    Path inside WSL ("/..." or "~...") or a Windows path (converted with wslpath inside WSL).
 * \param suffix  Relative path appended to the converted \p base (e.g. "bin/palace"), or empty.
 * \param pending Receives the probe item when it hasn't been checked yet.
 * \return Yes / No / NoAnswer from the probe, or Unknown while it is pending.
 **********************************************************************************************************************/
MainWindow::WslCheck MainWindow::wslExecutableCheck(const QString &base, const QString &suffix,
                                                    QStringList &pending) const
{
    if (!isWslAvailable())
        return WslCheck::No;

    const QString item = base + QLatin1Char('\n') + suffix;
    const WslCheck c = m_wslExecChecks.value(item, WslCheck::Unknown);
    if (c == WslCheck::Unknown && !pending.contains(item))
        pending << item;
    return c;
}

/*!*******************************************************************************************************************
 * \brief Checks WSL paths for executability with one asynchronous wsl.exe call.
 *
 * Runs a bash script fed through stdin (no command-line quoting through wsl.exe) that prints
 * "@EMSTUDIO <index> 1|0" per item. A cold WSL start can take many seconds, so the call doesn't block the
 * UI; after 60 s it is killed. Items without an answer are stored as NoAnswer. When done, the results
 * go into \c m_wslExecChecks and rebuildSimToolCombo() runs again.
 *
 * \param items  Probe items from wslExecutableCheck() ("base\nsuffix").
 * \param distro WSL distribution, or empty for the default one.
 **********************************************************************************************************************/
void MainWindow::startWslToolProbe(const QStringList &items, const QString &distro)
{
    stopWslToolProbe();
    m_wslProbeDistro = distro;

    // Linux paths are tested as they are; Windows paths are converted with wslpath first.
    QString script = QStringLiteral(
        "t() {\n"
        "  b=$1\n"
        "  case \"$b\" in\n"
        "    /*|\"~\"*) ;;\n"
        "    *) b=$(wslpath -a \"$b\" 2>/dev/null) || b= ;;\n"
        "  esac\n"
        "  if [ -n \"$b\" ] && [ -n \"$2\" ]; then b=\"${b%/}/$2\"; fi\n"
        "  if [ -n \"$b\" ] && test -x \"$b\"; then echo \"@EMSTUDIO $3 1\"; else echo \"@EMSTUDIO $3 0\"; fi\n"
        "}\n");
    for (int i = 0; i < items.size(); ++i) {
        const QString base   = items[i].section(QLatin1Char('\n'), 0, 0);
        const QString suffix = items[i].section(QLatin1Char('\n'), 1);
        const bool isLinux   = base.startsWith('/') || base.startsWith('~');
        script += QStringLiteral("t ") + shellQuoteSingle(isLinux ? base : QDir::toNativeSeparators(base))
                + QLatin1Char(' ') + shellQuoteSingle(suffix)
                + QLatin1Char(' ') + QString::number(i) + QLatin1Char('\n');
    }

    QStringList args;
    if (!distro.isEmpty())
        args << QStringLiteral("-d") << distro;
    args << QStringLiteral("--") << QStringLiteral("bash") << QStringLiteral("-s");

    auto *p = new QProcess(this);
    m_wslProbeProcess = p;

    const auto done = [this, p, items](const QString &out) {
        if (m_wslProbeProcess != p)
            return;
        m_wslProbeProcess = nullptr;
        p->deleteLater();

        for (const QString &item : items)
            m_wslExecChecks[item] = WslCheck::NoAnswer;
        const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() != 3 || parts[0] != QLatin1String("@EMSTUDIO"))
                continue;
            bool ok = false;
            const int i = parts[1].toInt(&ok);
            if (ok && i >= 0 && i < items.size())
                m_wslExecChecks[items[i]] = (parts[2] == QLatin1String("1")) ? WslCheck::Yes : WslCheck::No;
        }
        rebuildSimToolCombo();
    };

    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [p, done](int, QProcess::ExitStatus) { done(decodeWslOutput(p->readAllStandardOutput())); });
    connect(p, &QProcess::errorOccurred, this, [done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            done(QString());
    });
    QTimer::singleShot(60000, p, [p]() { p->kill(); });

    p->start(wslExePath(), args);
    p->write(script.toUtf8());
    p->closeWriteChannel();
}

/*!*******************************************************************************************************************
 * \brief Abandons a running WSL tool path probe; its results are discarded.
 **********************************************************************************************************************/
void MainWindow::stopWslToolProbe()
{
    if (!m_wslProbeProcess)
        return;
    QProcess *p = m_wslProbeProcess;
    m_wslProbeProcess = nullptr;
    p->disconnect(this);
    p->kill();
    p->waitForFinished(1000);
    p->deleteLater();
}

#endif

/*!*******************************************************************************************************************
 * \brief Checks whether WSL is available on this system.
 **********************************************************************************************************************/
bool isWslAvailable()
{
#ifdef Q_OS_WIN
    return !wslExePath().isEmpty();
#else
    return false;
#endif
}

/*!*******************************************************************************************************************
 * \brief Checks readability by trying local filesystem first, then WSL for Linux-style absolute paths.
 **********************************************************************************************************************/
bool isReadableLocalThenWsl(const QString &path, const QString &distro, int timeoutMs)
{
    const QString p = path.trimmed();
    if (p.isEmpty())
        return false;

    // 1) First try locally (host FS)
    if (QFileInfo(p).isReadable())
        return true;

#ifdef Q_OS_WIN
    // 2) Fallback to WSL (convert Windows path if needed)
    if (!isWslAvailable())
        return false;

    const QString linuxPath = toLinuxPathForWsl(p, distro, timeoutMs);
    if (linuxPath.isEmpty())
        return false;

    return wslPathIsReadable(distro, linuxPath, timeoutMs);
#else
    Q_UNUSED(distro);
    Q_UNUSED(timeoutMs);
    return false;
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

    if (p == QLatin1String("~") || p.startsWith("~/")) {
        const QString tail = (p == QLatin1String("~")) ? QString() : p.mid(2);
        const QString cmd = QString("printf '%s' \"$HOME%1\"")
                                .arg(tail.isEmpty() ? QString() : QLatin1String("/") + tail);
        return runWslCmdCapture(distro, QStringList() << "bash" << "-lc" << cmd, timeoutMs).trimmed();
    }

    const QString cmd =
        QString("wslpath -a %1").arg(shellQuoteSingle(QDir::toNativeSeparators(p)));

    return runWslCmdCapture(distro, QStringList() << "bash" << "-lc" << cmd, timeoutMs).trimmed();
#else
    Q_UNUSED(distro);
    Q_UNUSED(timeoutMs);
    return path;
#endif
}
