/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_layout_layer_panel.h"

#include <QtTest/QtTest>
#include <QCheckBox>
#include <QListWidget>
#include <QMenu>
#include <QSignalSpy>
#include <QSlider>
#include <QTimer>

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
    // Used-only filter hides UnusedMetal
    QCOMPARE(list->count(), 2);

    panel.setUsedLayersOnly(false);
    QVERIFY(usedSpy.count() >= 1);
    QCOMPARE(list->count(), 3);

    panel.setHighlightedName(QStringLiteral("Metal1"));
    QVERIFY(actSpy.count() >= 1);

    auto *slider = panel.findChild<QSlider *>();
    QVERIFY(slider);
    QVERIFY(slider->isEnabled());
    slider->setValue(55);
    QVERIFY(opSpy.count() >= 1);

    // Toggle visibility checkbox on first item
    QListWidgetItem *item = list->item(0);
    QVERIFY(item);
    item->setCheckState(Qt::Unchecked);
    QVERIFY(visSpy.count() >= 1);
    item->setCheckState(Qt::Checked);

    panel.clearHighlight();
    panel.setShowCoordinates(false);
    QVERIFY(coordSpy.count() >= 1);

    // Context menu: Show All / Hide All. Open menu and click via timers so exec() can return.
    auto runContextAction = [&panel](const QString &contains) {
        QTimer::singleShot(40, [contains]() {
            for (QWidget *w : QApplication::topLevelWidgets()) {
                auto *menu = qobject_cast<QMenu *>(w);
                if (!menu || !menu->isVisible())
                    continue;
                for (QAction *a : menu->actions()) {
                    if (a->text().contains(contains, Qt::CaseInsensitive)) {
                        a->trigger();
                        return;
                    }
                }
                menu->close();
            }
        });
        QTimer::singleShot(0, [&panel]() {
            QMetaObject::invokeMethod(&panel, "onListContextMenu",
                                      Q_ARG(QPoint, QPoint(8, 8)));
        });
        QTest::qWait(120);
    };
    runContextAction(QStringLiteral("Hide"));
    runContextAction(QStringLiteral("Show"));

    panel.clear();
    QCOMPARE(list->count(), 0);
}
