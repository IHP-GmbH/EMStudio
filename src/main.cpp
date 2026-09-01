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
#include <QDir>
#include <QPixmap>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QSplashScreen>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QFont>
#include <QFontInfo>
#include <QScreen>
#include <cstdint>

#if defined(Q_OS_WIN)
#  include <windows.h>
#elif defined(Q_OS_LINUX)
#  include <unistd.h>
#endif

namespace {

/*! Path of the running executable's own directory, determined without
 *  relying on QCoreApplication (which isn't constructed yet). Needed because
 *  QLibraryInfo only resolves qt.conf's paths relative to the executable
 *  once an application instance exists - too late to fix up plugin loading,
 *  which happens inside the QApplication constructor itself. */
QString executableDirectory()
{
#if defined(Q_OS_WIN)
    wchar_t buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
        return QString();
    return QFileInfo(QString::fromWCharArray(buffer, len)).absolutePath();
#elif defined(Q_OS_LINUX)
    char buffer[4096];
    const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0)
        return QString();
    return QFileInfo(QString::fromLocal8Bit(buffer, static_cast<int>(len))).absolutePath();
#else
    return QString();
#endif
}

#if defined(Q_OS_WIN)
/*! Prefer Per-Monitor V2 before QApplication so Qt High-DPI and the native
 *  menu bar agree on the Windows display scale (e.g. 150% on 4K). */
void enableWindowsPerMonitorDpiV2()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32)
        return;
    // MinGW headers may lack DPI_AWARENESS_CONTEXT; use void* and ordinal -4 (V2).
    typedef BOOL (WINAPI *SetCtxFn)(void *);
    SetCtxFn setCtx = reinterpret_cast<SetCtxFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setCtx)
        setCtx(reinterpret_cast<void *>(static_cast<intptr_t>(-4)));
}
#endif

/*! Normalize the application font to a point size (no locked pixelSize).
 *  On Windows, a pixel-sized system font + incomplete HiDPI awareness makes
 *  Qt widgets stay ~100% while the native menu bar follows Windows scaling. */
void normalizeApplicationFontForHighDpi()
{
    const QFont appFont = QApplication::font();
    const QFontInfo fi(appFont);
    QFont normalized(fi.family().isEmpty() ? appFont.family() : fi.family());
    normalized.setStyleHint(QFont::SansSerif);
    const qreal pt = fi.pointSizeF();
    if (pt > 0.0)
        normalized.setPointSizeF(pt);
    else
        normalized.setPointSize(9);
    QApplication::setFont(normalized);
}

/*! qt.conf ships a relative "Plugins = plugins" entry for bundled/deployed
 *  builds (windeployqt, the CI Linux bundle), where that folder is placed
 *  next to the executable. A plain local build has no such folder, so Qt
 *  would resolve the platform plugin search path to a location that doesn't
 *  exist and refuse to start ("Could not find the Qt platform plugin").
 *  Detect that case and fall back to the plugin directory of the Qt this
 *  binary was built against. Must run before QApplication is constructed,
 *  since platform plugin loading happens inside its constructor - too late
 *  to query QLibraryInfo, which only resolves qt.conf relative to the
 *  executable once an application instance exists. */
void ensureQtPluginPathFallback()
{
    if (qEnvironmentVariableIsSet("QT_PLUGIN_PATH")
        || qEnvironmentVariableIsSet("QT_QPA_PLATFORM_PLUGIN_PATH")) {
        return; // already configured explicitly, don't override
    }

    const QString exeDir = executableDirectory();
    if (exeDir.isEmpty())
        return; // couldn't determine reliably, leave Qt's own resolution alone

    // Mirrors qt.conf's "[Paths] Plugins = plugins" - keep in sync if that changes.
    if (QDir(exeDir + QLatin1String("/plugins")).exists())
        return; // bundled/deployed layout is present, nothing to do

#ifdef EMSTUDIO_BUILD_QT_PLUGINS_PATH
    const QString fallback = QStringLiteral(EMSTUDIO_BUILD_QT_PLUGINS_PATH);
    if (QDir(fallback).exists()) {
        // QCoreApplication::addLibraryPath() is too late here: once qt.conf
        // declares a Plugins entry, Qt's platform-plugin loader (which runs
        // inside the QApplication constructor) resolves exclusively through
        // qt.conf and ignores paths added beforehand. Environment variables
        // are read directly by that loader, so they reliably take effect.
        qputenv("QT_PLUGIN_PATH", fallback.toUtf8());
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", (fallback + QLatin1String("/platforms")).toUtf8());
    }
#endif
}

} // namespace

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
#if defined(Q_OS_WIN)
    enableWindowsPerMonitorDpiV2();
#endif

    // Qt5: opt into automatic High-DPI scaling (Qt6 enables this by default).
    // Must be set before QApplication. PassThrough keeps fractional scales
    // such as Windows 125%/150% instead of rounding to 1x/2x.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    if (!qEnvironmentVariableIsSet("QT_ENABLE_HIGHDPI_SCALING"))
        qputenv("QT_ENABLE_HIGHDPI_SCALING", "1");
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

    ensureQtPluginPathFallback();

    QApplication a(argc, argv);
    normalizeApplicationFontForHighDpi();

    QCoreApplication::setApplicationName("EMStudio");
    QCoreApplication::setApplicationVersion(QStringLiteral(EMSTUDIO_VERSION_STR));

#ifndef QT_NO_DEBUG_OUTPUT
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        qDebug() << "HiDPI: logicalDpi" << screen->logicalDotsPerInch()
                 << "devicePixelRatio" << screen->devicePixelRatio()
                 << "appFontPt" << QApplication::font().pointSizeF();
    }
#endif

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
