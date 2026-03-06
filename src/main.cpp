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


/*!*******************************************************************************************************************
 * \mainpage EMStudio – Electromagnetic Simulation Visualizer
 *
 * \section intro_sec Introduction
 *
 * EMStudio is a Qt-based desktop application for visualizing and configuring electromagnetic simulations.
 * It allows users to load substrate definitions, GDS files, and simulation parameters from JSON configuration files.
 * The application provides an interactive interface to:
 *   - View and navigate through 3D-like layer stackups
 *   - Modify simulation settings using a property browser
 *   - Manage ports, substrates, and run configurations
 *   - Integrate and launch simulation scripts
 *
 * \section features_sec Features
 *   - Zoomable and pannable 2.5D visualization of dielectric/metal/via layers
 *   - Real-time simulation parameter editing
 *   - GDS and substrate file handling
 *   - Command-line argument support
 *   - Configurable preferences panel
 *
 * \section klayout_sec KLayout Integration
 *
 * EMStudio supports integration with KLayout for layout-based simulation flows. A helper script (`klEmsDriver.py`)
 * is provided to bridge EMStudio configuration with layout inspection or export tools in KLayout.
 *
 * Example usage:
 * \code
 * klayout_app.exe -e -rm "<PathToEMStudio>/klEmsDriver.py"
 * \endcode
 *
 * Here, `<PathToEMStudio>` should be replaced with the full path to the EMStudio installation or source directory
 * where the `klEmsDriver.py` script is located.
 *
 * When launched from KLayout, the script checks if a JSON run file named `<TopCell>.json` exists in the same folder
 * as the GDS file. If such a file is found, it will be passed as the argument to EMStudio instead of using
 * `-gdsfile` and `-topcell` arguments. This allows preconfigured simulation setups to be launched directly.
 *
 * \section usage_sec Command-Line Usage
 * \code
 * EMStudio [options] [run_file.json]
 *
 * Options:
 *   -h, --help           Show help message
 *   -gdsfile <path>      Specify path to GDS file
 *   -topcell <name>      Specify top-level cell name
 *
 * Arguments:
 *   run_file.json        Optional simulation configuration file
 * \endcode
 **********************************************************************************************************************/

#include "mainwindow.h"

#include <QTimer>
#include <QDebug>
#include <QPixmap>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QSplashScreen>
#include <QCoreApplication>

/*!*******************************************************************************************************************
 * \brief Prints usage information for the EMStudio application.
 *
 * This function outputs the command-line usage and available options for the application.
 **********************************************************************************************************************/
void printHelp()
{
    qDebug() << "Usage: EMStudio [options] [model.py]";
    qDebug() << "\nOptions:";
    qDebug() << "  -h, --help            Show this help message";
    qDebug() << "  -gdsfile <path>       Specify path to GDS file";
    qDebug() << "  -topcell <name>       Specify name of the top cell in the GDS file";
    qDebug() << "  -run                  Run simulation headless (no GUI)";
    qDebug() << "  -palace               Select Palace backend (with -run)";
    qDebug() << "  -openems              Select OpenEMS backend (with -run)";
    qDebug() << "\nArguments:";
    qDebug() << "  model.py              Python model to load (optional, but usually needed)";
}

/*!*******************************************************************************************************************
 * \brief Main entry point for the EMStudio application.
 *
 * Initializes the Qt application, shows the splash screen, handles command-line arguments,
 * optionally loads a simulation JSON file, and starts the event loop.
 *
 * \param argc Argument count from the command line.
 * \param argv Argument vector from the command line.
 * \return The exit status of the application.
 **********************************************************************************************************************/
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QCoreApplication::setApplicationName("EMStudio");
    QCoreApplication::setApplicationVersion(QStringLiteral(EMSTUDIO_VERSION_STR));

    QString gdsFile;
    QString topCell;
    QString pythonFile;

    bool headlessRun = false;
    QString runTool;

    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args[i];

        if (arg == "-h" || arg == "--help") {
            printHelp();
            return 0;
        } else if (arg == "-gdsfile" && i + 1 < args.size()) {
            gdsFile = args[++i];
        } else if (arg == "-topcell" && i + 1 < args.size()) {
            topCell = args[++i];
        } else if (arg == "-run") {
            headlessRun = true;
        } else if (arg == "-palace") {
            runTool = "palace";
        } else if (arg == "-openems") {
            runTool = "openems";
        } else if (arg.endsWith(".py", Qt::CaseInsensitive)) {
            pythonFile = arg;
        } else {
            qWarning() << "Unknown or malformed argument:" << arg;
            printHelp();
            return 1;
        }
    }

    if (headlessRun) {
        if (runTool.isEmpty()) {
            qWarning() << "Headless mode requires backend: use -palace or -openems together with -run";
            printHelp();
            return 1;
        }
    }

    QScopedPointer<QSplashScreen> splash;
    if (!headlessRun) {
        QPixmap pixmap(":/logo");
        QPixmap scaledPixmap = pixmap.scaled(
            pixmap.width() / 3,
            pixmap.height() / 3,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
            );
        splash.reset(new QSplashScreen(scaledPixmap));
        splash->show();
        a.processEvents();
    }

    MainWindow w;

    if (!pythonFile.isEmpty() && QFileInfo::exists(pythonFile)) {
        w.loadPythonModel(pythonFile);
    }

    if (!gdsFile.isEmpty())
        w.setGdsFile(gdsFile);

    if (!topCell.isEmpty())
        w.setTopCell(topCell);

    if (headlessRun) {
        QTimer::singleShot(0, &w, [&w, runTool]() {
            w.runHeadless(runTool);
        });
        return a.exec();
    }

    QTimer::singleShot(1000, [&]() {
        if (splash) splash->finish(&w);
        w.tryAutoLoadRecentPythonForTopCell();
        w.show();
    });

    return a.exec();
}
