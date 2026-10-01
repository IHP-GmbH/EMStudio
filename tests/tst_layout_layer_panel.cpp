/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_layout_layer_panel.h"

#include <QtTest/QtTest>
#include <QCheckBox>
#include <QLabel>
#include <QListWidget>
#include <QSignalSpy>
#include <QSlider>

#include "layoutlayerpanel.h"

void LayoutLayerPanelTest::layers_filterHighlightOpacityAndContextMenu()
{
    LayoutLayerPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.resize(220, 400);
    panel.show();
    QTest::qWait(20);

    panel.setTitleVisible(true);
    panel.setShowCoordinates(true);
    QVERIFY(panel.showCoordinates());
    panel.setUsedLayersOnly(true);
    QVERIFY(panel.usedLayersOnly());

    QVector<LayoutLayerPanel::Entry> entries;
    LayoutLayerPanel::Entry used;
    used.gdsLayer = 1;
    used.name = QStringLiteral("Metal1");
    used.kind = QStringLiteral("conductor");
    used.color = QColor(200, 80, 40);
    used.used = true;
    used.visible = true;
    used.opacity = 1.0;
    entries << used;

    LayoutLayerPanel::Entry unused = used;
    unused.gdsLayer = 99;
    unused.name = QStringLiteral("UnusedMetal");
    unused.used = false;
    unused.visible = false;
    unused.opacity = 0.5;
    entries << unused;

    LayoutLayerPanel::Entry via = used;
    via.gdsLayer = 2;
    via.name = QStringLiteral("Via1");
    via.kind = QStringLiteral("via");
    via.color = QColor(80, 80, 80);
    entries << via;

    QSignalSpy visSpy(&panel, &LayoutLayerPanel::visibilityChanged);
    QSignalSpy opSpy(&panel, &LayoutLayerPanel::opacityChanged);
    QSignalSpy actSpy(&panel, &LayoutLayerPanel::layerActivated);
    QSignalSpy usedSpy(&panel, &LayoutLayerPanel::usedLayersOnlyToggled);
    QSignalSpy coordSpy(&panel, &LayoutLayerPanel::showCoordinatesToggled);

    panel.setLayers(entries);

    auto *list = panel.findChild<QListWidget *>();
    QVERIFY(list);
    // Row 0 is "All layers"; used-only filter hides UnusedMetal
    QCOMPARE(list->count(), 3);
    QCOMPARE(list->item(0)->text(), QStringLiteral("All layers"));

    // Nothing selected → "All layers": slider enabled and shows the true opacity.
    auto *slider = panel.findChild<QSlider *>();
    QVERIFY(slider);
    QVERIFY(slider->isEnabled());
    QCOMPARE(slider->value(), 100);
    QSignalSpy allSpy(&panel, &LayoutLayerPanel::allOpacityChanged);
    slider->setValue(40);
    QCOMPARE(allSpy.count(), 1);
    QCOMPARE(allSpy.takeFirst().at(0).toReal(), 0.4);

    panel.setUsedLayersOnly(false);
    QVERIFY(usedSpy.count() >= 1);
    QCOMPARE(list->count(), 4);

    panel.setHighlightedName(QStringLiteral("Metal1"));
    QVERIFY(actSpy.count() >= 1);

    QVERIFY(slider->isEnabled());
    QCOMPARE(slider->value(), 40); // the layer's own value (set via "All layers")
    slider->setValue(55);
    QCOMPARE(opSpy.count(), 1);
    QCOMPARE(opSpy.at(0).at(1).toReal(), 0.55);
    QCOMPARE(allSpy.count(), 0);

    // Esc in the list returns to "All layers"; layers now differ → average, "mixed".
    QSignalSpy deactSpy(&panel, &LayoutLayerPanel::layerDeactivated);
    QTest::keyClick(list, Qt::Key_Escape);
    QCOMPARE(deactSpy.count(), 1);
    QCOMPARE(list->currentItem(), list->item(0));
    QCOMPARE(slider->value(), 48); // (0.55 + 0.40) / 2 over used layers
    bool mixedShown = false;
    for (QLabel *l : panel.findChildren<QLabel *>())
        mixedShown |= l->text().contains(QStringLiteral("mixed"));
    QVERIFY(mixedShown);

    // Unused layer: opacity slider must stay disabled (nothing to fade in the preview).
    panel.setHighlightedName(QStringLiteral("UnusedMetal"));
    QVERIFY(!slider->isEnabled());
    panel.setHighlightedName(QStringLiteral("Metal1"));
    QVERIFY(slider->isEnabled());

    // Toggle visibility checkbox on the first layer (row 0 is "All layers")
    QListWidgetItem *item = list->item(1);
    QVERIFY(item);
    item->setCheckState(Qt::Unchecked);
    QVERIFY(visSpy.count() >= 1);
    item->setCheckState(Qt::Checked);

    panel.clearHighlight();
    panel.setShowCoordinates(false);
    QVERIFY(coordSpy.count() >= 1);

    // Context menu "Hide All" / "Show All" call setAllVisible(). (Driving QMenu::exec()
    // itself is unreliable on the offscreen platform: the popup may close at once.)
    QVERIFY(QMetaObject::invokeMethod(&panel, "setAllVisible", Q_ARG(bool, false)));
    QCOMPARE(list->item(1)->checkState(), Qt::Unchecked);
    QCOMPARE(list->item(0)->flags() & Qt::ItemIsUserCheckable, Qt::ItemFlags()); // "All layers" untouched
    QVERIFY(QMetaObject::invokeMethod(&panel, "setAllVisible", Q_ARG(bool, true)));
    QCOMPARE(list->item(1)->checkState(), Qt::Checked);

    panel.clear();
    QCOMPARE(list->count(), 0);
}

void LayoutLayerPanelTest::opacity_deferredUntilSliderReleased()
{
    LayoutLayerPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.resize(220, 400);
    panel.show();

    LayoutLayerPanel::Entry e;
    e.gdsLayer = 1;
    e.name = QStringLiteral("Metal1");
    e.kind = QStringLiteral("conductor");
    e.color = QColor(200, 80, 40);
    e.used = true;
    e.visible = true;
    e.opacity = 1.0;
    panel.setLayers({e});

    auto *slider = panel.findChild<QSlider *>();
    QVERIFY(slider);
    QSignalSpy allSpy(&panel, &LayoutLayerPanel::allOpacityChanged);
    QSignalSpy oneSpy(&panel, &LayoutLayerPanel::opacityChanged);

    // Not deferred (2D): every step is emitted at once.
    QVERIFY(!panel.deferOpacityUpdates());
    slider->setValue(70);
    QCOMPARE(allSpy.count(), 1);
    allSpy.clear();

    // Deferred (Iso3D): nothing while the handle is held, one signal on release.
    panel.setDeferOpacityUpdates(true);
    slider->setSliderDown(true);
    slider->setValue(60);
    slider->setValue(30);
    QCOMPARE(allSpy.count(), 0);
    bool labelFollows = false;
    for (QLabel *l : panel.findChildren<QLabel *>())
        labelFollows |= l->text().contains(QStringLiteral("30%"));
    QVERIFY(labelFollows);
    slider->setSliderDown(false);
    QCOMPARE(allSpy.count(), 1);
    QCOMPARE(allSpy.takeFirst().at(0).toReal(), 0.3);

    // Wheel / key steps: one signal once the value settles.
    slider->setValue(20);
    slider->setValue(10);
    QCOMPARE(allSpy.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(allSpy.count(), 1, 2000);
    QCOMPARE(allSpy.takeFirst().at(0).toReal(), 0.1);

    // A pending change is flushed for its own layer when the selection changes.
    panel.setHighlightedName(QStringLiteral("Metal1"));
    slider->setSliderDown(true);
    slider->setValue(50);
    QCOMPARE(oneSpy.count(), 0);
    panel.setDeferOpacityUpdates(false); // leaving Iso3D flushes at once
    QCOMPARE(oneSpy.count(), 1);
    QCOMPARE(oneSpy.at(0).at(0).toInt(), 1);
    QCOMPARE(oneSpy.at(0).at(1).toReal(), 0.5);
    slider->setSliderDown(false);
}
