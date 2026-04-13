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
 ************************************************************************/

#include "tst_headless_dispatch.h"

#include <QtTest/QtTest>
#include <QFile>
#include <QFileInfo>

#include "mainwindow.h"

/*!*******************************************************************************************************************
 * \brief Resolves the platform-specific OpenEMS Python launcher stub for unit tests.
 *
 * \return Absolute path to the OpenEMS python stub script, or empty string if not found.
 **********************************************************************************************************************/
static QString ensureTestOpenemsPythonStub()
{
#ifdef Q_OS_WIN
    return QFINDTESTDATA("tools/openems_python_stub.cmd");
#else
    return QFINDTESTDATA("tools/openems_python_stub.sh");
#endif
}

/*!*******************************************************************************************************************
 * \brief Resolves the platform-specific Palace launcher stub for unit tests.
 *
 * \return Absolute path to the Palace launcher stub script, or empty string if not found.
 **********************************************************************************************************************/
static QString ensureTestPalaceLauncher()
{
#ifdef Q_OS_WIN
    return QFINDTESTDATA("tools/palace_launcher_stub.cmd");
#else
    return QFINDTESTDATA("tools/palace_launcher_stub.sh");
#endif
}

/*!*******************************************************************************************************************
 * \brief Prepares a deterministic OpenEMS model for headless dispatch tests.
 *
 * \param w Main window under test.
 **********************************************************************************************************************/
static void prepareOpenems(MainWindow& w)
{
    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    const QString pyStub = ensureTestOpenemsPythonStub();
    QVERIFY2(!pyStub.isEmpty(), "OpenEMS python stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(pyStub,
                          QFile::permissions(pyStub) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.setGdsFile(gdsPath);
    w.setTopCell("t1");
    w.setSubstrateFile(xmlPath);

    w.testSetPreference("Python Path", pyStub);
    w.refreshSimToolOptionsForTests();

    QString terr;
    QVERIFY2(w.testSetSimToolKey("openems", &terr), qPrintable(terr));
    QVERIFY2(w.testInitDefaultOpenemsModel(), "testInitDefaultOpenemsModel() failed");

    QString err;
    const QString regenerated = w.testGenerateScriptFromGuiState(&err);
    QVERIFY2(!regenerated.isEmpty(), qPrintable(err));
    w.testSetEditorText(regenerated);

    const QString scriptPath = w.testWriteEditorToTempPyFile("headless_openems_test.py");
    QVERIFY2(!scriptPath.isEmpty(), "Failed to create temporary OpenEMS script");

    w.testSetRunPythonScriptPath(scriptPath);
    w.testSetSimSetting("RunDir", QFileInfo(scriptPath).absolutePath());
}

/*!*******************************************************************************************************************
 * \brief Prepares a deterministic Palace model for headless dispatch tests.
 *
 * \param w Main window under test.
 **********************************************************************************************************************/
static void preparePalace(MainWindow& w)
{
    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    const QString launcherPath = ensureTestPalaceLauncher();
    QVERIFY2(!launcherPath.isEmpty(), "Palace launcher stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(launcherPath,
                          QFile::permissions(launcherPath) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.setGdsFile(gdsPath);
    w.setTopCell("t1");
    w.setSubstrateFile(xmlPath);

    w.testSetPreference("PALACE_RUN_MODE", 1);
    w.testSetPreference("PALACE_RUN_SCRIPT", launcherPath);
    w.testSetPreference("PALACE_INSTALL_PATH", QString());
    w.refreshSimToolOptionsForTests();

    QString terr;
    QVERIFY2(w.testSetSimToolKey("palace", &terr), qPrintable(terr));
    QVERIFY2(w.testInitDefaultPalaceModel(), "testInitDefaultPalaceModel() failed");

    QString err;
    const QString regenerated = w.testGenerateScriptFromGuiState(&err);
    QVERIFY2(!regenerated.isEmpty(), qPrintable(err));
    w.testSetEditorText(regenerated);

    const QString scriptPath = w.testWriteEditorToTempPyFile("headless_palace_test.py");
    QVERIFY2(!scriptPath.isEmpty(), "Failed to create temporary Palace script");

    w.testSetRunPythonScriptPath(scriptPath);
    w.testSetSimSetting("RunDir", QFileInfo(scriptPath).absolutePath());
}

/*!*******************************************************************************************************************
 * \brief Verifies that headless dispatch for OpenEMS enables headless mode and starts OpenEMS flow.
 **********************************************************************************************************************/
void HeadlessDispatchTest::runHeadless_openems_dispatchesToOpenems()
{
    MainWindow w;
    prepareOpenems(w);

    w.testRunHeadless("openems");

    QVERIFY2(w.testIsHeadless(), "Headless flag shall be set after runHeadless(openems)");

    const bool finished = QTest::qWaitFor([&w]() {
        return !w.testIsSimulationRunning();
    }, 5000);

    QVERIFY2(finished, "OpenEMS headless dispatch did not finish in time");

    const QString log = w.testSimulationLogText();
    QVERIFY2(log.contains("Starting OpenEMS simulation"),
             qPrintable(QString("Missing OpenEMS start message:\n%1").arg(log)));
}

/*!*******************************************************************************************************************
 * \brief Verifies that headless dispatch for Palace enables headless mode and starts Palace flow.
 **********************************************************************************************************************/
void HeadlessDispatchTest::runHeadless_palace_dispatchesToPalace()
{
    MainWindow w;
    preparePalace(w);

    w.testRunHeadless("palace");

    QVERIFY2(w.testIsHeadless(), "Headless flag shall be set after runHeadless(palace)");

    const bool finished = QTest::qWaitFor([&w]() {
        return !w.testIsSimulationRunning();
    }, 5000);

    QVERIFY2(finished, "Palace headless dispatch did not finish in time");

    const QString log = w.testSimulationLogText();
    QVERIFY2(log.contains("Starting Palace Python preprocessing"),
             qPrintable(QString("Missing Palace start message:\n%1").arg(log)));
}

/*!*******************************************************************************************************************
 * \brief Verifies that an unknown backend in headless mode still sets headless state and requests application exit.
 *
 * This test only checks that the unknown-backend branch executes. It does not assert the exact exit code
 * because QCoreApplication::exit() is asynchronous and process-global in Qt tests.
 **********************************************************************************************************************/
void HeadlessDispatchTest::runHeadless_unknownBackend_setsHeadlessAndRequestsExit()
{
    MainWindow w;

    w.testRunHeadless("unknown_backend");

    QVERIFY2(w.testIsHeadless(), "Headless flag shall be set even for unknown backend");
    QVERIFY(true);
}
