/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_tips.h"

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "mainwindow.h"

void TipsTest::resolveKeywordsPath_mapsElmerToPalace()
{
    MainWindow w;
    const QString palace = w.testResolveKeywordsPath(QStringLiteral("palace"));
    QCOMPARE(w.testResolveKeywordsPath(QStringLiteral("elmer")), palace);
    QCOMPARE(w.testResolveKeywordsPath(QStringLiteral("elmer_em")), palace);
    QCOMPARE(w.testResolveKeywordsPath(QStringLiteral("elmer_thermal")), palace);
    QVERIFY(palace.endsWith(QStringLiteral("keywords/palace.csv"))
            || palace.endsWith(QStringLiteral("keywords\\palace.csv")));

    const QString openems = w.testResolveKeywordsPath(QStringLiteral("openems"));
    QVERIFY(openems.contains(QStringLiteral("openems")));
}

void TipsTest::loadKeywordTipsCsv_parsesDelimiters()
{
    MainWindow w;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString kwDir = QDir(appDir).filePath(QStringLiteral("keywords"));
    QVERIFY(QDir().mkpath(kwDir));

    const QString csvPath = QDir(kwDir).filePath(QStringLiteral("palace.csv"));
    {
        QFile f(csvPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
        QTextStream out(&f);
        out << "freq_start;Start frequency\n"
            << "freq_stop;Stop frequency\n"
            << "orphan_keyword\n"
            << "\n"
            << "freq_start;duplicate ignored\n";
    }

    const QMap<QString, QString> tips = w.testLoadKeywordTipsCsv(QStringLiteral("palace"));
    QCOMPARE(tips.value(QStringLiteral("freq_start")), QStringLiteral("Start frequency"));
    QCOMPARE(tips.value(QStringLiteral("freq_stop")), QStringLiteral("Stop frequency"));
    QVERIFY(tips.contains(QStringLiteral("orphan_keyword")));

    // Also cover comma-delimited openems file
    const QString oePath = QDir(kwDir).filePath(QStringLiteral("openems.csv"));
    {
        QFile f(oePath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
        QTextStream(&f) << "unit,Length unit\nmax_freq,Max freq\n";
    }
    const QMap<QString, QString> oe = w.testLoadKeywordTipsCsv(QStringLiteral("openems"));
    QCOMPARE(oe.value(QStringLiteral("unit")), QStringLiteral("Length unit"));

    // Tab-delimited tips
    {
        QFile f(QDir(kwDir).filePath(QStringLiteral("palace.csv")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
        QTextStream(&f) << "alpha\tAlpha tip\nbeta\tBeta tip\n";
    }
    const QMap<QString, QString> tabTips = w.testLoadKeywordTipsCsv(QStringLiteral("palace"));
    QCOMPARE(tabTips.value(QStringLiteral("alpha")), QStringLiteral("Alpha tip"));

    w.testRefreshKeywordTipsForCurrentTool();
}

void TipsTest::mergeTipsPreferModel_keepsModelOverrides()
{
    MainWindow w;
    QMap<QString, QString> model;
    model.insert(QStringLiteral("a"), QStringLiteral("from-model"));
    QMap<QString, QString> fallback;
    fallback.insert(QStringLiteral("a"), QStringLiteral("from-csv"));
    fallback.insert(QStringLiteral("b"), QStringLiteral("only-csv"));

    const QMap<QString, QString> merged = w.testMergeTipsPreferModel(model, fallback);
    QCOMPARE(merged.value(QStringLiteral("a")), QStringLiteral("from-model"));
    QCOMPARE(merged.value(QStringLiteral("b")), QStringLiteral("only-csv"));
}
