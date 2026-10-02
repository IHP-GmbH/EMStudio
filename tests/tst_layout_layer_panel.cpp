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
#include <QTimer>
#include <QContextMenuEvent>
#include <QStyle>
#include <QStyleOptionViewItem>

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
    QCOMPARE(slider->value(), 100); // the layer's own fill value; "All layers" is the whole layout
    slider->setValue(55);
    QCOMPARE(opSpy.count(), 1);
    QCOMPARE(opSpy.at(0).at(1).toReal(), 0.55);
    QCOMPARE(allSpy.count(), 0);

    // Esc in the list returns to "All layers": the whole-layout opacity again.
    QSignalSpy deactSpy(&panel, &LayoutLayerPanel::layerDeactivated);
    QTest::keyClick(list, Qt::Key_Escape);
    QCOMPARE(deactSpy.count(), 1);
    QCOMPARE(list->currentItem(), list->item(0));
    QCOMPARE(slider->value(), 40);

    // A right-click (context menu) must not select the row under it: the slider stays on
    // "All layers".
    QTest::mousePress(list->viewport(), Qt::RightButton, Qt::NoModifier,
                      list->visualItemRect(list->item(1)).center());
    QTest::mouseRelease(list->viewport(), Qt::RightButton, Qt::NoModifier,
                        list->visualItemRect(list->item(1)).center());
    QCOMPARE(list->currentItem(), list->item(0));
    QCOMPARE(slider->value(), 40);
    // The context menu still opens (closed again by the timer).
    QSignalSpy menuSpy(list, &QWidget::customContextMenuRequested);
    QTimer::singleShot(100, []() {
        if (QWidget *popup = QApplication::activePopupWidget())
            popup->close();
    });
    const QPoint rowPos = list->visualItemRect(list->item(1)).center();
    QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, rowPos, list->viewport()->mapToGlobal(rowPos));
    QApplication::sendEvent(list->viewport(), &menuEvent);
    QCOMPARE(menuSpy.count(), 1);
    QCOMPARE(list->currentItem(), list->item(0));

    // refreshOpacities (e.g. switch to the Fields page) shows the view's layout opacity.
    panel.refreshOpacities([](int) { return 0.55; }, 0.0);
    QCOMPARE(slider->value(), 0);
    panel.refreshOpacities([](int) { return 0.55; }, 0.4);
    QCOMPARE(slider->value(), 40);

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
    QSignalSpy batchSpy(&panel, &LayoutLayerPanel::layersVisibilityChanged);
    QVERIFY(QMetaObject::invokeMethod(&panel, "setAllVisible", Q_ARG(bool, false)));
    QCOMPARE(batchSpy.count(), 1);   // one batch, so the view redraws once
    QCOMPARE(batchSpy.at(0).at(0).value<QVector<int>>().size(), list->count() - 1);
    QCOMPARE(list->item(1)->checkState(), Qt::Unchecked);
    QCOMPARE(list->item(0)->checkState(), Qt::Unchecked); // "All layers" follows: none shown
    QVERIFY(QMetaObject::invokeMethod(&panel, "setAllVisible", Q_ARG(bool, true)));
    QCOMPARE(list->item(1)->checkState(), Qt::Checked);
    QCOMPARE(list->item(0)->checkState(), Qt::Checked);

    // "All layers" check box: partly checked when some layers are hidden; checking it shows all,
    // unchecking it hides all listed layers.
    QVERIFY(list->item(0)->flags() & Qt::ItemIsUserCheckable);
    QVERIFY(list->count() > 2);
    list->item(1)->setCheckState(Qt::Unchecked);
    QCOMPARE(list->item(0)->checkState(), Qt::PartiallyChecked);
    batchSpy.clear();
    list->item(0)->setCheckState(Qt::Checked);   // what a click on a partly checked box does
    QCOMPARE(list->item(1)->checkState(), Qt::Checked);
    QCOMPARE(batchSpy.count(), 1);
    QVERIFY(batchSpy.at(0).at(1).toBool());
    list->item(0)->setCheckState(Qt::Unchecked);
    for (int i = 1; i < list->count(); ++i)
        QCOMPARE(list->item(i)->checkState(), Qt::Unchecked);
    list->item(0)->setCheckState(Qt::Checked);
    for (int i = 1; i < list->count(); ++i)
        QCOMPARE(list->item(i)->checkState(), Qt::Checked);

    // A real click on the partly checked box shows all layers.
    list->item(1)->setCheckState(Qt::Unchecked);
    QCOMPARE(list->item(0)->checkState(), Qt::PartiallyChecked);
    QStyleOptionViewItem opt;
    opt.initFrom(list);
    opt.rect = list->visualItemRect(list->item(0));
    opt.features |= QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
    opt.text = list->item(0)->text();
    const QRect box = list->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &opt, list);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, box.center());
    QCOMPARE(list->item(0)->checkState(), Qt::Checked);
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

/*! Port markers without stackup layers are listed as "(not mapped)": italic, with an explanation,
 *  while lookups by layer name keep working. */
void LayoutLayerPanelTest::unmappedPort_isMarkedInList()
{
    LayoutLayerPanel panel;
    LayoutLayerPanel::Entry metal;
    metal.gdsLayer = 8;
    metal.name = QStringLiteral("Metal1");
    metal.kind = QStringLiteral("conductor");
    metal.color = Qt::blue;
    LayoutLayerPanel::Entry mapped;
    mapped.gdsLayer = 201;
    mapped.name = QStringLiteral("P1");
    mapped.kind = QStringLiteral("port");
    mapped.color = Qt::magenta;
    LayoutLayerPanel::Entry unmapped = mapped;
    unmapped.gdsLayer = 202;
    unmapped.name = QStringLiteral("P2");
    unmapped.unmapped = true;
    panel.setLayers({metal, mapped, unmapped});

    auto *list = panel.findChild<QListWidget *>();
    QVERIFY(list);
    QListWidgetItem *p1 = nullptr;
    QListWidgetItem *p2 = nullptr;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->text().startsWith(QStringLiteral("P1")))
            p1 = list->item(i);
        if (list->item(i)->text().startsWith(QStringLiteral("P2")))
            p2 = list->item(i);
    }
    QVERIFY(p1 && p2);
    QCOMPARE(p1->text(), QStringLiteral("P1"));
    QCOMPARE(p2->text(), QStringLiteral("P2 (not mapped)"));
    QVERIFY(p2->font().italic());
    QVERIFY(p2->toolTip().contains(QStringLiteral("guessed position")));

    panel.setHighlightedName(QStringLiteral("P2"));  // name lookup ignores the note
    QCOMPARE(list->currentItem(), p2);

    // Context menu "Hide Unmapped": hides only the "not mapped" markers, in one batch.
    QSignalSpy batchSpy(&panel, &LayoutLayerPanel::layersVisibilityChanged);
    QVERIFY(QMetaObject::invokeMethod(&panel, "hideUnmappedLayers"));
    QCOMPARE(p2->checkState(), Qt::Unchecked);
    QCOMPARE(p1->checkState(), Qt::Checked);
    QCOMPARE(batchSpy.count(), 1);
    QCOMPARE(batchSpy.at(0).at(0).value<QVector<int>>().size(), 1);
    QVERIFY(!batchSpy.at(0).at(1).toBool());
    QCOMPARE(list->item(0)->checkState(), Qt::PartiallyChecked);
}
