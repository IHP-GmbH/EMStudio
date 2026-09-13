/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_GDSLAYOUT_H
#define TST_GDSLAYOUT_H

#include <QObject>

class GdsLayoutTest : public QObject
{
    Q_OBJECT

private slots:
    void flattenTopCell_goldenGds_returnsPolygons();
    void flattenTopCell_missingFileOrCell_fails();
};

#endif // TST_GDSLAYOUT_H
