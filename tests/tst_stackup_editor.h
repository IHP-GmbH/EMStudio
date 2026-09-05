/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_STACKUP_EDITOR_H
#define TST_STACKUP_EDITOR_H

#include <QObject>

class StackupEditorTest : public QObject
{
    Q_OBJECT

private slots:
    void colorDelegate_parseAndIcon();
    void loadGolden_roundTripAndSelect();
    void addRemoveRow_andSave();
    void escape_emitsClearHighlight();
    void visitAllTabs_addRowsAndComboDelegate();
    void importXmlViaTestingHook_andResolveAdsScript();
};

#endif // TST_STACKUP_EDITOR_H
