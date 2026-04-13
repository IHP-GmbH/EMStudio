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

#ifndef TST_PYTHON_EDITOR_H
#define TST_PYTHON_EDITOR_H

#include <QObject>

class PythonEditorTest : public QObject
{
    Q_OBJECT

private slots:
    void updateVariableList_addsIdentifiersToCompleter();
    void findAndHighlight_plainTextFlow_works();
    void zoomAndFontSize_updateEditorFont_and_emitSignal();
    void setPlainTextUndoable_restoresPreviousTextWithUndo();
};

#endif // TST_PYTHON_EDITOR_H
