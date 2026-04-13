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
#include <QSignalSpy>
#include <QStringListModel>
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
 * The test sets editor text containing variables and Python keywords, triggers variable extraction,
 * and verifies that custom identifiers are present in the completion model.
 **********************************************************************************************************************/
void PythonEditorTest::updateVariableList_addsIdentifiersToCompleter()
{
    PythonEditor e;

    e.setPlainText(
        "alpha = 1\n"
        "beta_value = alpha + 2\n"
        "for i in range(3):\n"
        "    gamma = i\n");

    e.testUpdateVariableList();

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
 * \brief Verifies zoom helpers and direct font-size setter.
 *
 * The test checks that zooming changes the point size and emits sigFontSizeChanged.
 **********************************************************************************************************************/
void PythonEditorTest::zoomAndFontSize_updateEditorFont_and_emitSignal()
{
    PythonEditor e;

    const qreal initial = e.font().pointSizeF();

    QSignalSpy spy(&e, SIGNAL(sigFontSizeChanged(qreal)));

    e.testZoomInText();
    QVERIFY2(e.font().pointSizeF() > initial, "zoomInText() shall increase font size");
    QVERIFY2(spy.count() == 1, "zoomInText() shall emit sigFontSizeChanged");

    const qreal afterZoomIn = e.font().pointSizeF();

    e.testZoomOutText();
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
