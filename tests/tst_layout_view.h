/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_LAYOUT_VIEW_H
#define TST_LAYOUT_VIEW_H

#include <QObject>

class LayoutViewTest : public QObject
{
    Q_OBJECT

private slots:
    void setPolygons_conductorsAndPorts_drawAndInteract();
    void visibilityOpacity_andClear();
};

#endif // TST_LAYOUT_VIEW_H
