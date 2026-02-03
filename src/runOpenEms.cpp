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
 * \brief Runs OpenEMS: writes the Python script, configures env, and launches the Python process.
 **********************************************************************************************************************/
void MainWindow::runOpenEMS()
{
    if (m_simProcess && m_simProcess->state() == QProcess::Running) {
        info("Simulation is already running.", true);
        return;
    }

    on_actionSave_triggered();

    QString pythonPath = m_preferences.value("Python Path").toString().trimmed();
    if (pythonPath.isEmpty()) {
        pythonPath = "python";
    } else if (!QFileInfo::exists(pythonPath)) {
        error(QString("Python executable not found: %1").arg(pythonPath), true);
        return;
    }

    const QString scriptPath = m_simSettings.value("RunPythonScript").toString().trimmed();
    if (scriptPath.isEmpty() || !QFileInfo::exists(scriptPath)) {
        error(QString("Python file '%1' does not exist.").arg(scriptPath), true);
        return;
    }

    QString runDir = m_simSettings.value("RunDir").toString();
    if (runDir.isEmpty() || !QDir(runDir).exists()) {
        runDir = QFileInfo(scriptPath).absolutePath();
    }

    m_simProcess = new QProcess(this);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = m_preferences.constBegin(); it != m_preferences.constEnd(); ++it) {
        const QString key = it.key();
        const QString value = it.value().toString();

        if (key == "Python Path") {
            QFileInfo pythonFile(value);
            QString pythonDir = pythonFile.absolutePath();
            QString currentPath = env.value("PATH");

            if (!currentPath.contains(pythonDir, Qt::CaseInsensitive)) {
                env.insert("PATH", pythonDir + QDir::listSeparator() + currentPath);
            }
        } else if (!env.contains(key)) {
            env.insert(key, value);
        }
    }

    if (!env.contains("OPENEMS_INSTALL_PATH") && m_preferences.contains("OPENEMS_INSTALL_PATH")) {
        env.insert("OPENEMS_INSTALL_PATH", m_preferences.value("OPENEMS_INSTALL_PATH").toString());
    }

    const QString origScriptPath = QFileInfo(scriptPath).absolutePath();
#ifdef Q_OS_WIN
    const QString pathSep = ";";
#else
    const QString pathSep = ":";
#endif
    if (env.contains("PYTHONPATH")) {
        env.insert("PYTHONPATH", origScriptPath + pathSep + env.value("PYTHONPATH"));
    } else {
        env.insert("PYTHONPATH", origScriptPath);
    }

    env.remove("PYTHONHOME");

    m_simProcess->setProcessEnvironment(env);
    m_simProcess->setWorkingDirectory(runDir);

    auto appendLog = [this](const QByteArray& data)
    {
        if (data.isEmpty())
            return;

        QSignalBlocker blocker(m_ui->editSimulationLog);
        m_ui->editSimulationLog->moveCursor(QTextCursor::End);
        m_ui->editSimulationLog->insertPlainText(QString::fromUtf8(data));
        m_ui->editSimulationLog->moveCursor(QTextCursor::End);
    };


    connect(m_simProcess, &QProcess::readyReadStandardOutput, this, [=]() {
        appendLog(m_simProcess->readAllStandardOutput());
    });
    connect(m_simProcess, &QProcess::readyReadStandardError, this, [=]() {
        appendLog(m_simProcess->readAllStandardError());
    });
    connect(m_simProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [=](int exitCode, QProcess::ExitStatus) {
                const QString msg = QString("\n[Simulation finished with exit code %1]\n").arg(exitCode);
                QSignalBlocker blocker(m_ui->editSimulationLog);
                m_ui->editSimulationLog->moveCursor(QTextCursor::End);
                m_ui->editSimulationLog->insertPlainText(msg);
                m_ui->editSimulationLog->moveCursor(QTextCursor::End);
                m_simProcess->deleteLater();
                m_simProcess = nullptr;
            });

    m_ui->editSimulationLog->clear();
    m_ui->editSimulationLog->insertPlainText("Starting OpenEMS simulation...\n");

    m_ui->editSimulationLog->insertPlainText(QString("[RUN] %1 %2\n")
                                                 .arg(QDir::toNativeSeparators(pythonPath),
                                                  QDir::toNativeSeparators(scriptPath)));

    m_simProcess->start(pythonPath, QStringList() << scriptPath);
    if (!m_simProcess->waitForStarted(3000)) {
        error("Failed to start simulation process.", false);
        m_simProcess->deleteLater();
        m_simProcess = nullptr;
    }
}
