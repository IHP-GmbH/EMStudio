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

#include "tst_python_editor.h"

#include <QtTest/QtTest>
#include <QAbstractItemView>
#include <QCompleter>
#include <QDialog>
#include <QSignalSpy>
#include <QTextCursor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlock>
#include "sidebysidediff.h"

#include "pythoneditor.h"

/*!*******************************************************************************************************************
 * \brief Extracts all completion strings currently stored in the editor completer model.
 *
 * \param e Python editor under test.
 * \return Sorted list of completion strings.
 **********************************************************************************************************************/
static QStringList completionStrings(PythonEditor& e)
{
    QStringList out;

    QCompleter* c = e.completer();
    if (!c || !c->model())
        return out;

    const int rows = c->model()->rowCount();
    for (int r = 0; r < rows; ++r) {
        const QModelIndex idx = c->model()->index(r, 0);
        out << idx.data().toString();
    }

    return out;
}

/*!*******************************************************************************************************************
 * \brief Verifies that user identifiers are added to the completer model while Python keywords stay available.
 *
 * The test sets editor text containing variables and Python keywords and verifies that
 * custom identifiers are present in the completion model.
 **********************************************************************************************************************/
void PythonEditorTest::updateVariableList_addsIdentifiersToCompleter()
{
    PythonEditor e;

    e.setPlainText(
        "alpha = 1\n"
        "beta_value = alpha + 2\n"
        "for i in range(3):\n"
        "    gamma = i\n");

    QCoreApplication::processEvents();

    const QStringList items = completionStrings(e);

    QVERIFY2(items.contains("alpha"), "Identifier 'alpha' not found in completer");
    QVERIFY2(items.contains("beta_value"), "Identifier 'beta_value' not found in completer");
    QVERIFY2(items.contains("gamma"), "Identifier 'gamma' not found in completer");

    QVERIFY2(items.contains("for"), "Python keyword 'for' shall still exist in completer");
    QVERIFY2(items.contains("return"), "Python keyword 'return' shall still exist in completer");
}

/*!*******************************************************************************************************************
 * \brief Verifies plain-text search forward/backward and highlight lifecycle.
 *
 * The test searches for a repeated token, checks that the cursor moves to a match,
 * applies highlights, and then clears them again.
 **********************************************************************************************************************/
void PythonEditorTest::findAndHighlight_plainTextFlow_works()
{
    PythonEditor e;

    e.setPlainText(
        "one alpha two\n"
        "three alpha four\n"
        "five alpha six\n");

    QTextCursor c = e.textCursor();
    c.movePosition(QTextCursor::Start);
    e.setTextCursor(c);

    e.findNext("alpha", false, false, false);
    QVERIFY2(e.textCursor().selectedText() == "alpha", "findNext() did not select expected text");

    e.findNext("alpha", false, false, false);
    QVERIFY2(e.textCursor().selectedText() == "alpha", "Second findNext() did not select expected text");

    e.findPrev("alpha", false, false, false);
    QVERIFY2(e.textCursor().selectedText() == "alpha", "findPrev() did not select expected text");

    e.highlightAll("alpha", false, false, false);
    QVERIFY2(!e.extraSelections().isEmpty(), "highlightAll() shall produce extra selections");

    e.clearHighlights();
    QVERIFY2(!e.extraSelections().isEmpty(),
             "Current-line highlight shall remain after clearHighlights()");
}

/*!*******************************************************************************************************************
 * \brief Verifies regex-based forward search.
 *
 * The test searches using a regular expression pattern and verifies that the
 * first matching token is selected.
 **********************************************************************************************************************/
void PythonEditorTest::find_regex_flow_works()
{
    PythonEditor e;

    e.setPlainText("abc1 abc2 abc3");

    QTextCursor c = e.textCursor();
    c.movePosition(QTextCursor::Start);
    e.setTextCursor(c);

    e.findNext("abc\\d", false, false, true);

    QCOMPARE(e.textCursor().selectedText(), QString("abc1"));
}

/*!*******************************************************************************************************************
 * \brief Verifies wrap-around behavior for forward search.
 *
 * The test places the cursor at the end of the document and searches forward
 * for a token that exists only earlier in the text. The search shall wrap and find it.
 **********************************************************************************************************************/
void PythonEditorTest::find_wrapAround_works()
{
    PythonEditor e;

    e.setPlainText("first second third");

    QTextCursor c = e.textCursor();
    c.movePosition(QTextCursor::End);
    e.setTextCursor(c);

    e.findNext("first", false, false, false);

    QCOMPARE(e.textCursor().selectedText(), QString("first"));
}

/*!*******************************************************************************************************************
 * \brief Verifies regex-based highlight of all matches.
 *
 * The test applies highlighting with a regular expression and checks that
 * extra selections are produced for matches.
 **********************************************************************************************************************/
void PythonEditorTest::highlight_regex_works()
{
    PythonEditor e;

    e.setPlainText("a1 a2 a3");

    e.highlightAll("a\\d", false, false, true);

    QVERIFY2(e.extraSelections().size() > 1, "Regex highlight shall produce match selections");
}

/*!*******************************************************************************************************************
 * \brief Verifies that the find dialog is created and shown on demand.
 *
 * The test invokes openFindDialog() and verifies that a dialog child exists.
 **********************************************************************************************************************/
void PythonEditorTest::openFindDialog_createsDialog()
{
    PythonEditor e;

#ifdef EMSTUDIO_TESTING
    e.testOpenFindDialog();
#else
    e.openFindDialog();
#endif

    const auto dialogs = e.findChildren<QDialog*>();
    QVERIFY2(!dialogs.isEmpty(), "Find dialog was not created");
}

/*!*******************************************************************************************************************
 * \brief Verifies zoom helpers and direct font-size setter.
 *
 * The test checks that zooming changes the point size and emits sigFontSizeChanged.
 **********************************************************************************************************************/
void PythonEditorTest::zoomAndFontSize_updateEditorFont_and_emitSignal()
{
    PythonEditor e;

    const qreal initial = e.font().pointSizeF();

    QSignalSpy spy(&e, SIGNAL(sigFontSizeChanged(qreal)));

#ifdef EMSTUDIO_TESTING
    e.testZoomInText();
#else
    QSKIP("zoom test wrappers are available only in EMSTUDIO_TESTING builds");
#endif
    QVERIFY2(e.font().pointSizeF() > initial, "zoomInText() shall increase font size");
    QVERIFY2(spy.count() == 1, "zoomInText() shall emit sigFontSizeChanged");

    const qreal afterZoomIn = e.font().pointSizeF();

#ifdef EMSTUDIO_TESTING
    e.testZoomOutText();
#endif
    QVERIFY2(e.font().pointSizeF() < afterZoomIn, "zoomOutText() shall decrease font size");
    QVERIFY2(spy.count() == 2, "zoomOutText() shall emit sigFontSizeChanged");

    e.setEditorFontSize(17.0);
    QCOMPARE(e.font().pointSizeF(), 17.0);
}

/*!*******************************************************************************************************************
 * \brief Verifies that replacing the full text is undoable as one operation.
 *
 * The test sets initial content, replaces it via setPlainTextUndoable(), performs undo,
 * and verifies that the previous content is restored.
 **********************************************************************************************************************/
void PythonEditorTest::setPlainTextUndoable_restoresPreviousTextWithUndo()
{
    PythonEditor e;

    e.setPlainText("old text");
    QCOMPARE(e.toPlainText(), QString("old text"));

    e.setPlainTextUndoable("new text");
    QCOMPARE(e.toPlainText(), QString("new text"));

    e.undo();
    QCOMPARE(e.toPlainText(), QString("old text"));
}

/*! The conversion preview: the converter's aligned rows give both panes the same row count, filler rows
 *  have no line number, scrolling one pane scrolls the other, and Next / Previous step through the
 *  changes. Set DIFF_SNAPSHOT=<file.png> to save a screenshot. */
void PythonEditorTest::sideBySideDiff_alignsRowsAndSyncsScrolling()
{
    QString py = QString::fromLocal8Bit(qgetenv("EMSTUDIO_TEST_PYTHON"));
    if (py.isEmpty())
        py = QStandardPaths::findExecutable(QStringLiteral("python3"));
    const QString fixture = QFINDTESTDATA("python/fixtures/convert_loose");
    const QString script = QFINDTESTDATA("../scripts/convert_loose_to_settings.py");
    if (py.isEmpty() || fixture.isEmpty() || script.isEmpty())
        QSKIP("needs Python and the converter fixture");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir().mkpath(dir.filePath(QStringLiteral("modules")));
    const QDir mods(fixture + QStringLiteral("/modules"));
    for (const QString &f : mods.entryList({QStringLiteral("*.py")}, QDir::Files))
        QVERIFY(QFile::copy(mods.filePath(f), dir.filePath(QStringLiteral("modules/") + f)));
    const QString model = dir.filePath(QStringLiteral("model_loose.py"));
    QVERIFY(QFile::copy(fixture + QStringLiteral("/model_loose.py"), model));

    QProcess p;
    p.start(py, {script, QStringLiteral("--report"), model, QStringLiteral("--signatures"),
                 QFINDTESTDATA("../keywords/workflow_signatures.csv")});
    QVERIFY(p.waitForFinished(30000));
    const QJsonObject report = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
    QVERIFY2(report.value(QStringLiteral("ok")).toBool(), qPrintable(report.value(QStringLiteral("reason")).toString()));

    auto lines = [](const QJsonValue &v) {
        QStringList out;
        for (const QJsonValue &l : v.toArray())
            out << l.toString();
        return out;
    };
    const QStringList left = lines(report.value(QStringLiteral("original_lines")));
    const QStringList right = lines(report.value(QStringLiteral("converted_lines")));
    const QVector<SideBySideDiff::Row> rows =
        SideBySideDiff::rowsFromJson(report.value(QStringLiteral("rows")).toArray());

    SideBySideDiff view;
    view.resize(1300, 700);
    view.setContent(left, right, rows, QStringLiteral("Original"), QStringLiteral("Converted"));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    // Same rows on both sides; every source line appears once, in order, on its side.
    QCOMPARE(view.leftPane()->document()->blockCount(), rows.size());
    QCOMPARE(view.rightPane()->document()->blockCount(), rows.size());
    int lastLeft = 0, fillers = 0;
    for (int i = 0; i < rows.size(); ++i) {
        const int l = view.leftPane()->lineNumberOfRow(i);
        if (l == 0) {
            ++fillers;
            QCOMPARE(view.leftPane()->document()->findBlockByNumber(i).text(), QString());
            continue;
        }
        QCOMPARE(l, lastLeft + 1);
        lastLeft = l;
        QCOMPARE(view.leftPane()->document()->findBlockByNumber(i).text(), left.at(l - 1));
    }
    QCOMPARE(lastLeft, left.size());
    QVERIFY(fillers > 0);   // the converter adds lines (settings = {}, settings[...] = ...)

    // A changed line pair: 'margin = 50' -> "settings['margin'] = 50" on the same row.
    bool found = false;
    for (int i = 0; i < rows.size(); ++i) {
        if (view.rightPane()->document()->findBlockByNumber(i).text().startsWith(QStringLiteral("settings['margin']"))) {
            QVERIFY(view.leftPane()->document()->findBlockByNumber(i).text().startsWith(QStringLiteral("margin")));
            QVERIFY(rows.at(i).changed && !rows.at(i).rightSpans.isEmpty());
            found = true;
        }
    }
    QVERIFY(found);
    QVERIFY(!view.leftPane()->extraSelections().isEmpty());

    // Synchronized scrolling, both directions.
    QScrollBar *lv = view.leftPane()->verticalScrollBar();
    QScrollBar *rv = view.rightPane()->verticalScrollBar();
    QVERIFY(lv->maximum() > 10);
    lv->setValue(lv->maximum() / 2);
    QCOMPARE(rv->value(), lv->value());
    rv->setValue(3);
    QCOMPARE(lv->value(), 3);

    // Next / Previous change.
    QVERIFY(view.changeCount() > 2);
    auto *next = view.findChild<QPushButton *>(QStringLiteral("diffNextChange"));
    auto *prev = view.findChild<QPushButton *>(QStringLiteral("diffPrevChange"));
    QVERIFY(next && prev);
    next->click();
    QCOMPARE(view.currentChange(), 0);
    next->click();
    QCOMPARE(view.currentChange(), 1);
    QCOMPARE(rv->value(), lv->value());
    QVERIFY(view.leftPane()->textCursor().block().blockNumber() > 0);
    prev->click();
    QCOMPARE(view.currentChange(), 0);
    QVERIFY(!prev->isEnabled());

    // Each shown change reports its original lines (the dialog selects the variables assigned there);
    // jumping to an original line makes its change current.
    QSignalSpy shown(&view, &SideBySideDiff::changeShown);
    int marginLine = 0;
    for (int i = 0; i < left.size(); ++i)
        if (left.at(i).startsWith(QStringLiteral("margin")))
            marginLine = i + 1;
    QVERIFY(marginLine > 0);
    QVERIFY(view.goToLeftLine(marginLine));
    const int marginChange = view.currentChange();
    QVERIFY(view.leftLinesOfChange(marginChange).contains(marginLine));
    view.goToChange(marginChange);
    QCOMPARE(shown.count(), 1);
    QCOMPARE(shown.at(0).at(0).toInt(), marginChange);
    QVERIFY(shown.at(0).at(1).value<QVector<int>>().contains(marginLine));
    QCOMPARE(view.leftPane()->lineNumberOfRow(view.leftPane()->textCursor().blockNumber()),
             view.leftLinesOfChange(marginChange).first());
    QVERIFY(!view.goToLeftLine(100000));

    const QString snapshot = QString::fromLocal8Bit(qgetenv("DIFF_SNAPSHOT"));
    if (!snapshot.isEmpty()) {
        const QByteArray at = qgetenv("DIFF_SNAPSHOT_CHANGE");
        view.goToChange(at.isEmpty() ? 4 : at.toInt());
        QTest::qWait(50);
        view.grab().save(snapshot);
    }
}
