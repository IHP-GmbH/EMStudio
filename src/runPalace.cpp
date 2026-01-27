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
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTextCursor>

#include "extension/variantmanager.h"
#include "extension/variantfactory.h"

#include "QtPropertyBrowser/qtvariantproperty.h"
#include "QtPropertyBrowser/qttreepropertybrowser.h"

#include "mainwindow.h"
#include "preferences.h"
#include "ui_mainwindow.h"
#include "substrateview.h"
#include "pythonparser.h"

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
void MainWindow::runPalace()
{
    if (m_simProcess) {
        info("Simulation is already running.", true);
        return;
    }

    on_actionSave_triggered();

    PalaceRunContext ctx;
    QString err;
    if (!buildPalaceRunContext(ctx, err)) {
        error(err, true);
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
#ifdef Q_OS_WIN
        error("Failed to start Palace Python preprocessing under WSL.", false);
#else
        error("Failed to start Palace Python preprocessing.", false);
#endif
        m_simProcess->deleteLater();
        m_simProcess = nullptr;
        m_palacePhase = PalacePhase::None;
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
    if (ctx.simKeyLower != QLatin1String("palace")) {
        outError = QStringLiteral("Current simulation tool is not Palace.");
        return false;
    }

    ctx.modelWin = m_simSettings.value("RunPythonScript").toString().trimmed();
    if (ctx.modelWin.isEmpty() || !QFileInfo::exists(ctx.modelWin)) {
        outError = QStringLiteral("Palace Python model script is not specified or does not exist.");
        return false;
    }

    ctx.runMode = m_preferences.value("PALACE_RUN_MODE", 0).toInt();

    if (ctx.runMode == 1) {
        ctx.launcherWin = m_preferences.value("PALACE_RUN_SCRIPT").toString().trimmed();
        if (ctx.launcherWin.isEmpty() || !QFileInfo::exists(ctx.launcherWin)) {
            outError = QStringLiteral("PALACE_RUN_SCRIPT is not configured or does not exist.");
            return false;
        }

        QFileInfo fiLaunch(ctx.launcherWin);
        if (!fiLaunch.isExecutable()) {
            outError = QStringLiteral("PALACE_RUN_SCRIPT must point to an executable file.");
            return false;
        }
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
    if (ctx.palaceRoot.isEmpty()) {
        outError = QStringLiteral("PALACE_INSTALL_PATH is not configured in Preferences.");
        return false;
    }

#ifdef Q_OS_WIN
    if (!ensureWslAvailable(outError))
        return false;

    ctx.distro = m_simSettings.value("WSL_DISTRO", "Ubuntu").toString().trimmed();

    QString palaceRootLinux = ctx.palaceRoot;
    if (!palaceRootLinux.startsWith('/'))
        palaceRootLinux = toWslPath(palaceRootLinux);

    ctx.palaceExeLinux = QDir(palaceRootLinux).filePath("bin/palace");
    ctx.modelDirLinux  = toWslPath(QFileInfo(ctx.modelWin).absolutePath());
    ctx.modelLinux     = toWslPath(ctx.modelWin);
#else
    ctx.palaceExeLinux = QDir(ctx.palaceRoot).filePath("bin/palace");
    ctx.modelDirLinux  = QFileInfo(ctx.modelWin).absolutePath();
    ctx.modelLinux     = ctx.modelWin;
#endif

    ctx.pythonCmd = m_preferences.value("PALACE_WSL_PYTHON").toString().trimmed();
    if (ctx.pythonCmd.isEmpty())
        ctx.pythonCmd = QStringLiteral("python3");

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
    if (ctx.runMode == 1) {
        m_ui->editSimulationLog->insertPlainText(
            QString("Starting Palace Python preprocessing in WSL (%1) [launcher mode]...\n").arg(ctx.distro));
    } else {
        m_ui->editSimulationLog->insertPlainText(
            QString("Starting Palace Python preprocessing in WSL (%1)...\n").arg(ctx.distro));
    }
#else
    if (ctx.runMode == 1)
        m_ui->editSimulationLog->insertPlainText("Starting Palace Python preprocessing (launcher mode)...\n");
    else
        m_ui->editSimulationLog->insertPlainText("Starting Palace Python preprocessing (native)...\n");
#endif

    m_ui->editSimulationLog->insertPlainText(QString("[Using Python: %1]\n").arg(ctx.pythonCmd));
    m_ui->editSimulationLog->insertPlainText(QString("[Initial Palace run directory guess: %1]\n").arg(ctx.runDirGuessWin));

    if (ctx.runMode == 1) {
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
    QStringList args;
    args << "-d" << ctx.distro
         << "--" << "bash" << "-lc"
         << QString("cd \"%1\" && %2 \"%3\"").arg(ctx.modelDirLinux, ctx.pythonCmd, ctx.modelLinux);

    m_simProcess->start("wsl", args);
#else
    m_simProcess->setWorkingDirectory(ctx.modelDirLinux);
    m_simProcess->start(ctx.pythonCmd, QStringList() << ctx.modelLinux);
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
            appendToSimulationLog(QString("\n[Palace Python preprocessing finished with exit code %1]\n")
                                      .arg(exitCode).toUtf8());

            m_simProcess->deleteLater();
            m_simProcess = nullptr;
            m_palacePhase = PalacePhase::None;
            return;
        }

        // detect run dir from log (optional)
        const QString detectedRunDir = detectRunDirFromLog();
        if (!detectedRunDir.isEmpty())
            m_simSettings["RunDir"] = detectedRunDir;

        appendToSimulationLog("\n[Palace Python preprocessing finished successfully, searching for config...]\n");

        PalaceRunContext ctx;
        QString err;
        if (!buildPalaceRunContext(ctx, err)) {
            error(err, true);
            m_simProcess->deleteLater();
            m_simProcess = nullptr;
            m_palacePhase = PalacePhase::None;
            return;
        }

        ctx.detectedRunDirWin = detectedRunDir;
        startPalaceSolverStage(ctx);
        return;
    }

    if (m_palacePhase == PalacePhase::PalaceSolver) {
        const QString msg = (runMode == 1)
        ? QString("\n[Palace launcher finished with exit code %1]\n").arg(exitCode)
        : QString("\n[Palace solver finished with exit code %1]\n").arg(exitCode);

        appendToSimulationLog(msg.toUtf8());

        m_simProcess->deleteLater();
        m_simProcess = nullptr;
        m_palacePhase = PalacePhase::None;
    }
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
    QProcess p;

    QStringList args;
    args << "-d" << distro << "--" << "nproc";

    p.start(QStringLiteral("wsl"), args);

    if (!p.waitForFinished(3000))
        return QString();

    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return QString();

    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
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
QString MainWindow::detectMpiCoreCount(const QString &distro) const
{
#ifndef Q_OS_WIN
    Q_UNUSED(distro);
#endif

    QProcess p;

#ifdef Q_OS_WIN
    QStringList args;
    args << "-d" << distro << "--" << "nproc";
    p.start(QStringLiteral("wsl"), args);
#else
    p.start(QStringLiteral("nproc"), QStringList());
#endif

    if (!p.waitForFinished(2000))
        return QStringLiteral("1");

    const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    return out.isEmpty() ? QStringLiteral("1") : out;
}

/*!*******************************************************************************************************************
 * \brief Starts the Palace solver stage.
 *
 * Determines the Palace configuration file to use and launches the solver
 * either via an external launcher script or directly using the Palace binary
 * (natively or under WSL).
 *
 * Handles error reporting and internal phase transitions.
 *
 * \param[in,out] ctx Palace execution context, updated with resolved paths.
 **********************************************************************************************************************/
void MainWindow::startPalaceSolverStage(PalaceRunContext &ctx)
{
    const QString defRunDir = guessDefaultPalaceRunDir(m_ui->txtRunPythonScript->text(), m_ui->cbxTopCell->currentText());
    ctx.searchDirWin = chooseSearchDir(ctx.detectedRunDirWin, defRunDir);

    if (ctx.searchDirWin.isEmpty()) {
        error("Cannot determine Palace run directory to search for config.", true);
        m_simProcess->deleteLater();
        m_simProcess = nullptr;
        m_palacePhase = PalacePhase::None;
        return;
    }

    ctx.configPathWin = findPalaceConfigJson(ctx.searchDirWin);
    if (ctx.configPathWin.isEmpty()) {
        error(QString("No Palace config (*.json) found in run directory: %1").arg(ctx.searchDirWin), true);
        m_simProcess->deleteLater();
        m_simProcess = nullptr;
        m_palacePhase = PalacePhase::None;
        return;
    }

#ifdef Q_OS_WIN
    ctx.configLinux = toWslPath(ctx.configPathWin);
#else
    ctx.configLinux = ctx.configPathWin;
#endif

    appendToSimulationLog(QString("[Using Palace config: %1]\n").arg(ctx.configPathWin).toUtf8());

    // launcher mode
    if (ctx.runMode == 1) {
        appendToSimulationLog("\n[Starting Palace via external launcher script...]\n");

        m_palacePhase = PalacePhase::PalaceSolver;

        QString workDir = ctx.searchDirWin;
        if (workDir.isEmpty())
            workDir = QFileInfo(ctx.configPathWin).absolutePath();

        m_simProcess->setWorkingDirectory(workDir);
        m_simProcess->start(QDir::toNativeSeparators(ctx.launcherWin),
                            QStringList() << QDir::toNativeSeparators(ctx.configPathWin));

        if (!m_simProcess->waitForStarted(3000)) {
            error("Failed to start Palace launcher script.", false);
            m_simProcess->deleteLater();
            m_simProcess = nullptr;
            m_palacePhase = PalacePhase::None;
        }
        return;
    }

    // normal solver mode
    const QString configDirLinux  = QFileInfo(ctx.configLinux).path();
    const QString configBaseLinux = QFileInfo(ctx.configLinux).fileName();

    QString palaceRoot2 = m_preferences.value("PALACE_INSTALL_PATH").toString().trimmed();
#ifdef Q_OS_WIN
    if (!palaceRoot2.startsWith('/'))
        palaceRoot2 = toWslPath(palaceRoot2);
#endif
    const QString palaceExeLinux2 = QDir(palaceRoot2).filePath("bin/palace");

#ifdef Q_OS_WIN
    const QString distro = m_simSettings.value("WSL_DISTRO", "Ubuntu").toString().trimmed();

    const QString coreExpr = QStringLiteral("$(/usr/bin/nproc 2>/dev/null || nproc 2>/dev/null || echo 1)");
    const QString palaceCmd = QString("\"%1\" --launcher-args --oversubscribe -np %2 \"%3\"")
                                      .arg(palaceExeLinux2, coreExpr, configBaseLinux);

    const QString cmd = QString("cd \"%1\" && %2").arg(configDirLinux, palaceCmd);

    appendToSimulationLog(QString("[Palace solver command]\n  %1\n").arg(cmd).toUtf8());

#ifdef Q_OS_WIN
    const QString cores = detectMpiCoreCount(distro);
#else
    const QString cores = detectMpiCoreCount(QString());
#endif

    if (!cores.isEmpty()) {
        appendToSimulationLog(QString("[MPI cores]\n  np = %1\n").arg(cores).toUtf8());
    } else {
        appendToSimulationLog("[MPI cores]\n  np = nproc (failed to query value)\n");
    }

    QStringList argsPalace;
    argsPalace << "-d" << distro
               << "--" << "bash" << "-lc" << cmd;

    appendToSimulationLog("\n[Starting Palace solver in WSL...]\n");
    m_palacePhase = PalacePhase::PalaceSolver;
    m_simProcess->start("wsl", argsPalace);
#else
    appendToSimulationLog("\n[Starting Palace solver (native)...]\n");
    m_palacePhase = PalacePhase::PalaceSolver;
    m_simProcess->setWorkingDirectory(configDirLinux);
    m_simProcess->start(palaceExeLinux2, QStringList() << configBaseLinux);
#endif

    if (!m_simProcess->waitForStarted(3000)) {
#ifdef Q_OS_WIN
        error("Failed to start Palace solver under WSL.", false);
#else
        error("Failed to start Palace solver.", false);
#endif
        m_simProcess->deleteLater();
        m_simProcess = nullptr;
        m_palacePhase = PalacePhase::None;
    }
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

