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
#include <QTextStream>
#include <QMessageBox>

#include "mainwindow.h"
#include "ui_mainwindow.h"

/*!*******************************************************************************************************************
 * \brief Runs OpenEMS: writes the Python script, configures env, and launches the Python process.
 **********************************************************************************************************************/
void MainWindow::runOpenEMS(bool interactive)
{
    if (m_simProcess && m_simProcess->state() == QProcess::Running) {
        info("Simulation is already running.", true);
        return;
    }

    // Headless flag (same idea as Palace)
    m_headless = !interactive;

    if (interactive) {
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

    QString pythonPath = m_preferences.value("Python Path").toString().trimmed();
    if (pythonPath.isEmpty()) {
        pythonPath = QStringLiteral("python");
    } else if (!QFileInfo::exists(pythonPath)) {
        error(QString("Python executable not found: %1").arg(pythonPath), true);
        if (!interactive) QCoreApplication::exit(1);
        return;
    }

    const QString scriptPath = m_simSettings.value("RunPythonScript").toString().trimmed();
    if (scriptPath.isEmpty() || !QFileInfo::exists(scriptPath)) {
        error(QString("Python file '%1' does not exist.").arg(scriptPath), true);
        if (!interactive) QCoreApplication::exit(1);
        return;
    }

    QString runDir = m_simSettings.value("RunDir").toString().trimmed();
    if (runDir.isEmpty() || !QDir(runDir).exists()) {
        runDir = QFileInfo(scriptPath).absolutePath();
    }

    m_simProcess = new QProcess(this);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = m_preferences.constBegin(); it != m_preferences.constEnd(); ++it) {
        const QString key = it.key();
        const QString value = it.value().toString();

        if (key == QLatin1String("Python Path")) {
            QFileInfo pythonFile(value);
            const QString pythonDir = pythonFile.absolutePath();
            const QString currentPath = env.value(QStringLiteral("PATH"));

            if (!currentPath.contains(pythonDir, Qt::CaseInsensitive)) {
                env.insert(QStringLiteral("PATH"), pythonDir + QDir::listSeparator() + currentPath);
            }
        } else if (!env.contains(key)) {
            env.insert(key, value);
        }
    }

    const QString origScriptPath = QFileInfo(scriptPath).absolutePath();
#ifdef Q_OS_WIN
    const QString pathSep = ";";
#else
    const QString pathSep = ":";
#endif
    if (env.contains(QStringLiteral("PYTHONPATH"))) {
        env.insert(QStringLiteral("PYTHONPATH"),
                   origScriptPath + pathSep + env.value(QStringLiteral("PYTHONPATH")));
    } else {
        env.insert(QStringLiteral("PYTHONPATH"), origScriptPath);
    }

    env.remove(QStringLiteral("PYTHONHOME"));

    m_simProcess->setProcessEnvironment(env);
    m_simProcess->setWorkingDirectory(runDir);

    auto appendLog = [this](const QByteArray& data)
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
    };

    connect(m_simProcess, &QProcess::readyReadStandardOutput, this, [this, appendLog]() {
        if (!m_simProcess) return;
        appendLog(m_simProcess->readAllStandardOutput());
    });
    connect(m_simProcess, &QProcess::readyReadStandardError, this, [this, appendLog]() {
        if (!m_simProcess) return;
        appendLog(m_simProcess->readAllStandardError());
    });

    connect(m_simProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus)
            {
                const QString msg =
                    QString("\n[Simulation finished with exit code %1]\n").arg(exitCode);

                {
                    QSignalBlocker blocker(m_ui->editSimulationLog);
                    m_ui->editSimulationLog->moveCursor(QTextCursor::End);
                    m_ui->editSimulationLog->insertPlainText(msg);
                    m_ui->editSimulationLog->moveCursor(QTextCursor::End);
                }

                if (m_simProcess) {
                    m_simProcess->deleteLater();
                    m_simProcess = nullptr;
                }

                if (m_headless)
                    QCoreApplication::exit(exitCode);
            });

    m_ui->editSimulationLog->clear();
    m_ui->editSimulationLog->insertPlainText("Starting OpenEMS simulation...\n");
    m_ui->editSimulationLog->insertPlainText(
        QString("[RUN] %1 %2\n")
            .arg(QDir::toNativeSeparators(pythonPath),
                 QDir::toNativeSeparators(scriptPath)));

    m_simProcess->start(pythonPath, QStringList() << scriptPath);

    if (!m_simProcess->waitForStarted(3000)) {
        error("Failed to start simulation process.", false);

        if (m_simProcess) {
            m_simProcess->deleteLater();
            m_simProcess = nullptr;
        }

        if (!interactive)
            QCoreApplication::exit(3);
    }
}
