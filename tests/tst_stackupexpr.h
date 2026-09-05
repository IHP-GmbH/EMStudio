/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_STACKUPEXPR_H
#define TST_STACKUPEXPR_H

#include <QObject>

class StackupExprTest : public QObject
{
    Q_OBJECT

private slots:
    void isExpression_and_strip();
    void eval_literalsAndArithmetic();
    void eval_variablesAndErrors();
    void evalNumber_rejectsStrings();
};

#endif // TST_STACKUPEXPR_H
