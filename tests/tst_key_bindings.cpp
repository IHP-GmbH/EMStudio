/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_key_bindings.h"

#include <QtTest/QtTest>
#include <QAction>
#include <QComboBox>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QSet>
#include <QShortcut>
#include <QTabWidget>
#include <QTableWidget>

#include "keybindingsdialog.h"
#include "layoutview.h"
#include "mainwindow.h"
#include "navigationstyle.h"

void KeyBindingsTest::bindingTable_bothStylesConsistent()
{
    QCOMPARE(NavigationStyle::fromId(QStringLiteral("setupem")), NavStyle::SetupEM);
    QCOMPARE(NavigationStyle::fromId(QStringLiteral("SetupEM")), NavStyle::SetupEM);
    QCOMPARE(NavigationStyle::fromId(QString()), NavStyle::EMStudio);
    QCOMPARE(NavigationStyle::fromId(QStringLiteral("bogus")), NavStyle::EMStudio);
    QCOMPARE(NavigationStyle::id(NavStyle::SetupEM), QStringLiteral("setupem"));
    QCOMPARE(NavigationStyle::fromPreferences({}), NavStyle::EMStudio);

    for (NavStyle style : {NavStyle::EMStudio, NavStyle::SetupEM}) {
        const QVector<BindingRow> rows = NavigationStyle::bindingTable(style);
        QVERIFY(rows.size() > 20);
        // No binding is listed twice for the same viewer.
        QSet<QString> seen;
        for (const BindingRow &row : rows) {
            QVERIFY(!row.viewer.isEmpty() && !row.action.isEmpty() && !row.binding.isEmpty());
            const QString key = row.viewer + QLatin1Char('|') + row.binding;
            QVERIFY2(!seen.contains(key), qPrintable(key));
            seen.insert(key);
        }
        QVERIFY(!NavigationStyle::tooltipFor(style, QStringLiteral("Layout 3D")).isEmpty());
    }

    // The mouse rows differ between the styles, the global keys don't.
    const QString emRight = NavigationStyle::tooltipFor(NavStyle::EMStudio, QStringLiteral("Layout 3D"));
    const QString vtkRight = NavigationStyle::tooltipFor(NavStyle::SetupEM, QStringLiteral("Layout 3D"));
    QVERIFY(emRight != vtkRight);
    QCOMPARE(NavigationStyle::tooltipFor(NavStyle::EMStudio, QStringLiteral("Global")),
             NavigationStyle::tooltipFor(NavStyle::SetupEM, QStringLiteral("Global")));
}

void KeyBindingsTest::dialog_selectsStyleAndListsBindings()
{
    KeyBindingsDialog dlg(NavStyle::EMStudio);
    QCOMPARE(dlg.style(), NavStyle::EMStudio);
    auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("navStyleCombo"));
    auto *table = dlg.findChild<QTableWidget *>(QStringLiteral("keyBindingsTable"));
    QVERIFY(combo && table);
    QCOMPARE(table->rowCount(), NavigationStyle::bindingTable(NavStyle::EMStudio).size());

    combo->setCurrentIndex(combo->findData(QStringLiteral("setupem")));
    QCOMPARE(dlg.style(), NavStyle::SetupEM);
    QCOMPARE(table->rowCount(), NavigationStyle::bindingTable(NavStyle::SetupEM).size());
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("Global"));
    QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
}

void KeyBindingsTest::mainWindow_appliesStyleAndShortcuts()
{
    MainWindow w;

    // Setup menu entry and menu shortcuts.
    auto *keyAction = w.findChild<QAction *>(QStringLiteral("actionKeyBindings"));
    QVERIFY(keyAction);
    QCOMPARE(keyAction->text(), QStringLiteral("Key Bindings..."));
    const QHash<QString, QString> expected{
        {QStringLiteral("actionOpen_Python_Model"), QStringLiteral("Ctrl+O")},
        {QStringLiteral("actionSave"), QStringLiteral("Ctrl+S")},
        {QStringLiteral("actionSave_As"), QStringLiteral("Ctrl+Shift+S")},
        {QStringLiteral("actionPrefernces"), QStringLiteral("Ctrl+,")},
        {QStringLiteral("actionAbout_EMStudio"), QStringLiteral("F1")},
    };
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        auto *action = w.findChild<QAction *>(it.key());
        QVERIFY2(action, qPrintable(it.key()));
        QCOMPARE(action->shortcut(), QKeySequence(it.value()));
    }

    // The preference drives the Layout preview and the field viewer command line.
    w.testSetPreference(NavigationStyle::preferenceKey(), QStringLiteral("setupem"));
    QCOMPARE(w.testApplyNavigationStyle(), NavStyle::SetupEM);
    QStringList args = w.testFieldViewerArguments(QStringLiteral("/s/field_viewer.py"), QStringLiteral("/r"));
    int idx = args.indexOf(QStringLiteral("--nav-style"));
    QVERIFY(idx > 0);
    QCOMPARE(args.at(idx + 1), QStringLiteral("setupem"));

    w.testSetPreference(NavigationStyle::preferenceKey(), QStringLiteral("emstudio"));
    QCOMPARE(w.testApplyNavigationStyle(), NavStyle::EMStudio);
    args = w.testFieldViewerArguments(QStringLiteral("/s/field_viewer.py"), QStringLiteral("/r"));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("--nav-style")) + 1), QStringLiteral("emstudio"));

    // Ctrl+1…7 open the Run Control pages.
    auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabSettings"));
    QVERIFY(tabs);
    auto find = [&w](const QKeySequence &seq) -> QShortcut * {
        for (QShortcut *sc : w.findChildren<QShortcut *>()) {
            if (sc->key() == seq)
                return sc;
        }
        return nullptr;
    };
    auto activate = [&find](const QKeySequence &seq) {
        QShortcut *sc = find(seq);
        if (sc)
            emit sc->activated();
        return sc != nullptr;
    };
    QVERIFY(activate(QKeySequence(QStringLiteral("Ctrl+2"))));
    QCOMPARE(tabs->tabText(0), QStringLiteral("Substrate"));
    QVERIFY(activate(QKeySequence(QStringLiteral("Ctrl+1"))));
    QCOMPARE(tabs->tabText(0), QStringLiteral("Main"));

    // Fields (Ctrl+7) takes the shared layout view and turns Field mode on; Substrate gets it
    // back with Field off and its 3D view restored. No Field toggle on the view.
    auto *view = w.findChild<LayoutView *>(QStringLiteral("layoutView"));
    auto *fieldsPage = w.findChild<QWidget *>(QStringLiteral("tabFields"));
    auto *substratePane = w.findChild<QWidget *>(QStringLiteral("wdgLayoutPane"));
    auto *fieldBtn = w.findChild<QWidget *>(QStringLiteral("layoutViewFieldBtn"));
    QVERIFY(view && fieldsPage && substratePane && fieldBtn);
    QVERIFY(fieldBtn->isHidden());
    QVERIFY(activate(QKeySequence(QStringLiteral("Ctrl+2"))));
    view->setViewMode(LayoutView::ViewMode::Iso3D);
    QVERIFY(activate(QKeySequence(QStringLiteral("Ctrl+7"))));
    QCOMPARE(tabs->tabText(0), QStringLiteral("Fields"));
    QVERIFY(fieldsPage->isAncestorOf(view));
    QVERIFY(view->isFieldMode());
    QVERIFY(!view->isView3d());
    QVERIFY(activate(QKeySequence(QStringLiteral("Ctrl+2"))));
    QVERIFY(substratePane->isAncestorOf(view));
    QVERIFY(!view->isFieldMode());
    QVERIFY(view->isView3d());
    view->setViewMode(LayoutView::ViewMode::Top2D);

    // Shift+F on the view switches pages.
    QKeyEvent shiftF(QEvent::KeyPress, Qt::Key_F, Qt::ShiftModifier);
    QApplication::sendEvent(view, &shiftF);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Fields"));
    QApplication::sendEvent(view, &shiftF);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Substrate"));
    QVERIFY(find(QKeySequence(Qt::Key_F5))); // not activated: it would start a run
}
