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

#include "tst_wsl_helper.h"

#include <QtTest/QtTest>
#include <QMap>
#include <QVariant>

#include "wslHelper.h"

/*!*******************************************************************************************************************
 * \brief Verifies that plain local 8-bit/UTF-8 style WSL output is decoded correctly.
 **********************************************************************************************************************/
void WslHelperTest::decodeWslOutput_utf8_returnsText()
{
#ifdef Q_OS_WIN
    const QByteArray raw("hello\n");
    QCOMPARE(decodeWslOutput(raw), QString("hello\n"));
#else
    QSKIP("WSL helper output decoding is Windows-specific");
#endif
}

/*!*******************************************************************************************************************
 * \brief Verifies that UTF-16LE style WSL output containing NUL bytes is decoded correctly.
 **********************************************************************************************************************/
void WslHelperTest::decodeWslOutput_utf16le_returnsText()
{
#ifdef Q_OS_WIN
    const QByteArray raw = QByteArray::fromHex("680065006c006c006f000a00");
    QCOMPARE(decodeWslOutput(raw), QString("hello\n"));
#else
    QSKIP("WSL helper output decoding is Windows-specific");
#endif
}

/*!*******************************************************************************************************************
 * \brief Verifies that exportWslDistroToEnv() sets and clears EMSTUDIO_WSL_DISTRO.
 **********************************************************************************************************************/
void WslHelperTest::exportWslDistroToEnv_setsAndClearsVariable()
{
#ifdef Q_OS_WIN
    QMap<QString, QVariant> prefs;
    prefs["WSL_DISTRO"] = QString("Ubuntu");

    exportWslDistroToEnv(prefs);
    QCOMPARE(QString::fromLocal8Bit(qgetenv("EMSTUDIO_WSL_DISTRO")), QString("Ubuntu"));

    prefs["WSL_DISTRO"] = QString();
    exportWslDistroToEnv(prefs);
    QVERIFY(QString::fromLocal8Bit(qgetenv("EMSTUDIO_WSL_DISTRO")).isEmpty());
#else
    QSKIP("WSL environment export is Windows-specific");
#endif
}

/*!*******************************************************************************************************************
 * \brief Verifies that wslExePath() and isWslAvailable() are consistent.
 **********************************************************************************************************************/
void WslHelperTest::wslExePath_matchesAvailability()
{
#ifdef Q_OS_WIN
    const QString exe = wslExePath();
    QCOMPARE(!exe.isEmpty(), isWslAvailable());
#else
    QVERIFY(!isWslAvailable());
    QVERIFY(wslExePath().isEmpty());
#endif
}

/*!*******************************************************************************************************************
 * \brief Verifies that a simple WSL command can be executed if WSL is available.
 *
 * Uses the default distro by passing an empty distro name.
 **********************************************************************************************************************/
void WslHelperTest::runWslCmdCapture_echo_works_ifWslAvailable()
{
#ifdef Q_OS_WIN
    if (!isWslAvailable())
        QSKIP("WSL is not available on this system");

    const QString out =
        runWslCmdCapture(QString(), QStringList() << "bash" << "-lc" << "printf hello", 5000);

    QCOMPARE(out, QString("hello"));
#else
    QSKIP("WSL command execution is Windows-specific");
#endif
}

/*!*******************************************************************************************************************
 * \brief Verifies that distro enumeration returns at least one entry if WSL is available.
 **********************************************************************************************************************/
void WslHelperTest::listWslDistrosFromSystem_returnsSomething_ifWslAvailable()
{
#ifdef Q_OS_WIN
    if (!isWslAvailable())
        QSKIP("WSL is not available on this system");

    const QStringList distros = listWslDistrosFromSystem(5000);
    QVERIFY2(!distros.isEmpty(), "No WSL distros were returned although WSL is available");
#else
    QSKIP("WSL distro enumeration is Windows-specific");
#endif
}
