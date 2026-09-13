/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_EXPRPARSER_H
#define TST_EXPRPARSER_H

#include <QObject>

class ExprParserTest : public QObject
{
    Q_OBJECT

private slots:
    void arithmetic_andParentheses();
    void cap_andYdiffCalls();
    void ind_q_delay_andYdiffLser();
    void db_ph_andOptionalFreq();
    void parseErrors();
};

#endif // TST_EXPRPARSER_H
