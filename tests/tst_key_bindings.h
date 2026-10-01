/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_KEY_BINDINGS_H
#define TST_KEY_BINDINGS_H

#include <QObject>

class KeyBindingsTest : public QObject
{
    Q_OBJECT

private slots:
    void bindingTable_bothStylesConsistent();
    void dialog_selectsStyleAndListsBindings();
    void mainWindow_appliesStyleAndShortcuts();
};

#endif // TST_KEY_BINDINGS_H
