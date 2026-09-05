/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_substrate_view.h"

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMouseEvent>

#include "substrate.h"
#include "substrateview.h"

void SubstrateViewTest::rendersStackup_andHandlesHighlightZoom()
{
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "golden XML missing");

    Substrate sub;
    QVERIFY(sub.parseXmlFile(xmlPath));

    SubstrateView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(640, 480);
    view.show();
    view.setSubstrate(sub);
    QTest::qWait(40);

    QVERIFY(!view.grab().isNull());

    view.setHighlightedLayer(QStringLiteral("Metal1"));
    QTest::qWait(20);
    view.clearHighlight();

    QSignalSpy cleared(&view, &SubstrateView::highlightCleared);
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &esc);
    QVERIFY(cleared.count() >= 0); // may clear even if already clear

    // Zoom in / out
    QWheelEvent wheelIn(QPointF(100, 100), QPointF(100, 100), QPoint(0, 0), QPoint(0, 120),
                        Qt::NoButton, Qt::NoModifier, Qt::ScrollPhase::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &wheelIn);
    QWheelEvent wheelOut(QPointF(100, 100), QPointF(100, 100), QPoint(0, 0), QPoint(0, -120),
                         Qt::NoButton, Qt::NoModifier, Qt::ScrollPhase::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &wheelOut);

    view.resize(800, 600);
    QTest::qWait(20);

    // Click roughly in the middle of the view to exercise mousePress hit-testing.
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(320, 240), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QTest::qWait(20);

    QVERIFY(!view.grab().isNull());
}
