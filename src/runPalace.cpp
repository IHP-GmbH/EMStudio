/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 ************************************************************************/

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTextCursor>

#include "wslHelper.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"


#ifdef Q_OS_WIN

/*!*******************************************************************************************************************
 * \brief Parses physical CPU core count from \c lscpu CSV output.
 *
 * Interprets the output of \c lscpu -p=CORE,SOCKET and counts unique
 * (socket, core) pairs, effectively determining the number of
 * physical CPU cores without hyper-threading.
 *
 * Comment lines (starting with '#') and malformed entries are ignored.
 *
 * \param out Raw CSV output produced by \c lscpu -p=CORE,SOCKET.
 *
 * \return Number of detected physical CPU cores as a string,
 *         or an empty string if parsing fails.
 **********************************************************************************************************************/
static QString parsePhysicalCoresFromLscpuCsv(QString out)
{
    out.replace('\r', "");

    QSet<QString> cores; // "socket:core"
    const QStringList lines = out.split('\n', Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        if (line.startsWith('#'))
            continue;

        const QStringList parts = line.split(',', Qt::KeepEmptyParts);
        if (parts.size() < 2)
            continue;

        const QString core   = parts.at(0).trimmed();
        const QString socket = parts.at(1).trimmed();
        if (core.isEmpty() || socket.isEmpty())
            continue;

        cores.insert(socket + ":" + core);
    }

    return cores.isEmpty() ? QString() : QString::number(cores.size());
}
#endif

/*!*******************************************************************************************************************
 * \brief Executes the Palace simulation workflow.
 *
 * Runs the Palace simulation in two stages:
 *  - Stage 1: Executes the Palace Python model script to generate a configuration.
 *  - Stage 2: Locates the generated Palace configuration file and launches the Palace solver.
 *
 * Depending on platform and user preferences, execution may occur natively,
 * under WSL, or via an external launcher script.
 *
 * The function manages process lifetime, logging, and phase transitions internally.
 **********************************************************************************************************************/
void MainWindow::runPalace(bool interactive)
{
    if (m_simProcess && m_simProcess->state() == QProcess::Running) {
        info("Simulation is already running.", true);
        return;
    }

    if (interactive) {
        if (currentSimToolKey() == QLatin1String("elmer"))
            m_simSettings[QStringLiteral("elmer")] = true;
        if (currentSimToolKey() == QLatin1String("elmer"))
            m_simSettings[QStringLiteral("iterative")] = true;

        syncGuiSettingsToPythonEditor();
        on_actionSave_triggered();
    } else {
        if (!applyPythonScriptFromEditor()) {
            error("Failed to apply Python script in headless mode.", true);
            QCoreApplication::exit(2);
            return;
        }
        saveSettings();
        setStateSaved();
    }

    PalaceRunContext ctx;
    QString err;
    if (!buildPalaceRunContext(ctx, err)) {
        error(err, true);
        if (!interactive) {
            QCoreApplication::exit(1);
        }

        return;
    }

    m_palacePythonOutput.clear();
    m_ui->editSimulationLog->clear();

    logPalaceStartupInfo(ctx);

    m_simProcess = new QProcess(this);
    m_palacePhase = PalacePhase::PythonModel;

    connectPalaceProcessIo();

    connect(m_simProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus) { onPalaceProcessFinished(exitCode); });

    startPalacePythonStage(ctx);

    if (!m_simProcess->waitForStarted(3000)) {
        if (ctx.simKeyLower == QLatin1String("elmer"))
            error("Failed to start gds2palace Python preprocessing (Windows native).", false);
#ifdef Q_OS_WIN
        else if (!ctx.useWsl)
            error("Failed to start Palace Python preprocessing.", false);
        else
            error("Failed to start Palace Python preprocessing under WSL.", false);
#else
        else
            error("Failed to start Palace Python preprocessing.", false);
#endif
        m_simProcess->deleteLater();
        m_simProcess = nullptr;
        m_palacePhase = PalacePhase::None;

        if (!interactive)
            QCoreApplication::exit(3);
    }
}

/*!*******************************************************************************************************************
 * \brief Validates Palace-related settings and prepares execution context.
 *
 * Collects and validates all information required to run the Palace workflow,
 * including model paths, run mode, launcher configuration, Python interpreter,
 * Palace installation path, and platform-specific details.
 *
 * On success, fills \a ctx with resolved paths and parameters.
 *
 * \param[out] ctx       Execution context to be populated.
 * \param[out] outError  Human-readable error message in case of failure.
 *
 * \return True if the context was successfully built; false otherwise.
 **********************************************************************************************************************/
bool MainWindow::buildPalaceRunContext(PalaceRunContext &ctx, QString &outError)
{
    ctx.simKeyLower = currentSimToolKey().toLower();
    if (ctx.simKeyLower != QLatin1String("palace") && ctx.simKeyLower != QLatin1String("elmer")) {
        outError = QStringLiteral("Current simulation tool is not Palace or Elmer.");
        return false;
    }

    ctx.modelWin = m_simSettings.value("RunPythonScript").toString().trimmed();
    if (ctx.modelWin.isEmpty() || !QFileInfo::exists(ctx.modelWin)) {
        outError = QStringLiteral("Palace Python model script is not specified or does not exist.");
        return false;
    }

    ctx.runMode = m_preferences.value("PALACE_RUN_MODE", 0).toInt();

    bool isScriptMode = false;
    if (ctx.runMode == 1 && ctx.simKeyLower != QLatin1String("elmer")) {
        ctx.launcherWin = m_preferences.value("PALACE_RUN_SCRIPT").toString().trimmed();
        if (ctx.launcherWin.isEmpty()) {
            outError = QStringLiteral("PALACE_RUN_SCRIPT is not configured.");
            return false;
        }

#ifdef Q_OS_WIN
        ctx.launcherWin = toLinuxPathPortable(ctx.launcherWin, ctx.distro, 8000);
#endif

        if (!pathIsExecutablePortable(ctx.launcherWin, ctx.distro, 8000)) {
            outError = QStringLiteral("PALACE_RUN_SCRIPT must point to an executable file.");
            return false;
        }

        isScriptMode = true;
    }


    const QFileInfo fi(ctx.modelWin);
    ctx.baseName = fi.completeBaseName();
    if (ctx.baseName.isEmpty()) {
        outError = QStringLiteral("Cannot infer Palace run directory (empty model basename).");
        return false;
    }

    ctx.runDirGuessWin = QDir(fi.absolutePath())
                             .filePath(QStringLiteral("palace_model/%1_data").arg(ctx.baseName));

    ctx.palaceRoot = m_preferences.value("PALACE_INSTALL_PATH").toString().trimmed();
    if (ctx.palaceRoot.isEmpty() && !isScriptMode && ctx.simKeyLower != QLatin1String("elmer")) {
        outError = QStringLiteral("PALACE_INSTALL_PATH is not configured in Preferences.");
        return false;
    }

#ifdef Q_OS_WIN
    ctx.useWsl = (ctx.simKeyLower != QLatin1String("elmer"));

    if (ctx.useWsl) {
        if (!ensureWslAvailable(outError))
            return false;

        ctx.distro = m_preferences.value("WSL_DISTRO").toString().trimmed();

        QString palaceRootLinux = ctx.palaceRoot;
        if (!palaceRootLinux.startsWith('/') &&
            !palaceRootLinux.startsWith('~')) {
            palaceRootLinux = toWslPath(palaceRootLinux);
        }

        ctx.palaceExeLinux = QDir(palaceRootLinux).filePath("bin/palace");
        ctx.modelDirLinux  = toWslPath(QFileInfo(ctx.modelWin).absolutePath());
        ctx.modelLinux     = toWslPath(ctx.modelWin);

        ctx.pythonCmd = m_preferences.value("PALACE_PYTHON").toString().trimmed();
        if (ctx.pythonCmd.isEmpty())
            ctx.pythonCmd = QStringLiteral("python3");
    } else {
        const QString solverPath =
            m_preferences.value(QStringLiteral("ELMER_SOLVER_PATH")).toString().trimmed();
        if (solverPath.isEmpty() || !QFileInfo::exists(solverPath)) {
            outError = QStringLiteral("ELMER_SOLVER_PATH is not configured or does not exist.");
            return false;
        }

        ctx.modelDirLinux = QFileInfo(ctx.modelWin).absolutePath();
        ctx.modelLinux    = ctx.modelWin;

        if (!resolveElmerPythonLaunch(ctx.pythonCmd, ctx.pythonArgs)) {
            outError = QStringLiteral("No Windows Python found for Elmer preprocessing. "
                                      "Set ELMER_PYTHON in Preferences.");
            return false;
        }
    }
#else
    ctx.palaceExeLinux = QDir(ctx.palaceRoot).filePath("bin/palace");
    ctx.modelDirLinux  = QFileInfo(ctx.modelWin).absolutePath();
    ctx.modelLinux     = ctx.modelWin;

    if (ctx.simKeyLower == QLatin1String("elmer")) {
        if (!resolveElmerPythonLaunch(ctx.pythonCmd, ctx.pythonArgs)) {
            outError = QStringLiteral("No Python found for Elmer preprocessing. Set ELMER_PYTHON in Preferences.");
            return false;
        }
    } else {
        ctx.pythonCmd = m_preferences.value("PALACE_PYTHON").toString().trimmed();
        if (ctx.pythonCmd.isEmpty())
            ctx.pythonCmd = QStringLiteral("python3");
    }
#endif

    return true;
}

/*!*******************************************************************************************************************
 * \brief Writes Palace startup information to the simulation log.
 *
 * Prints a short banner describing the execution mode (native, WSL, launcher),
 * selected Python interpreter, initial run directory guess, and optional launcher
 * script information.
 *
 * \param ctx Prepared Palace execution context.
 **********************************************************************************************************************/
void MainWindow::logPalaceStartupInfo(const PalaceRunContext &ctx)
{
#ifdef Q_OS_WIN
    if (!ctx.useWsl) {
        m_ui->editSimulationLog->insertPlainText(
            QStringLiteral("Starting gds2palace Python preprocessing (Windows native)...\n"));
    } else if (ctx.runMode == 1) {
        m_ui->editSimulationLog->insertPlainText(
            QString("Starting Palace Python preprocessing in WSL (%1) [launcher mode]...\n").arg(ctx.distro));
    } else {
        m_ui->editSimulationLog->insertPlainText(
            QString("Starting Palace Python preprocessing in WSL (%1)...\n").arg(ctx.distro));
    }
#else
    if (ctx.simKeyLower == QLatin1String("elmer"))
        m_ui->editSimulationLog->insertPlainText(
            QStringLiteral("Starting gds2palace Python preprocessing (native)...\n"));
    else if (ctx.runMode == 1)
        m_ui->editSimulationLog->insertPlainText("Starting Palace Python preprocessing (launcher mode)...\n");
    else
        m_ui->editSimulationLog->insertPlainText("Starting Palace Python preprocessing (native)...\n");
#endif

    m_ui->editSimulationLog->insertPlainText(QString("[Using Python: %1]\n").arg(ctx.pythonCmd));
    m_ui->editSimulationLog->insertPlainText(QString("[Initial Palace run directory guess: %1]\n").arg(ctx.runDirGuessWin));

    if (ctx.simKeyLower == QLatin1String("elmer")) {
        const QString solverPath =
            m_preferences.value(QStringLiteral("ELMER_SOLVER_PATH")).toString().trimmed();
        if (!solverPath.isEmpty()) {
            m_ui->editSimulationLog->insertPlainText(
                QString("[Elmer tools from: %1]\n").arg(QDir::toNativeSeparators(solverPath)));
        } else {
            m_ui->editSimulationLog->insertPlainText(
                "[Warning] ELMER_SOLVER_PATH is not set.\n");
        }
    }

    if (ctx.runMode == 1 && ctx.useWsl) {
        m_ui->editSimulationLog->insertPlainText(
            QString("[Launcher script: %1]\n").arg(QDir::toNativeSeparators(ctx.launcherWin)));
    }
}

/*!*******************************************************************************************************************
 * \brief Starts the Palace Python preprocessing stage.
 *
 * Launches the Palace Python model script either natively or under WSL,
 * depending on the current platform and configuration.
 *
 * The function assumes that \c m_simProcess is already created.
 *
 * \param ctx Prepared Palace execution context.
 **********************************************************************************************************************/
void MainWindow::startPalacePythonStage(const PalaceRunContext &ctx)
{
#ifdef Q_OS_WIN
    if (!ctx.useWsl) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        applyElmerHomeToProcessEnv(env);

        m_simProcess->setProcessEnvironment(env);
        m_simProcess->setWorkingDirectory(ctx.modelDirLinux);

        QStringList args = ctx.pythonArgs;
        args << ctx.modelLinux;
        m_simProcess->start(ctx.pythonCmd, args);
        return;
    }

    const QString wslExe = wslExePath();
    if (wslExe.isEmpty()) {
        error("WSL is not available (wsl.exe not found).", false);
        return;
    }

    QStringList args;
    args << "-d" << ctx.distro
         << "--" << "bash" << "-lc"
         << QString("cd %1 && %2 %3")
                .arg(shellQuoteSingle(ctx.modelDirLinux),
                     ctx.pythonCmd,
                     shellQuoteSingle(ctx.modelLinux));

    m_simProcess->start(wslExe, args);
#else
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (ctx.simKeyLower == QLatin1String("elmer"))
        applyElmerHomeToProcessEnv(env);

    m_simProcess->setProcessEnvironment(env);
    m_simProcess->setWorkingDirectory(ctx.modelDirLinux);

    QStringList args = ctx.pythonArgs;
    args << ctx.modelLinux;
    m_simProcess->start(ctx.pythonCmd, args);
#endif
}

/*!*******************************************************************************************************************
 * \brief Connects Palace process output streams to the simulation log.
 *
 * Attaches handlers for standard output and standard error of the currently
 * running Palace process, appending all received text to the simulation log
 * widget while preserving cursor position.
 **********************************************************************************************************************/
void MainWindow::connectPalaceProcessIo()
{
    connect(m_simProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        appendToSimulationLog(m_simProcess->readAllStandardOutput());
    });
    connect(m_simProcess, &QProcess::readyReadStandardError, this, [this]() {
        appendToSimulationLog(m_simProcess->readAllStandardError());
    });
}

/*!*******************************************************************************************************************
 * \brief Appends raw process output to the simulation log.
 *
 * Inserts the given byte array at the end of the simulation log editor
 * without disturbing user selection or triggering additional signals.
 *
 * \param data Raw UTF-8 encoded output from a running process.
 **********************************************************************************************************************/
void MainWindow::appendToSimulationLog(const QByteArray &data)
{
    if (data.isEmpty())
        return;

    if (m_headless) {
        fwrite(data.constData(), 1, size_t(data.size()), stdout);
        fflush(stdout);
    }

    QSignalBlocker blocker(m_ui->editSimulationLog);
    m_ui->editSimulationLog->moveCursor(QTextCursor::End);
    m_ui->editSimulationLog->insertPlainText(QString::fromUtf8(data));
    m_ui->editSimulationLog->moveCursor(QTextCursor::End);
}

/*!*******************************************************************************************************************
 * \brief Handles completion of Palace process stages.
 *
 * Reacts to process termination depending on the current Palace phase:
 *  - After Python preprocessing, attempts to detect the run directory and
 *    launches the Palace solver stage.
 *  - After solver completion, finalizes logging and resets internal state.
 *
 * \param exitCode Exit code returned by the finished process.
 **********************************************************************************************************************/
void MainWindow::onPalaceProcessFinished(int exitCode)
{
    const int runMode = m_preferences.value("PALACE_RUN_MODE", 0).toInt();

    if (m_palacePhase == PalacePhase::PythonModel) {
        if (exitCode != 0) {
            appendToSimulationLog(
                QString("\n[Palace Python preprocessing finished with exit code %1]\n")
                    .arg(exitCode).toUtf8());

            if (m_simProcess) {
                m_simProcess->deleteLater();
                m_simProcess = nullptr;
            }
            m_palacePhase = PalacePhase::None;

            if (m_headless)
                QCoreApplication::exit(exitCode);

            return;
        }

        const QString detectedRunDir = detectRunDirFromLog();
        if (!detectedRunDir.isEmpty()) {
            m_simSettings["RunDir"] = detectedRunDir;
        } else {
            const QString scriptPath = m_simSettings.value("RunPythonScript").toString().trimmed();
            if (scriptPath.isEmpty() || !QFileInfo::exists(scriptPath)) {
                error(QString("Python file '%1' does not exist.").arg(scriptPath), true);

                if (m_simProcess) {
                    m_simProcess->deleteLater();
                    m_simProcess = nullptr;
                }
                m_palacePhase = PalacePhase::None;

                if (m_headless)
                    QCoreApplication::exit(1);

                return;
            }

            m_simSettings["RunDir"] = QFileInfo(scriptPath).absolutePath();
        }

        appendToSimulationLog(
            "\n[gds2palace Python preprocessing finished successfully, searching for solver...]\n");

        PalaceRunContext ctx;
        QString err;
        if (!buildPalaceRunContext(ctx, err)) {
            error(err, true);

            if (m_simProcess) {
                m_simProcess->deleteLater();
                m_simProcess = nullptr;
            }
            m_palacePhase = PalacePhase::None;

            if (m_headless)
                QCoreApplication::exit(1);

            return;
        }

        ctx.detectedRunDirWin = detectedRunDir;

        const QString runDir = resolveGds2PalaceRunDir(ctx);
        const Gds2PalaceSolverKind solverKind =
            detectGds2PalaceSolverKind(runDir, ctx.simKeyLower);

        appendToSimulationLog(
            QString("[Using simulation tool: %1]\n")
                .arg(ctx.simKeyLower == QLatin1String("elmer") ? QStringLiteral("Elmer")
                    : ctx.simKeyLower == QLatin1String("palace") ? QStringLiteral("Palace")
                                                                : solverKind == Gds2PalaceSolverKind::Elmer ? QStringLiteral("Elmer")
                                                                : solverKind == Gds2PalaceSolverKind::Palace ? QStringLiteral("Palace")
                                                                                                            : QStringLiteral("unknown"))
                .toUtf8());

        if (solverKind == Gds2PalaceSolverKind::Elmer) {
            startElmerSolverStage(ctx);
        } else if (solverKind == Gds2PalaceSolverKind::Palace) {
            startPalaceSolverStage(ctx);
        } else {
            failPalaceSolver(
                QStringLiteral("Cannot determine solver in run directory: %1").arg(runDir),
                true);
        }
        return;
    }

    if (m_palacePhase == PalacePhase::PalaceSolver) {
        const QString simKey = currentSimToolKey();
        QString msg;
        if (simKey == QLatin1String("elmer"))
            msg = QString("\n[Elmer solver finished with exit code %1]\n").arg(exitCode);
        else if (runMode == 1)
            msg = QString("\n[Palace launcher finished with exit code %1]\n").arg(exitCode);
        else
            msg = QString("\n[Palace solver finished with exit code %1]\n").arg(exitCode);

        appendToSimulationLog(msg.toUtf8());

        if (m_simProcess) {
            m_simProcess->deleteLater();
            m_simProcess = nullptr;
        }
        m_palacePhase = PalacePhase::None;

        if (m_headless)
            QCoreApplication::exit(exitCode);

        return;
    }

    if (m_headless)
        QCoreApplication::exit(exitCode);
}

/*!*******************************************************************************************************************
 * \brief Attempts to detect the Palace simulation data directory from log output.
 *
 * Searches the accumulated simulation log for a line indicating the Palace
 * simulation data directory and converts it to a native path if required.
 *
 * \return Detected run directory path, or an empty string if not found.
 **********************************************************************************************************************/
QString MainWindow::detectRunDirFromLog() const
{
    const QString log = m_ui->editSimulationLog->toPlainText();
    QRegularExpression re(R"(Simulation data directory:\s*([^\s]+))");
    QRegularExpressionMatch m = re.match(log);
    if (!m.hasMatch())
        return QString();

    const QString simDir = m.captured(1).trimmed();

#ifdef Q_OS_WIN
    return wslToWinPath(simDir);
#else
    return simDir;
#endif
}

/*!*******************************************************************************************************************
 * \brief Constructs a default Palace run directory guess.
 *
 * Builds the expected Palace run directory based on the Python model location
 * and model base name. The directory is only returned if it already exists.
 *
 * \param modelFile Full path to the Palace Python model file.
 * \param baseName  Base name of the model without extension.
 *
 * \return Existing default run directory path, or an empty string if not found.
 **********************************************************************************************************************/
QString MainWindow::guessDefaultPalaceRunDir(const QString &modelFile, const QString &baseName) const
{
    const QString defRunDir = QFileInfo(modelFile).absolutePath()
    + QStringLiteral("/palace_model/%1_data").arg(baseName);
    if (!QFileInfo::exists(defRunDir))
        return QString();
    return defRunDir;
}

/*!*******************************************************************************************************************
 * \brief Selects the directory to search for Palace configuration files.
 *
 * Prefers a detected run directory (if available); otherwise falls back
 * to a default guessed directory.
 *
 * \param detectedRunDir Run directory detected from log output.
 * \param defaultRunDir  Fallback run directory guess.
 *
 * \return Directory path to be used for configuration search.
 **********************************************************************************************************************/
QString MainWindow::chooseSearchDir(const QString &detectedRunDir, const QString &defaultRunDir) const
{
    return detectedRunDir.isEmpty() ? defaultRunDir : detectedRunDir;
}

/*!*******************************************************************************************************************
 * \brief Finds a Palace configuration JSON file in the given directory.
 *
 * Searches for readable *.json files in \a runDir, preferring a file named
 * "config.json" (case-insensitive). If not found, selects the newest JSON file.
 *
 * \param runDir Directory to search for Palace configuration files.
 *
 * \return Absolute path to the selected config file, or an empty string if none found.
 **********************************************************************************************************************/
QString MainWindow::findPalaceConfigJson(const QString &runDir) const
{
    QDir dir(runDir);
    dir.setFilter(QDir::Files | QDir::Readable | QDir::NoSymLinks);
    dir.setNameFilters(QStringList() << "*.json");
    dir.setSorting(QDir::Time | QDir::Reversed);

    const QFileInfoList files = dir.entryInfoList();
    if (files.isEmpty())
        return QString();

    QString best = files.first().absoluteFilePath();

    for (const QFileInfo &fi : files) {
        if (fi.completeBaseName().compare(QLatin1String("config"), Qt::CaseInsensitive) == 0 &&
            fi.completeSuffix().compare(QLatin1String("json"), Qt::CaseInsensitive) == 0)
        {
            best = fi.absoluteFilePath();
            break;
        }
    }

    return best;
}

/*!*******************************************************************************************************************
 * \brief Queries the number of available CPU cores inside WSL.
 *
 * Executes the \c nproc command inside the specified WSL distribution and returns
 * the number of logical CPU cores visible to Linux. This reflects possible limits
 * imposed by .wslconfig (e.g. processors=).
 *
 * \param distro WSL distribution name (e.g. "Ubuntu").
 *
 * \return Number of CPU cores reported by \c nproc, or an empty string on failure.
 **********************************************************************************************************************/
QString MainWindow::queryWslCpuCores(const QString &distro) const
{
#ifdef Q_OS_WIN
    return runWslCmdCapture(distro, QStringList() << "nproc", 3000).trimmed();
#else
    Q_UNUSED(distro);
    return QString();
#endif
}

/*!*******************************************************************************************************************
 * \brief Detects the number of available CPU cores for MPI execution.
 *
 * On Windows, queries the core count via WSL using \c nproc.
 * On Linux, executes \c nproc directly.
 *
 * If detection fails, returns "1" as a safe fallback.
 *
 * \param distro WSL distribution name (used only on Windows).
 * \return Number of available CPU cores as string.
 **********************************************************************************************************************/
MainWindow::CoreCountResult MainWindow::detectMpiCoreCount() const
{
    CoreCountResult r;

#ifdef Q_OS_WIN
    const QString distro = m_preferences.value("WSL_DISTRO").toString().trimmed();

    const QString lscpuOut = runWslCmdCapture(
        distro, QStringList() << "lscpu" << "-p=CORE,SOCKET", 2000);

    const QString phys = parsePhysicalCoresFromLscpuCsv(lscpuOut).trimmed();
    if (!phys.isEmpty()) {
        r.cores  = phys;
        r.source = QStringLiteral("physical (lscpu)");
        return r;
    }

    const QString nprocOut = runWslCmdCapture(
                                 distro, QStringList() << "nproc", 2000).trimmed();

    if (!nprocOut.isEmpty()) {
        r.cores  = nprocOut;
        r.source = QStringLiteral("logical (nproc)");
        return r;
    }

    r.cores  = QStringLiteral("1");
    r.source = QStringLiteral("fallback");
    return r;
#else
    const QString phys = detectPhysicalCoreCountLinux().trimmed();
    if (!phys.isEmpty()) {
        r.cores  = phys;
        r.source = QStringLiteral("physical (lscpu)");
        return r;
    }

    QProcess p;
    p.start(QStringLiteral("nproc"), QStringList());

    if (p.waitForFinished(2000)) {
        const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        if (!out.isEmpty()) {
            r.cores  = out;
            r.source = QStringLiteral("logical (nproc)");
            return r;
        }
    }

    r.cores  = QStringLiteral("1");
    r.source = QStringLiteral("fallback");
    return r;
#endif
}

/*!*******************************************************************************************************************
 * \brief Detects the number of physical CPU cores on Linux.
 *
 * Determines the number of hardware cores (without Hyper-Threading / SMT) by
 * parsing the output of \c lscpu -p=CORE,SOCKET and counting unique (socket,core)
 * pairs. This value is suitable for CPU-bound MPI runs where oversubscribing
 * logical threads is undesirable.
 *
 * \return Number of physical CPU cores as string, or an empty string if detection fails.
 **********************************************************************************************************************/
QString MainWindow::detectPhysicalCoreCountLinux() const
{
    QProcess p;
    p.start(QStringLiteral("lscpu"), QStringList() << "-p=CORE,SOCKET");

    if (!p.waitForFinished(2000))
        return QString();

    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return QString();

    const QString out = QString::fromUtf8(p.readAllStandardOutput());

    QSet<QString> cores; // "socket:core"
    const QStringList lines = out.split('\n', Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        if (line.startsWith('#'))
            continue;

        const QStringList parts = line.split(',', Qt::KeepEmptyParts);
        if (parts.size() < 2)
            continue;

        const QString core   = parts.at(0).trimmed();
        const QString socket = parts.at(1).trimmed();
        if (core.isEmpty() || socket.isEmpty())
            continue;

        cores.insert(socket + ":" + core);
    }

    if (cores.isEmpty())
        return QString();

    return QString::number(cores.size());
}

/*!*******************************************************************************************************************
 * \brief Stops the current Palace run and resets internal solver state.
 *
 * Reports the given error message (optionally via dialog), schedules the active
 * simulation process for deletion, clears the process pointer, and resets the
 * Palace phase to \c PalacePhase::None.
 *
 * This helper is intended to be used as a single exit path for all Palace stage
 * failures to keep cleanup consistent.
 *
 * \param message    Error message to report. If empty, no error is shown.
 * \param showDialog If true, show the error in a dialog; otherwise log it only.
 **********************************************************************************************************************/
void MainWindow::failPalaceSolver(const QString &message, bool showDialog)
{
    if (!message.isEmpty())
        error(message, showDialog);

    if (m_simProcess) {
        m_simProcess->deleteLater();
        m_simProcess = nullptr;
    }

    m_palacePhase = PalacePhase::None;
}

/*!*******************************************************************************************************************
 * \brief Starts the Palace solver stage.
 *
 * Resolves the Palace configuration file, handles launcher mode if enabled,
 * prepares the solver command and MPI core count, logs execution details,
 * and dispatches solver execution to the platform-specific runner.
 *
 * All common logic is platform-independent; OS-specific code is isolated
 * to the final execution step.
 *
 * \param[in,out] ctx Palace execution context, updated with resolved paths.
 **********************************************************************************************************************/
QString MainWindow::resolveGds2PalaceRunDir(const PalaceRunContext &ctx) const
{
    const QString modelFile = m_simSettings.value("RunPythonScript").toString().trimmed();
    QString defRunDir;
    if (!modelFile.isEmpty()) {
        defRunDir = guessDefaultPalaceRunDir(modelFile,
                                             QFileInfo(modelFile).completeBaseName());
    }
    if (defRunDir.isEmpty() && !ctx.runDirGuessWin.isEmpty())
        defRunDir = ctx.runDirGuessWin;

    return chooseSearchDir(ctx.detectedRunDirWin, defRunDir);
}

QString MainWindow::buildElmerEnvShellPrefix() const
{
    const QString solverPath =
        m_preferences.value(QStringLiteral("ELMER_SOLVER_PATH")).toString().trimmed();
    if (solverPath.isEmpty())
        return QString();

    const QFileInfo solverFi(solverPath);
    if (!solverFi.exists())
        return QString();

    const QString binDir = solverFi.absolutePath();
    const QString homeDir = QFileInfo(binDir).absolutePath();

#ifdef Q_OS_WIN
    const QString binPath = toWslPath(binDir);
    const QString homePath = toWslPath(homeDir);
#else
    const QString binPath = binDir;
    const QString homePath = homeDir;
#endif

    return QStringLiteral("export ELMER_HOME=%1 && export PATH=%2:$PATH && ")
        .arg(shellQuoteSingle(homePath), shellQuoteSingle(binPath));
}

void MainWindow::applyElmerHomeToProcessEnv(QProcessEnvironment &env) const
{
    const QString solverPath =
        m_preferences.value(QStringLiteral("ELMER_SOLVER_PATH")).toString().trimmed();
    if (solverPath.isEmpty())
        return;

    const QString binDir = QFileInfo(solverPath).absolutePath();
    const QString homeDir = QFileInfo(binDir).absolutePath();

    env.insert(QStringLiteral("ELMER_HOME"), homeDir);

    const QString pathKey = QStringLiteral("PATH");
    env.insert(pathKey, binDir + QDir::listSeparator() + env.value(pathKey));
}

bool MainWindow::resolveElmerPythonLaunch(QString &outExe, QStringList &outArgs) const
{
    outArgs.clear();

    const QString configured = m_preferences.value(QStringLiteral("ELMER_PYTHON")).toString().trimmed();
    if (!configured.isEmpty()) {
        if (!QFileInfo::exists(configured))
            return false;
        outExe = configured;
        return true;
    }

    QString py = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (!py.isEmpty()) {
        outExe = py;
        return true;
    }

    py = QStandardPaths::findExecutable(QStringLiteral("py"));
    if (!py.isEmpty()) {
        outExe = py;
        outArgs << QStringLiteral("-3");
        return true;
    }

    return false;
}

void MainWindow::patchElmerSifFilesNoMumps(const QString &runDir) const
{
    QDir dir(runDir);
    const QFileInfoList files =
        dir.entryInfoList(QStringList() << QStringLiteral("*.sif"), QDir::Files);

    static const QRegularExpression reMumps(
        R"(Linear\s+System\s+Direct\s+Method\s*=\s*zmumps)",
        QRegularExpression::CaseInsensitiveOption);

    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        QString content = QString::fromUtf8(f.readAll());
        f.close();

        if (!reMumps.match(content).hasMatch())
            continue;

        content.replace(reMumps, QStringLiteral("Linear System Direct Method = umfpack"));

        if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
            continue;

        f.write(content.toUtf8());
    }
}

MainWindow::Gds2PalaceSolverKind MainWindow::detectGds2PalaceSolverKind(
    const QString &runDir,
    const QString &simKeyLower) const
{
    if (runDir.isEmpty())
        return Gds2PalaceSolverKind::Unknown;

    if (simKeyLower == QLatin1String("elmer"))
        return Gds2PalaceSolverKind::Elmer;
    if (simKeyLower == QLatin1String("palace"))
        return Gds2PalaceSolverKind::Palace;

    const QDir dir(runDir);
    const bool hasElmerMarkers =
        QFileInfo::exists(dir.filePath(QStringLiteral("case.sif"))) ||
        QFileInfo::exists(dir.filePath(QStringLiteral("ELMERSOLVER_STARTINFO"))) ||
        QFileInfo::exists(dir.filePath(QStringLiteral("physics.sif")));
    const bool hasRunElmer = QFileInfo::exists(dir.filePath(QStringLiteral("run_elmer")));

    if (hasElmerMarkers || hasRunElmer)
        return Gds2PalaceSolverKind::Elmer;

    if (QFileInfo::exists(dir.filePath(QStringLiteral("config.json"))))
        return Gds2PalaceSolverKind::Palace;

    return Gds2PalaceSolverKind::Unknown;
}

void MainWindow::startPalaceSolverStage(PalaceRunContext &ctx)
{
    if (ctx.simKeyLower == QLatin1String("elmer") ||
        currentSimToolKey() == QLatin1String("elmer")) {
        startElmerSolverStage(ctx);
        return;
    }

    ctx.searchDirWin = resolveGds2PalaceRunDir(ctx);
    if (ctx.searchDirWin.isEmpty()) {
        failPalaceSolver("Cannot determine Palace run directory to search for config.", true);
        return;
    }

    ctx.configPathWin = findPalaceConfigJson(ctx.searchDirWin);
    if (ctx.configPathWin.isEmpty()) {
        failPalaceSolver(QString("No Palace config (*.json) found in run directory: %1")
                                 .arg(ctx.searchDirWin),
                         true);
        return;
    }

    appendToSimulationLog(QString("[Using Palace config: %1]\n").arg(QDir::toNativeSeparators(ctx.configPathWin)).toUtf8());

    if (ctx.runMode == 1) {
        if (!startPalaceLauncherStage(ctx))
            failPalaceSolver(QString(), false);
        return;
    }

    QString workDirLinux;
    QString cmd;
    QString cores;

    if (!preparePalaceSolverLaunch(ctx, workDirLinux, cmd, cores)) {
        failPalaceSolver(QString(), true);
        return;
    }

    appendToSimulationLog(
        QString("[Palace solver command] %1\n").arg(cmd).toUtf8());
    appendToSimulationLog(
        QString("[MPI cores] np = %1\n").arg(cores).toUtf8());

#ifdef Q_OS_WIN
    if (!runPalaceSolverWindows(ctx, cmd))
        failPalaceSolver(QString(), false);
#else
    if (!runPalaceSolverLinux(ctx, workDirLinux, cmd))
        failPalaceSolver(QString(), false);
#endif
}

/*!*******************************************************************************************************************
 * \brief Starts the Elmer solver stage after gds2palace preprocessing.
 *
 * Runs \c run_elmer from the simulation data directory (same as gds2palace workflow),
 * or invokes \c ELMER_SOLVER_PATH directly on Windows when configured.
 **********************************************************************************************************************/
void MainWindow::startElmerSolverStage(PalaceRunContext &ctx)
{
    ctx.searchDirWin = resolveGds2PalaceRunDir(ctx);
    if (ctx.searchDirWin.isEmpty()) {
        failPalaceSolver(QStringLiteral("Cannot determine Elmer run directory."), true);
        return;
    }

    appendToSimulationLog(
        QString("[Using Elmer run directory: %1]\n")
            .arg(QDir::toNativeSeparators(ctx.searchDirWin))
            .toUtf8());

    const QString runScriptWin = QDir(ctx.searchDirWin).filePath(QStringLiteral("run_elmer"));
    const QString elmerExeWin =
        m_preferences.value(QStringLiteral("ELMER_SOLVER_PATH")).toString().trimmed();

#ifdef Q_OS_WIN
    if (elmerExeWin.isEmpty() || !QFileInfo::exists(elmerExeWin)) {
        failPalaceSolver(QStringLiteral("ELMER_SOLVER_PATH is not configured or does not exist."), true);
        return;
    }

    patchElmerSifFilesNoMumps(ctx.searchDirWin);
    appendToSimulationLog(
        "\n[Patched Elmer .sif files: zmumps -> umfpack (Windows Elmer without MUMPS)]\n");

    appendToSimulationLog("\n[Starting ElmerSolver (native)...]\n");
    m_palacePhase = PalacePhase::PalaceSolver;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    applyElmerHomeToProcessEnv(env);
    m_simProcess->setProcessEnvironment(env);
    m_simProcess->setWorkingDirectory(ctx.searchDirWin);
    m_simProcess->start(QDir::toNativeSeparators(elmerExeWin), QStringList());

    if (!m_simProcess->waitForStarted(3000)) {
        error(QStringLiteral("Failed to start ElmerSolver."), false);
        failPalaceSolver(QString(), false);
    }
    return;
#else
    if (!QFileInfo::exists(runScriptWin)) {
        failPalaceSolver(
            QStringLiteral("No run_elmer script found in: %1").arg(ctx.searchDirWin),
            true);
        return;
    }

    appendToSimulationLog("\n[Starting Elmer via run_elmer...]\n");
    m_palacePhase = PalacePhase::PalaceSolver;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    applyElmerHomeToProcessEnv(env);
    m_simProcess->setProcessEnvironment(env);
    m_simProcess->setWorkingDirectory(ctx.searchDirWin);
    m_simProcess->start(QStringLiteral("bash"),
                        QStringList() << QStringLiteral("-lc") << QStringLiteral("./run_elmer"));

    if (!m_simProcess->waitForStarted(3000)) {
        error(QStringLiteral("Failed to start Elmer solver."), false);
        failPalaceSolver(QString(), false);
    }
#endif
}

/*!*******************************************************************************************************************
 * \brief Starts the Palace solver using an external launcher script.
 *
 * Executes the user-configured Palace launcher script instead of invoking the
 * Palace binary directly. The launcher is started in the directory containing
 * the Palace configuration file.
 *
 * This mode bypasses internal MPI command construction and delegates all
 * execution details (including core count and environment setup) to the
 * external script.
 *
 * On success, switches the internal state to \c PalacePhase::PalaceSolver.
 * On failure, reports an error and leaves the solver inactive.
 *
 * \param[in,out] ctx Palace execution context containing launcher path and
 *                    resolved configuration file.
 *
 * \return True if the launcher process was started successfully; false otherwise.
 **********************************************************************************************************************/
bool MainWindow::startPalaceLauncherStage(PalaceRunContext &ctx)
{
    appendToSimulationLog("\n[Starting Palace via external launcher script...]\n");

    m_palacePhase = PalacePhase::PalaceSolver;

    QString workDirWin = ctx.searchDirWin;
    if (workDirWin.isEmpty())
        workDirWin = QFileInfo(ctx.configPathWin).absolutePath();

#ifdef Q_OS_WIN
    const QString launcher = ctx.launcherWin.trimmed();
    QString launcherNative = launcher;
    if (launcherNative.startsWith(QLatin1Char('/')) || launcherNative.startsWith(QLatin1Char('~')))
        launcherNative = wslToWinPath(launcherNative);

    const bool launcherIsBatch =
        launcherNative.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive) ||
        launcherNative.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive);

    if (launcherIsBatch) {
        if (launcherNative.contains(QStringLiteral("palace_launcher_stub"), Qt::CaseInsensitive)) {
            appendToSimulationLog(
                "[Warning] PALACE_RUN_SCRIPT points to the EMStudio test stub. "
                "Configure a real Palace launcher (bash script in WSL) or use "
                "PALACE_RUN_MODE = Executable.\n");
        }

        m_simProcess->setWorkingDirectory(workDirWin);
        m_simProcess->start(
            QStringLiteral("cmd.exe"),
            QStringList() << QStringLiteral("/c")
                          << QDir::toNativeSeparators(launcherNative)
                          << QDir::toNativeSeparators(ctx.configPathWin));
    } else if (launcher.startsWith(QLatin1Char('/')) || launcher.startsWith(QLatin1Char('~'))) {
        const QString wslExe = wslExePath();
        if (wslExe.isEmpty()) {
            error("WSL is not available (wsl.exe not found).", false);
            return false;
        }

        const QString configLinux = toWslPath(ctx.configPathWin);
        const QString workDirLinux = toWslPath(workDirWin);

        QStringList args;
        if (!ctx.distro.trimmed().isEmpty())
            args << "-d" << ctx.distro.trimmed();

        const QString cmd =
            QString("cd %1 && %2 %3")
                .arg(shellQuoteSingle(workDirLinux),
                     shellQuoteSingle(launcher),
                     shellQuoteSingle(configLinux));

        args << "--" << "bash" << "-lc" << cmd;

        m_simProcess->start(wslExe, args);
    } else {
        m_simProcess->setWorkingDirectory(workDirWin);
        m_simProcess->start(QDir::toNativeSeparators(launcher),
                            QStringList() << QDir::toNativeSeparators(ctx.configPathWin));
    }
#else
    m_simProcess->setWorkingDirectory(workDirWin);
    m_simProcess->start(ctx.launcherWin,
                        QStringList() << ctx.configPathWin);
#endif

    if (!m_simProcess->waitForStarted(3000)) {
        error("Failed to start Palace launcher script.", false);
        return false;
    }

    return true;
}


/*!*******************************************************************************************************************
 * \brief Prepares the Palace solver launch command and execution parameters.
 *
 * Resolves the Palace configuration path for the current platform, determines
 * the working directory, detects the number of MPI cores, and constructs the
 * full solver launch command.
 *
 * This function performs only preparation and validation. It does not start
 * the solver process itself.
 *
 * On success, all output parameters are filled with valid values suitable for
 * passing to the platform-specific solver runner.
 *
 * \param[in,out] ctx           Palace execution context, updated with resolved paths.
 * \param[out]    outWorkDirLinux Working directory for the solver execution.
 * \param[out]    outCmd         Full shell command used to start the Palace solver.
 * \param[out]    outCores       Detected number of MPI cores to be used.
 *
 * \return True if the solver launch information was prepared successfully;
 *         false if a required setting is missing or preparation fails.
 **********************************************************************************************************************/
bool MainWindow::preparePalaceSolverLaunch(PalaceRunContext &ctx,
                                           QString &outWorkDirLinux,
                                           QString &outCmd,
                                           QString &outCores)
{
    if (ctx.configPathWin.isEmpty()) {
        error("Internal error: Palace config path is empty.", true);
        return false;
    }

#ifdef Q_OS_WIN
    ctx.configLinux = toWslPath(ctx.configPathWin);
#else
    ctx.configLinux = ctx.configPathWin;
#endif

    const QString configDirLinux  = QFileInfo(ctx.configLinux).path();
    const QString configBaseLinux = QFileInfo(ctx.configLinux).fileName();

    outWorkDirLinux = configDirLinux;

    const CoreCountResult cc = detectMpiCoreCount();
    outCores = cc.cores;

    appendToSimulationLog(
        QString("[MPI cores detected] np = %1 (%2)\n").arg(outCores, cc.source).toUtf8());

    const QString palaceCmd =
        QString("\"%1\" --launcher-args --oversubscribe -np %2 \"%3\"")
            .arg(ctx.palaceExeLinux, outCores, configBaseLinux);

    outCmd = QString("cd \"%1\" && %2").arg(configDirLinux, palaceCmd);
    return true;
}

#ifdef Q_OS_WIN
/*!*******************************************************************************************************************
 * \brief Starts the Palace solver stage under Windows using WSL.
 *
 * Launches the Palace solver inside the configured WSL distribution by executing
 * the prepared shell command via \c wsl.exe. The function assumes that all paths
 * and command strings are already validated and prepared.
 *
 * On success, switches the internal state to \c PalacePhase::PalaceSolver.
 * On failure, reports an error and leaves the solver inactive.
 *
 * \param ctx Prepared Palace execution context (WSL distro is taken from it).
 * \param cmd Full shell command to execute inside WSL.
 *
 * \return True if the solver process was started successfully; false otherwise.
 **********************************************************************************************************************/
bool MainWindow::runPalaceSolverWindows(const PalaceRunContext &ctx, const QString &cmd)
{
    appendToSimulationLog("\n[Starting Palace solver in WSL...]\n");

    const QString wslExe = wslExePath();
    if (wslExe.isEmpty()) {
        error("WSL is not available (wsl.exe not found).", false);
        return false;
    }

    m_palacePhase = PalacePhase::PalaceSolver;

    QStringList args;
    if (!ctx.distro.trimmed().isEmpty())
        args << "-d" << ctx.distro.trimmed();

    args << "--" << "bash" << "-lc" << cmd;

    m_simProcess->start(wslExe, args);

    if (!m_simProcess->waitForStarted(3000)) {
        error("Failed to start Palace solver under WSL.", false);
        return false;
    }

    return true;
}

#endif

/*!*******************************************************************************************************************
 * \brief Starts the Palace solver stage natively on Linux.
 *
 * Launches the Palace solver directly using the native Palace binary.
 * The working directory is set to the directory containing the Palace
 * configuration file.
 *
 * On success, switches the internal state to \c PalacePhase::PalaceSolver.
 * On failure, reports an error and leaves the solver inactive.
 *
 * \param ctx Prepared Palace execution context (used to resolve config file name).
 * \param workDirLinux Directory in which the solver should be executed.
 *
 * \return True if the solver process was started successfully; false otherwise.
 **********************************************************************************************************************/
bool MainWindow::runPalaceSolverLinux(const PalaceRunContext &ctx,
                                      const QString &workDirLinux,
                                      const QString &cmd)
{
    Q_UNUSED(ctx);

    appendToSimulationLog("\n[Starting Palace solver (native)...]\n");

    m_palacePhase = PalacePhase::PalaceSolver;

    m_simProcess->setWorkingDirectory(workDirLinux);
    m_simProcess->start(QStringLiteral("bash"), QStringList() << "-lc" << cmd);

    if (!m_simProcess->waitForStarted(3000)) {
        error("Failed to start Palace solver.", false);
        return false;
    }

    return true;
}

#ifdef Q_OS_WIN
/*!*******************************************************************************************************************
 * \brief Checks whether Windows Subsystem for Linux (WSL) is available.
 *
 * Verifies that the \c wsl executable can be found in the system PATH.
 *
 * \param[out] outError Error message describing the problem if WSL is unavailable.
 *
 * \return True if WSL is available; false otherwise.
 **********************************************************************************************************************/
bool MainWindow::ensureWslAvailable(QString &outError) const
{
    if (QStandardPaths::findExecutable("wsl").isEmpty()) {
        outError = QStringLiteral("WSL is not available on this system. Install WSL or use Palace launcher mode.");
        return false;
    }
    return true;
}

/*!*******************************************************************************************************************
 * \brief Converts a WSL-style path to a Windows path.
 *
 * Translates paths of the form "/mnt/<drive>/..." to "<Drive>:/...".
 * If the input path does not follow this pattern, it is returned unchanged.
 *
 * \param p WSL-style path string.
 *
 * \return Best-effort Windows-style path.
 **********************************************************************************************************************/
QString MainWindow::wslToWinPath(const QString &p) const
{
    if (p.startsWith("/mnt/") && p.size() > 6) {
        const QChar drive = p.at(5).toUpper();
        const QString rest = p.mid(6);
        return QString("%1:/%2").arg(drive).arg(rest);
    }
    return p;
}
#endif

#ifdef EMSTUDIO_TESTING
#ifdef Q_OS_WIN
/*!*******************************************************************************************************************
 * \brief Exposes parsePhysicalCoresFromLscpuCsv() for tests.
 *
 * \param out Raw CSV output produced by lscpu -p=CORE,SOCKET.
 * \return Number of detected physical CPU cores as string, or empty string on failure.
 **********************************************************************************************************************/
QString MainWindow::testParsePhysicalCoresFromLscpuCsv(const QString& out) const
{
    return parsePhysicalCoresFromLscpuCsv(out);
}
#endif

void MainWindow::testLogPalaceStartupInfo(const QString& modelPath,
                                          int runMode,
                                          const QString& launcherPath,
                                          const QString& pythonCmd,
                                          const QString& distro,
                                          const QString& runDirGuessWin)
{
    PalaceRunContext ctx;
    ctx.modelWin = modelPath;
    ctx.runMode = runMode;
    ctx.launcherWin = launcherPath;
    ctx.pythonCmd = pythonCmd;
    ctx.distro = distro;
    ctx.runDirGuessWin = runDirGuessWin;
    logPalaceStartupInfo(ctx);
}

bool MainWindow::testPreparePalaceSolverLaunch(const QString& configPathWin,
                                               const QString& palaceExeLinux,
                                               QString& outWorkDirLinux,
                                               QString& outCmd,
                                               QString& outCores)
{
    PalaceRunContext ctx;
    ctx.configPathWin = configPathWin;
    ctx.palaceExeLinux = palaceExeLinux;
    return preparePalaceSolverLaunch(ctx, outWorkDirLinux, outCmd, outCores);
}

void MainWindow::testFailPalaceSolver(const QString& message, bool showDialog)
{
    failPalaceSolver(message, showDialog);
}

void MainWindow::testSetPalacePhasePythonModel()
{
    m_palacePhase = PalacePhase::PythonModel;
}

void MainWindow::testSetPalacePhaseSolver()
{
    m_palacePhase = PalacePhase::PalaceSolver;
}

void MainWindow::testCallOnPalaceProcessFinished(int exitCode)
{
    onPalaceProcessFinished(exitCode);
}

void MainWindow::testAttachDummySimProcess()
{
    if (m_simProcess) {
        delete m_simProcess;
        m_simProcess = nullptr;
    }
    m_simProcess = new QProcess(this);
}

bool MainWindow::testHasSimProcess() const
{
    return m_simProcess != nullptr;
}

void MainWindow::testStartPalaceSolverStage(const QString& modelPath,
                                            const QString& topCell,
                                            const QString& detectedRunDirWin,
                                            int runMode,
                                            const QString& launcherPath)
{
    m_ui->txtRunPythonScript->setText(modelPath);
    m_ui->cbxTopCell->setCurrentText(topCell);
    m_preferences["PALACE_RUN_MODE"] = runMode;

    PalaceRunContext ctx;
    ctx.detectedRunDirWin = detectedRunDirWin;
    ctx.runMode = runMode;
    ctx.launcherWin = launcherPath;
    ctx.searchDirWin = detectedRunDirWin;
    ctx.configPathWin.clear();
    ctx.palaceExeLinux = "/tmp/fake/palace";
    ctx.distro = m_preferences.value("WSL_DISTRO").toString().trimmed();

    if (!m_simProcess)
        m_simProcess = new QProcess(this);

    startPalaceSolverStage(ctx);
}

#endif

