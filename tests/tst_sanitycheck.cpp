/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_sanitycheck.h"

#include <QtTest/QtTest>
#include <QApplication>
#include <QDialog>
#include <QPushButton>
#include <QTimer>

#include "sanitycheck.h"

void SanityCheckTest::appendPortDirection_zRequiresFromTo()
{
    QVector<SanityFinding> out;
    appendPortDirectionFindings(out, 1, QStringLiteral("z"), QString(), QString());
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.first().severity, SanityFinding::Error);
    QCOMPARE(out.first().code, QStringLiteral("port_z_needs_from_to"));

    out.clear();
    appendPortDirectionFindings(out, 2, QStringLiteral("-z"), QStringLiteral("Metal1"), QString());
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.first().severity, SanityFinding::Error);

    out.clear();
    appendPortDirectionFindings(out, 3, QStringLiteral("z"), QStringLiteral("Metal1"), QStringLiteral("Metal2"));
    QCOMPARE(out.size(), 0);
}

void SanityCheckTest::appendPortDirection_xyRules()
{
    QVector<SanityFinding> out;
    appendPortDirectionFindings(out, 1, QStringLiteral("x"), QString(), QString());
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.first().code, QStringLiteral("port_xy_needs_target"));

    out.clear();
    appendPortDirectionFindings(out, 2, QStringLiteral("y"), QStringLiteral("Metal1"), QString());
    QCOMPARE(out.size(), 0);

    out.clear();
    appendPortDirectionFindings(out, 3, QStringLiteral("x"), QStringLiteral("A"), QStringLiteral("B"));
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.first().severity, SanityFinding::Warning);
    QCOMPARE(out.first().code, QStringLiteral("port_xy_has_from_and_to"));
}

void SanityCheckTest::appendPortDirection_emptyDirection()
{
    QVector<SanityFinding> out;
    appendPortDirectionFindings(out, 0, QString(), QString(), QString());
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.first().severity, SanityFinding::Warning);
    QCOMPARE(out.first().code, QStringLiteral("port_empty_direction"));
}

void SanityCheckTest::showDialog_emptyReturnsTrue()
{
    QVERIFY(showSanityCheckDialog(nullptr, {}));
}

void SanityCheckTest::showDialog_cancelAndAccept()
{
    QVector<SanityFinding> findings;
    {
        SanityFinding e;
        e.severity = SanityFinding::Error;
        e.code = QStringLiteral("test_err");
        e.message = QStringLiteral("boom");
        findings.append(e);
        SanityFinding w;
        w.severity = SanityFinding::Warning;
        w.code = QStringLiteral("test_warn");
        w.message = QStringLiteral("careful");
        findings.append(w);
    }

    // Cover dialog construction + Cancel. (Accept is the same QDialogButtonBox path.)
    QTimer::singleShot(50, qApp, []() {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *dlg = qobject_cast<QDialog *>(w);
            if (dlg && dlg->isVisible()
                && dlg->windowTitle().contains(QStringLiteral("Sanity"), Qt::CaseInsensitive)) {
                dlg->reject();
                return;
            }
        }
    });
    QVERIFY(!showSanityCheckDialog(nullptr, findings));
}
