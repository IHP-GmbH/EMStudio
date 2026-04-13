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

#include "tst_find_dialog.h"

#include <QtTest/QtTest>

#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>

#include "finddialog.h"

/*!*******************************************************************************************************************
 * \brief Verifies that FindDialog buttons emit the expected signals with current search settings.
 *
 * The test fills the search text field, enables all search options, presses all action buttons,
 * and verifies that the corresponding dialog signals are emitted with correct payload.
 **********************************************************************************************************************/
void FindDialogTest::buttons_emit_expected_signals()
{
    FindDialog dlg;

    auto* edit = dlg.findChild<QLineEdit*>();
    QVERIFY2(edit, "Search line edit not found");

    const auto checkBoxes = dlg.findChildren<QCheckBox*>();
    QVERIFY2(checkBoxes.size() == 3, "Expected 3 checkboxes");

    QCheckBox* caseBox  = nullptr;
    QCheckBox* wholeBox = nullptr;
    QCheckBox* regexBox = nullptr;

    for (QCheckBox* cb : checkBoxes) {
        if (cb->text().contains("case", Qt::CaseInsensitive))
            caseBox = cb;
        else if (cb->text().contains("Whole", Qt::CaseInsensitive))
            wholeBox = cb;
        else if (cb->text().contains("Regex", Qt::CaseInsensitive))
            regexBox = cb;
    }

    QVERIFY2(caseBox, "Match case checkbox not found");
    QVERIFY2(wholeBox, "Whole words checkbox not found");
    QVERIFY2(regexBox, "Regex checkbox not found");

    edit->setText("abc");
    caseBox->setChecked(true);
    wholeBox->setChecked(true);
    regexBox->setChecked(true);

    QSignalSpy spyNext(&dlg, SIGNAL(sigFindNext(QString,bool,bool,bool)));
    QSignalSpy spyPrev(&dlg, SIGNAL(sigFindPrev(QString,bool,bool,bool)));
    QSignalSpy spyHighlight(&dlg, SIGNAL(sigHighlightAll(QString,bool,bool,bool)));
    QSignalSpy spyClear(&dlg, SIGNAL(sigClearHighlights()));

    const auto buttons = dlg.findChildren<QPushButton*>();
    QVERIFY2(!buttons.isEmpty(), "No buttons found in FindDialog");

    QPushButton* btnNext = nullptr;
    QPushButton* btnPrev = nullptr;
    QPushButton* btnHighlight = nullptr;
    QPushButton* btnClear = nullptr;

    for (QPushButton* btn : buttons) {
        const QString text = btn->text().trimmed();

        if (text == "Find Next")
            btnNext = btn;
        else if (text == "Find Previous")
            btnPrev = btn;
        else if (text == "Highlight All")
            btnHighlight = btn;
        else if (text == "Clear Highlights")
            btnClear = btn;
    }

    QVERIFY2(btnNext, "Find Next button not found");
    QVERIFY2(btnPrev, "Find Previous button not found");
    QVERIFY2(btnHighlight, "Highlight All button not found");
    QVERIFY2(btnClear, "Clear Highlights button not found");

    QTest::mouseClick(btnNext, Qt::LeftButton);
    QCOMPARE(spyNext.count(), 1);
    {
        const QList<QVariant> args = spyNext.takeFirst();
        QCOMPARE(args.at(0).toString(), QString("abc"));
        QCOMPARE(args.at(1).toBool(), true);
        QCOMPARE(args.at(2).toBool(), true);
        QCOMPARE(args.at(3).toBool(), true);
    }

    QTest::mouseClick(btnPrev, Qt::LeftButton);
    QCOMPARE(spyPrev.count(), 1);
    {
        const QList<QVariant> args = spyPrev.takeFirst();
        QCOMPARE(args.at(0).toString(), QString("abc"));
        QCOMPARE(args.at(1).toBool(), true);
        QCOMPARE(args.at(2).toBool(), true);
        QCOMPARE(args.at(3).toBool(), true);
    }

    QTest::mouseClick(btnHighlight, Qt::LeftButton);
    QCOMPARE(spyHighlight.count(), 1);
    {
        const QList<QVariant> args = spyHighlight.takeFirst();
        QCOMPARE(args.at(0).toString(), QString("abc"));
        QCOMPARE(args.at(1).toBool(), true);
        QCOMPARE(args.at(2).toBool(), true);
        QCOMPARE(args.at(3).toBool(), true);
    }

    QTest::mouseClick(btnClear, Qt::LeftButton);
    QCOMPARE(spyClear.count(), 1);
}
