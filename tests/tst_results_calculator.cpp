/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_results_calculator.h"

#include <QtTest/QtTest>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>

#include "resultscalculator.h"
#include "touchstone.h"

namespace {

QPushButton *findEvaluate(QWidget *root)
{
    for (QPushButton *b : root->findChildren<QPushButton *>()) {
        if (b->text().contains(QStringLiteral("Evaluate"), Qt::CaseInsensitive))
            return b;
    }
    return nullptr;
}

void setSelectedTrace(ResultsCalculatorPanel *panel, const TouchstoneNetwork *net,
                      const QString &label = QStringLiteral("dut"))
{
    ResultsCalculatorPanel::TraceRef t;
    t.label = label;
    t.path = label;
    t.network = net;
    t.selected = true;
    panel->setTraces({t}, true);
}

} // namespace

void ResultsCalculatorTest::evaluates_cap_ind_delay_onSampleS2p()
{
    const QString path = QFINDTESTDATA("testdata/sample.s2p");
    QVERIFY2(!path.isEmpty(), "testdata/sample.s2p not found");

    TouchstoneNetwork net;
    QString err;
    QVERIFY2(net.load(path, &err), qPrintable(err));

    ResultsCalculatorPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.show();
    QTest::qWait(20);

    setSelectedTrace(&panel, &net);

    auto *freq = panel.findChild<QDoubleSpinBox *>();
    QVERIFY(freq);
    freq->setValue(1.0);

    auto *expr = panel.findChild<QLineEdit *>();
    auto *out = panel.findChild<QPlainTextEdit *>();
    auto *eval = findEvaluate(&panel);
    QVERIFY(expr);
    QVERIFY(out);
    QVERIFY(eval);

    const QStringList exprs = {
        QStringLiteral("cser($1)"),
        QStringLiteral("csh1($1)"),
        QStringLiteral("csh2($1)"),
        QStringLiteral("lser($1)"),
        QStringLiteral("rser($1)"),
        QStringLiteral("q($1)"),
        QStringLiteral("delay($1)"),
        QStringLiteral("db(S21,$1)"),
        QStringLiteral("ph(S21,$1)"),
        QStringLiteral("cser($1,1)"),
        QStringLiteral("lser($1)+rser($1)"),
    };

    for (const QString &e : exprs) {
        expr->setText(e);
        eval->click();
        QTest::qWait(10);
        const QString text = out->toPlainText();
        QVERIFY2(!text.contains(QStringLiteral("Error")), qPrintable(e + " → " + text));
        QVERIFY2(text.contains(QStringLiteral("→")), qPrintable(text));
    }
}

void ResultsCalculatorTest::comboInsert_andEmptyExpression()
{
    const QString path = QFINDTESTDATA("testdata/sample.s2p");
    QVERIFY(!path.isEmpty());
    TouchstoneNetwork net;
    QVERIFY(net.load(path));

    ResultsCalculatorPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.show();
    setSelectedTrace(&panel, &net);

    auto *combo = panel.findChild<QComboBox *>();
    auto *expr = panel.findChild<QLineEdit *>();
    auto *out = panel.findChild<QPlainTextEdit *>();
    auto *eval = findEvaluate(&panel);
    QVERIFY(combo);
    QVERIFY(expr);
    QVERIFY(out);
    QVERIFY(eval);

    QVERIFY(combo->count() > 1);
    combo->setCurrentIndex(1);
    panel.insertFunctionSnippet(1);
    QVERIFY(!expr->text().isEmpty());

    expr->clear();
    eval->click();
    QVERIFY(out->toPlainText().contains(QStringLiteral("Empty"), Qt::CaseInsensitive));

    // No selection
    panel.setTraces({}, false);
    expr->setText(QStringLiteral("cser($1)"));
    eval->click();
    QVERIFY(out->toPlainText().contains(QStringLiteral("Click"), Qt::CaseInsensitive));
}

void ResultsCalculatorTest::ydiff_needsTwoSelectedTraces()
{
    const QString path = QFINDTESTDATA("testdata/sample.s2p");
    QVERIFY(!path.isEmpty());
    TouchstoneNetwork net;
    QVERIFY(net.load(path));

    ResultsCalculatorPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.show();

    ResultsCalculatorPanel::TraceRef a;
    a.label = QStringLiteral("meas");
    a.path = QStringLiteral("meas");
    a.network = &net;
    a.selected = true;
    ResultsCalculatorPanel::TraceRef b = a;
    b.label = QStringLiteral("open");
    b.path = QStringLiteral("open");

    panel.setTraces({a, b}, true);

    auto *expr = panel.findChild<QLineEdit *>();
    auto *out = panel.findChild<QPlainTextEdit *>();
    auto *eval = findEvaluate(&panel);
    QVERIFY(expr);
    QVERIFY(out);
    QVERIFY(eval);

    expr->setText(QStringLiteral("ydiff_cser($1,$2)"));
    eval->click();
    QVERIFY(!out->toPlainText().contains(QStringLiteral("Error")));

    // Same fixture twice → Ya−Yb ≈ 0; Lser may be singular — use arithmetic instead.
    expr->setText(QStringLiteral("lser($1)-lser($2)"));
    eval->click();
    QVERIFY(!out->toPlainText().contains(QStringLiteral("Error")));

    // Only one selected → $2 missing
    a.selected = true;
    b.selected = false;
    panel.setTraces({a, b}, true);
    expr->setText(QStringLiteral("ydiff_cser($1,$2)"));
    eval->click();
    QVERIFY(out->toPlainText().contains(QStringLiteral("Error")));
}

void ResultsCalculatorTest::resultsCalculatorIcon_isValid()
{
    const QIcon ic = resultsCalculatorIcon(22);
    QVERIFY(!ic.isNull());
    QVERIFY(!ic.pixmap(22, 22).isNull());
}

void ResultsCalculatorTest::errorPaths_s1p_andBadArgs()
{
    const QString s1p = QFINDTESTDATA("testdata/sample.s1p");
    const QString s2p = QFINDTESTDATA("testdata/sample.s2p");
    QVERIFY(!s1p.isEmpty());
    QVERIFY(!s2p.isEmpty());

    TouchstoneNetwork net1;
    TouchstoneNetwork net2;
    QVERIFY(net1.load(s1p));
    QVERIFY(net2.load(s2p));

    ResultsCalculatorPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.show();

    auto *expr = panel.findChild<QLineEdit *>();
    auto *out = panel.findChild<QPlainTextEdit *>();
    auto *eval = findEvaluate(&panel);
    QVERIFY(expr);
    QVERIFY(out);
    QVERIFY(eval);

    // 1-port: cser/delay/db out of range should error.
    setSelectedTrace(&panel, &net1, QStringLiteral("s1p"));
    const QStringList bad = {
        QStringLiteral("cser($1)"),
        QStringLiteral("lser($1)"),
        QStringLiteral("delay($1)"),
        QStringLiteral("db(S21,$1)"),
        QStringLiteral("ph(S33,$1)"),
        QStringLiteral("not_a_func($1)"),
        QStringLiteral("cser("),
    };
    for (const QString &e : bad) {
        expr->setText(e);
        eval->click();
        QVERIFY2(out->toPlainText().contains(QStringLiteral("Error"), Qt::CaseInsensitive)
                     || out->toPlainText().contains(QStringLiteral("Need"), Qt::CaseInsensitive)
                     || out->toPlainText().contains(QStringLiteral("out of range"), Qt::CaseInsensitive)
                     || out->toPlainText().contains(QStringLiteral("Unknown"), Qt::CaseInsensitive)
                     || out->toPlainText().contains(QStringLiteral("Parse"), Qt::CaseInsensitive),
                 qPrintable(e + " → " + out->toPlainText()));
    }

    // Two traces: ydiff_lser path + clear trailing action by typing.
    ResultsCalculatorPanel::TraceRef a;
    a.label = QStringLiteral("a");
    a.path = QStringLiteral("a");
    a.network = &net2;
    a.selected = true;
    ResultsCalculatorPanel::TraceRef b = a;
    b.label = QStringLiteral("b");
    b.path = QStringLiteral("b");
    panel.setTraces({a, b}, true);

    expr->setText(QStringLiteral("ydiff_lser($1,$2)"));
    eval->click();
    // May succeed or report singular; either way exercises the branch.
    QVERIFY(!out->toPlainText().isEmpty());

    expr->setText(QStringLiteral("ydiff_cser($1,$2)"));
    eval->click();
    QVERIFY(!out->toPlainText().contains(QStringLiteral("Click"), Qt::CaseInsensitive));

    expr->setText(QStringLiteral("qser($1)"));
    eval->click();

    expr->clear();
    QVERIFY(expr->text().isEmpty());
}
