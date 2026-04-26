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

#include "tst_about_dialog.h"

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QLabel>

#include "about.h"

/*!*******************************************************************************************************************
 * \brief Returns a QLabel child of the given widget by object name.
 *
 * \param parent Parent widget.
 * \param name Object name of the label.
 *
 * \return Pointer to QLabel or nullptr if not found.
 **********************************************************************************************************************/
static QLabel* findLabel(QWidget* parent, const QString& name)
{
    return parent ? parent->findChild<QLabel*>(name) : nullptr;
}

/*!*******************************************************************************************************************
 * \brief Verifies whether a QLabel contains a valid pixmap without deprecated Qt API warnings.
 *
 * This helper avoids direct QLabel::pixmap() pointer overload usage in Qt5.15+,
 * while remaining compatible with older Qt versions.
 *
 * \param label QLabel to inspect.
 *
 * \return true if the label contains a valid non-empty pixmap, otherwise false.
 **********************************************************************************************************************/
static bool pixmapIsValid(const QLabel* label)
{
    if (!label)
        return false;

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    const QPixmap pix = label->pixmap(Qt::ReturnByValue);
    return !pix.isNull();
#else
    const QPixmap* pix = label->pixmap();
    return pix && !pix->isNull();
#endif
}

/*!*******************************************************************************************************************
 * \brief Verifies that AboutDialog initializes version, Qt version, build text and logo.
 *
 * The test checks that:
 *  - lblVersion shows the application version,
 *  - lblQt shows the current Qt runtime version,
 *  - lblBuild is populated and contains build type information,
 *  - lblLogo contains a valid pixmap.
 *
 * The dialog is not shown, which keeps the test stable in headless CI environments.
 **********************************************************************************************************************/
void AboutDialogTest::initUi_setsExpectedLabels()
{
    QCoreApplication::setApplicationVersion(QStringLiteral(EMSTUDIO_VERSION_STR));

    AboutDialog dlg;

    QLabel* lblVersion = findLabel(&dlg, "lblVersion");
    QLabel* lblQt      = findLabel(&dlg, "lblQt");
    QLabel* lblBuild   = findLabel(&dlg, "lblBuild");
    QLabel* lblLogo    = findLabel(&dlg, "lblLogo");

    QVERIFY2(lblVersion, "lblVersion not found");
    QVERIFY2(lblQt,      "lblQt not found");
    QVERIFY2(lblBuild,   "lblBuild not found");
    QVERIFY2(lblLogo,    "lblLogo not found");

    QCOMPARE(lblVersion->text(), QStringLiteral(EMSTUDIO_VERSION_STR));
    QCOMPARE(lblQt->text(), QString::fromLatin1(qVersion()));

#ifdef QT_DEBUG
    const QString expectedBuildType = QStringLiteral("Debug");
#else
    const QString expectedBuildType = QStringLiteral("Release");
#endif

    QVERIFY2(!lblBuild->text().isEmpty(), "lblBuild shall not be empty");
    QVERIFY2(lblBuild->text().contains(expectedBuildType),
             qPrintable(QString("lblBuild does not contain build type '%1': %2")
                            .arg(expectedBuildType, lblBuild->text())));
    QVERIFY2(lblBuild->text().contains(" | "),
             qPrintable(QString("lblBuild does not contain expected separator: %1")
                            .arg(lblBuild->text())));

    const QStringList buildParts = lblBuild->text().split(" | ");
    QVERIFY2(buildParts.size() == 2,
             qPrintable(QString("lblBuild has unexpected format: %1").arg(lblBuild->text())));
    QVERIFY2(!buildParts.at(1).trimmed().isEmpty(),
             qPrintable(QString("lblBuild date part is empty: %1").arg(lblBuild->text())));

    QVERIFY2(pixmapIsValid(lblLogo),
             "Logo label has no valid pixmap");
}
