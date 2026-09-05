/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_stackupexpr.h"

#include <QtTest/QtTest>

#include "stackupexpr.h"

void StackupExprTest::isExpression_and_strip()
{
    QVERIFY(StackupExpr::isExpression(QStringLiteral("=a+1")));
    QVERIFY(!StackupExpr::isExpression(QStringLiteral("12.5")));
    QCOMPARE(StackupExpr::stripEquals(QStringLiteral(" = x + 2 ")), QStringLiteral("x + 2"));
    QCOMPARE(StackupExpr::stripEquals(QStringLiteral("3.14")), QStringLiteral("3.14"));
}

void StackupExprTest::eval_literalsAndArithmetic()
{
    QHash<QString, QVariant> vars;
    QString err;

    QCOMPARE(StackupExpr::eval(QStringLiteral("12.5"), vars, &err).toDouble(), 12.5);
    QCOMPARE(StackupExpr::eval(QStringLiteral("hello"), vars, &err).toString(), QStringLiteral("hello"));

    double out = 0;
    QVERIFY(StackupExpr::evalNumber(QStringLiteral("=(1+2)*3-4/2"), vars, &out, &err));
    QCOMPARE(out, 7.0);

    QVERIFY(StackupExpr::evalNumber(QStringLiteral("=-3"), vars, &out, &err));
    QCOMPARE(out, -3.0);

    QVERIFY(StackupExpr::evalNumber(QStringLiteral("=+(4)"), vars, &out, &err));
    QCOMPARE(out, 4.0);
}

void StackupExprTest::eval_variablesAndErrors()
{
    QHash<QString, QVariant> vars;
    vars.insert(QStringLiteral("t"), 10.0);
    vars.insert(QStringLiteral("name"), QStringLiteral("Metal1"));

    QString err;
    double out = 0;
    QVERIFY(StackupExpr::evalNumber(QStringLiteral("=t*2+1"), vars, &out, &err));
    QCOMPARE(out, 21.0);

    QVERIFY(!StackupExpr::evalNumber(QStringLiteral("=missing"), vars, &out, &err));
    QVERIFY(err.contains(QStringLiteral("Unknown")));

    QVERIFY(!StackupExpr::evalNumber(QStringLiteral("=1/0"), vars, &out, &err));
    QVERIFY(err.contains(QStringLiteral("Division by zero")));

    QVERIFY(!StackupExpr::eval(QStringLiteral(""), vars, &err).isValid());
    QVERIFY(err.contains(QStringLiteral("Empty")));

    QVERIFY(StackupExpr::evalNumber(QStringLiteral("=1e-3"), vars, &out, &err));
    QCOMPARE(out, 0.001);

    QVERIFY(!StackupExpr::eval(QStringLiteral("=(1+2"), vars, &err).isValid());
    QVERIFY(err.contains(QLatin1Char(')')));
}

void StackupExprTest::evalNumber_rejectsStrings()
{
    QHash<QString, QVariant> vars;
    vars.insert(QStringLiteral("name"), QStringLiteral("Metal1"));
    QString err;
    double out = 0;
    QVERIFY(!StackupExpr::evalNumber(QStringLiteral("=name"), vars, &out, &err));
    QVERIFY(err.contains(QStringLiteral("Expected number")));
}
