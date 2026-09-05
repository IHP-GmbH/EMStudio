/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_SUBSTRATE_VIEW_H
#define TST_SUBSTRATE_VIEW_H

#include <QObject>

class SubstrateViewTest : public QObject
{
    Q_OBJECT

private slots:
    void rendersStackup_andHandlesHighlightZoom();
};

#endif // TST_SUBSTRATE_VIEW_H
