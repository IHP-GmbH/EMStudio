/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_RESULTS_CALCULATOR_H
#define TST_RESULTS_CALCULATOR_H

#include <QObject>

class ResultsCalculatorTest : public QObject
{
    Q_OBJECT

private slots:
    void evaluates_cap_ind_delay_onSampleS2p();
    void comboInsert_andEmptyExpression();
    void ydiff_needsTwoSelectedTraces();
    void resultsCalculatorIcon_isValid();
};

#endif // TST_RESULTS_CALCULATOR_H
