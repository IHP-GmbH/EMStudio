/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_TOUCHSTONE_H
#define TST_TOUCHSTONE_H

#include <QObject>

class TouchstoneTest : public QObject
{
    Q_OBJECT

private slots:
    void load_s1p_ri_hz();
    void load_s2p_ma_ghz();
    void load_s2p_db_mhz();
    void load_rejectsMissingFile();
    void load_rejectsBadToken();
    void load_infersPortsWithoutSuffix();
    void accessors_boundsAndSParam();
    void load_khzAndThzUnits();
    void load_rejectsIncompleteBlock();
    void load_acceptsDataWithoutOptionLine();
};

#endif // TST_TOUCHSTONE_H
