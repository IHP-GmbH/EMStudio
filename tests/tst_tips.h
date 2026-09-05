/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_TIPS_H
#define TST_TIPS_H

#include <QObject>

class TipsTest : public QObject
{
    Q_OBJECT

private slots:
    void resolveKeywordsPath_mapsElmerToPalace();
    void loadKeywordTipsCsv_parsesDelimiters();
    void mergeTipsPreferModel_keepsModelOverrides();
};

#endif // TST_TIPS_H
