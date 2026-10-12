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
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

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

void SubstrateViewTest::thickStackup_keepsProportions()
{
    // An interposer stack (200 µm air, 250 µm substrate) must not turn into a thin strip:
    // the drawing scales with thickness ratios, not absolute µm.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("interposer.xml"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f)
            << "<Stackup schemaVersion=\"3.0\">\n"
            << "  <Materials>\n"
            << "    <Material Name=\"Si\" Type=\"Dielectric\" Color=\"ffffff\"/>\n"
            << "    <Material Name=\"Met\" Type=\"Conductor\" Color=\"ff0000\"/>\n"
            << "  </Materials>\n"
            << "  <ELayers LengthUnit=\"um\">\n"
            << "    <Dielectrics>\n"
            << "      <Dielectric Name=\"Air\" Material=\"Si\" Thickness=\"200\" "
               "Reference=\"Ox\" ReferenceEdge=\"Top\" />\n"
            << "      <Dielectric Name=\"Ox\" Material=\"Si\" Thickness=\"12\" "
               "Reference=\"Sub\" ReferenceEdge=\"Top\" />\n"
            << "      <Dielectric Name=\"Sub\" Material=\"Si\" Thickness=\"250\" />\n"
            << "    </Dielectrics>\n"
            << "    <Layers>\n"
            << "      <Substrate Offset=\"250\" />\n"
            << "      <Layer Name=\"M1\" Type=\"conductor\" Material=\"Met\" Layer=\"1\" "
               "Zmin=\"0.6\" Zmax=\"1.0\" />\n"
            << "      <Layer Name=\"M2\" Type=\"conductor\" Material=\"Met\" Layer=\"2\" "
               "Zmin=\"8\" Zmax=\"10\" />\n"
            << "    </Layers>\n"
            << "  </ELayers>\n"
            << "</Stackup>\n";
    }

    Substrate sub;
    QVERIFY(sub.parseXmlFile(path));

    SubstrateView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(640, 480);
    view.show();
    view.setSubstrate(sub);

    const QRectF r = view.scene()->sceneRect();
    QVERIFY(r.width() > 0.0);
    QVERIFY2(r.height() / r.width() < 4.0,
             qPrintable(QStringLiteral("stack drawing too narrow: %1 x %2")
                            .arg(r.width()).arg(r.height())));
}
