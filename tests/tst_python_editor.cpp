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
