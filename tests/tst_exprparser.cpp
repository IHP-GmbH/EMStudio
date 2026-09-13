/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_exprparser.h"

#include <QtTest/QtTest>

#include "exprparser.h"

namespace {

ExprParser makeParser(const QString &expr, double defaultF = 1.0)
{
    return ExprParser(
        expr,
        defaultF,
        [](CapKind kind, int t, double f, double *out, QString *) {
            *out = 10.0 + static_cast<int>(kind) + t + f * 0.1;
            return true;
        },
        [](CapKind kind, int a, int b, double f, double *out, QString *) {
            *out = 100.0 + static_cast<int>(kind) + 10 * a - b + f;
            return true;
        },
        [](bool wantDb, int m, int n, int t, double f, double *out, QString *) {
            *out = wantDb ? (-20.0 - m - n) : (30.0 + t + f);
            return true;
        },
        [](IndKind kind, int t1, int t2, double f, double *out, QString *) {
            if (kind == IndKind::Delay) {
                *out = 12.5 + t1 + f;
                return true;
            }
            if (t2 > 0) {
                *out = 70.0 + t1 - t2 + f;
                return true;
            }
            *out = 50.0 + static_cast<int>(kind) + t1 + f * 0.01;
            return true;
        });
}

bool evalOk(const QString &expr, double *out, QString *err = nullptr, double f = 1.0)
{
    QString local;
    QString *e = err ? err : &local;
    ExprParser p = makeParser(expr, f);
    return p.parse(out, e);
}

} // namespace

void ExprParserTest::arithmetic_andParentheses()
{
    double v = 0;
    QString err;
    QVERIFY(evalOk(QStringLiteral("1+2*3"), &v, &err));
    QCOMPARE(v, 7.0);

    QVERIFY(evalOk(QStringLiteral("(1+2)*3-4/2"), &v, &err));
    QCOMPARE(v, 7.0);

    QVERIFY(evalOk(QStringLiteral("-3"), &v, &err));
    QCOMPARE(v, -3.0);

    QVERIFY(evalOk(QStringLiteral("+4"), &v, &err));
    QCOMPARE(v, 4.0);

    QVERIFY(evalOk(QStringLiteral("10/2"), &v, &err));
    QCOMPARE(v, 5.0);

    // unicode minus normalized by parser
    QVERIFY(evalOk(QStringLiteral("5−2"), &v, &err));
    QCOMPARE(v, 3.0);
}

void ExprParserTest::cap_andYdiffCalls()
{
    double v = 0;
    QString err;
    // cser → CapKind::Cser=0 → 10+0+1+0.1 = 11.1 at default f=1
    QVERIFY(evalOk(QStringLiteral("cser($1)"), &v, &err));
    QCOMPARE(v, 11.1);

    QVERIFY(evalOk(QStringLiteral("csh1($2)"), &v, &err));
    QCOMPARE(v, 10.0 + 1 + 2 + 0.1);

    QVERIFY(evalOk(QStringLiteral("csh2($1)"), &v, &err));
    QCOMPARE(v, 10.0 + 2 + 1 + 0.1);

    QVERIFY(evalOk(QStringLiteral("ydiff_cser($1,$2)"), &v, &err));
    QCOMPARE(v, 100.0 + 0 + 10 - 2 + 1.0);

    QVERIFY(evalOk(QStringLiteral("ycser($3,$1)"), &v, &err));
    QCOMPARE(v, 100.0 + 0 + 30 - 1 + 1.0);

    QVERIFY(evalOk(QStringLiteral("ydiff_csh1($1,$2)"), &v, &err));
    QCOMPARE(v, 100.0 + 1 + 10 - 2 + 1.0);

    QVERIFY(evalOk(QStringLiteral("ydiff_csh2($1,$2)"), &v, &err));
    QCOMPARE(v, 100.0 + 2 + 10 - 2 + 1.0);

    QVERIFY(evalOk(QStringLiteral("cser($1)+csh1($1)"), &v, &err));
    QCOMPARE(v, 11.1 + (10.0 + 1 + 1 + 0.1));
}

void ExprParserTest::ind_q_delay_andYdiffLser()
{
    double v = 0;
    QString err;
    QVERIFY(evalOk(QStringLiteral("lser($1)"), &v, &err));
    QCOMPARE(v, 50.0 + 0 + 1 + 0.01);

    QVERIFY(evalOk(QStringLiteral("rser($1)"), &v, &err));
    QCOMPARE(v, 50.0 + 1 + 1 + 0.01);

    QVERIFY(evalOk(QStringLiteral("q($1)"), &v, &err));
    QCOMPARE(v, 50.0 + 2 + 1 + 0.01);

    QVERIFY(evalOk(QStringLiteral("qser($2)"), &v, &err));
    QCOMPARE(v, 50.0 + 2 + 2 + 0.01);

    QVERIFY(evalOk(QStringLiteral("delay($1)"), &v, &err));
    QCOMPARE(v, 12.5 + 1 + 1.0);

    QVERIFY(evalOk(QStringLiteral("tdelay($3)"), &v, &err));
    QCOMPARE(v, 12.5 + 3 + 1.0);

    QVERIFY(evalOk(QStringLiteral("ydiff_lser($1,$2)"), &v, &err));
    QCOMPARE(v, 70.0 + 1 - 2 + 1.0);

    QVERIFY(evalOk(QStringLiteral("ylser($2,$1)"), &v, &err));
    QCOMPARE(v, 70.0 + 2 - 1 + 1.0);
}

void ExprParserTest::db_ph_andOptionalFreq()
{
    double v = 0;
    QString err;
    QVERIFY(evalOk(QStringLiteral("db(S21,$1)"), &v, &err));
    QCOMPARE(v, -20.0 - 2 - 1);

    QVERIFY(evalOk(QStringLiteral("ph(S11,$2)"), &v, &err));
    QCOMPARE(v, 30.0 + 2 + 1.0);

    QVERIFY(evalOk(QStringLiteral("phase(S2,1,$1)"), &v, &err));
    QCOMPARE(v, 30.0 + 1 + 1.0);

    QVERIFY(evalOk(QStringLiteral("cser($1, 2.5)"), &v, &err));
    QCOMPARE(v, 10.0 + 0 + 1 + 0.25);

    QVERIFY(evalOk(QStringLiteral("cser($1, 2GHz)"), &v, &err));
    QCOMPARE(v, 10.0 + 0 + 1 + 0.2);

    QVERIFY(evalOk(QStringLiteral("lser($1, 3G)"), &v, &err));
    QCOMPARE(v, 50.0 + 0 + 1 + 0.03);
}

void ExprParserTest::parseErrors()
{
    double v = 0;
    QString err;

    QVERIFY(!evalOk(QStringLiteral("1/0"), &v, &err));
    QVERIFY(err.contains(QStringLiteral("Division by zero")));

    QVERIFY(!evalOk(QStringLiteral("foo($1)"), &v, &err));
    QVERIFY(err.contains(QStringLiteral("Unknown")));

    QVERIFY(!evalOk(QStringLiteral("cser($1"), &v, &err));
    QVERIFY(err.contains(QLatin1Char(')')));

    QVERIFY(!evalOk(QStringLiteral("cser 1"), &v, &err));
    QVERIFY(err.contains(QLatin1Char('(')));

    QVERIFY(!evalOk(QStringLiteral("cser($)"), &v, &err));
    QVERIFY(err.contains(QStringLiteral("$N")) || err.contains(QStringLiteral("trace")));

    QVERIFY(!evalOk(QStringLiteral("ydiff_cser($1)"), &v, &err));
    QVERIFY(err.contains(QStringLiteral("two")));

    QVERIFY(!evalOk(QStringLiteral("db(S21)"), &v, &err));
    QVERIFY(err.contains(QStringLiteral("curve")) || err.contains(QStringLiteral("S21")));

    QVERIFY(!evalOk(QStringLiteral("1+"), &v, &err));
    QVERIFY(!err.isEmpty());

    QVERIFY(!evalOk(QStringLiteral("(1+2"), &v, &err));
    QVERIFY(err.contains(QLatin1Char(')')));

    QVERIFY(!evalOk(QStringLiteral("cser($0)"), &v, &err));
    QVERIFY(err.contains(QStringLiteral("≥")) || err.contains(QStringLiteral("1")));
}
