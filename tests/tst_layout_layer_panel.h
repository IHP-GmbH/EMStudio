/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_LAYOUT_LAYER_PANEL_H
#define TST_LAYOUT_LAYER_PANEL_H

#include <QObject>

class LayoutLayerPanelTest : public QObject
{
    Q_OBJECT

private slots:
    void layers_filterHighlightOpacityAndContextMenu();
};

#endif // TST_LAYOUT_LAYER_PANEL_H
