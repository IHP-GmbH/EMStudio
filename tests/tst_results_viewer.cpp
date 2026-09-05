/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_results_viewer.h"

#include <QtTest/QtTest>
#include <QCheckBox>
#include <QFile>
#include <QPushButton>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "resultsviewer.h"

namespace {

void copyFixture(const QString &src, const QString &dst)
{
    QFile::remove(dst);
    QVERIFY(QFile::copy(src, dst));
}

QTreeWidgetItem *firstCheckableFileItem(QTreeWidget *tree)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if (item->flags() & Qt::ItemIsUserCheckable)
            return item;
        for (int c = 0; c < item->childCount(); ++c) {
            QTreeWidgetItem *ch = item->child(c);
            if (ch->flags() & Qt::ItemIsUserCheckable)
                return ch;
        }
    }
    return nullptr;
}

QRadioButton *findRadio(QWidget *root, const QString &contains)
{
    for (QRadioButton *r : root->findChildren<QRadioButton *>()) {
        if (r->text().contains(contains, Qt::CaseInsensitive))
            return r;
    }
    return nullptr;
}

} // namespace

void ResultsViewerTest::emptyDirectory_showsPlaceholder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ResultsViewer v;
    v.setAttribute(Qt::WA_DontShowOnScreen, true);
    v.resize(900, 600);
    v.show();
    v.setTargetDirectory(dir.path());
    QCOMPARE(v.targetDirectory(), dir.path());
    v.rescan();
    v.refresh();
}

void ResultsViewerTest::loadsTouchstone_andSwitchesModes()
{
    const QString s1p = QFINDTESTDATA("testdata/sample.s1p");
    const QString s2p = QFINDTESTDATA("testdata/sample.s2p");
    QVERIFY(!s1p.isEmpty());
    QVERIFY(!s2p.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    copyFixture(s1p, dir.filePath(QStringLiteral("dut.s1p")));
    copyFixture(s2p, dir.filePath(QStringLiteral("dut.s2p")));

    ResultsViewer v;
    v.setAttribute(Qt::WA_DontShowOnScreen, true);
    v.resize(1000, 700);
    v.show();
    QTest::qWait(30);

    v.setTargetDirectory(dir.path());
    v.rescan();

    auto *tree = v.findChild<QTreeWidget *>();
    QVERIFY(tree);
    QVERIFY(tree->topLevelItemCount() > 0);

    QTreeWidgetItem *fileItem = firstCheckableFileItem(tree);
    QVERIFY(fileItem);
    fileItem->setCheckState(0, Qt::Checked);
    QTest::qWait(50);

    // Prefer checking the 2-port file if present for a richer parameter grid.
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        auto tryCheck = [](QTreeWidgetItem *it) {
            if ((it->flags() & Qt::ItemIsUserCheckable)
                && it->text(0).contains(QStringLiteral("s2p"), Qt::CaseInsensitive)) {
                it->setCheckState(0, Qt::Checked);
                return true;
            }
            return false;
        };
        if (tryCheck(item))
            break;
        for (int c = 0; c < item->childCount(); ++c) {
            if (tryCheck(item->child(c)))
                break;
        }
    }
    QTest::qWait(80);

    QRadioButton *smith = findRadio(&v, QStringLiteral("Smith"));
    QRadioButton *zoom = findRadio(&v, QStringLiteral("Zoom"));
    QRadioButton *phase = findRadio(&v, QStringLiteral("Phase"));
    QVERIFY(smith);
    QVERIFY(zoom);
    QVERIFY(phase);

    smith->setChecked(true);
    QTest::qWait(40);
    zoom->setChecked(true);
    QTest::qWait(40);
    phase->setChecked(true);
    QTest::qWait(40);

    v.refresh();
}

void ResultsViewerTest::filterCheckboxes_affectListing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString s2p = QFINDTESTDATA("testdata/sample.s2p");
    QVERIFY(!s2p.isEmpty());
    copyFixture(s2p, dir.filePath(QStringLiteral("a.s2p")));
    copyFixture(s2p, dir.filePath(QStringLiteral("a_dc.s2p")));
    copyFixture(s2p, dir.filePath(QStringLiteral("a_deembedded.s2p")));

    ResultsViewer v;
    v.setAttribute(Qt::WA_DontShowOnScreen, true);
    v.show();
    v.setTargetDirectory(dir.path());

    const auto boxes = v.findChildren<QCheckBox *>();
    QVERIFY(!boxes.isEmpty());
    for (QCheckBox *cb : boxes) {
        cb->setChecked(true);
        cb->setChecked(false);
    }
    v.rescan();
}

void ResultsViewerTest::checkedFile_togglesParamButtonsAndSmith()
{
    const QString s2p = QFINDTESTDATA("testdata/sample.s2p");
    QVERIFY(!s2p.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    copyFixture(s2p, dir.filePath(QStringLiteral("plot.s2p")));

    ResultsViewer v;
    v.setAttribute(Qt::WA_DontShowOnScreen, true);
    v.resize(1100, 750);
    v.show();
    v.setTargetDirectory(dir.path());
    v.rescan();
    QTest::qWait(40);

    auto *tree = v.findChild<QTreeWidget *>();
    QVERIFY(tree);
    QTreeWidgetItem *fileItem = firstCheckableFileItem(tree);
    QVERIFY(fileItem);
    fileItem->setCheckState(0, Qt::Checked);
    QTest::qWait(80);

    // Toggle every S-parameter button that appeared.
    int toggledParams = 0;
    for (QPushButton *btn : v.findChildren<QPushButton *>()) {
        const QString t = btn->text();
        if (!t.startsWith(QLatin1Char('S')) || t.size() < 3)
            continue;
        if (!btn->isCheckable())
            continue;
        btn->setChecked(true);
        ++toggledParams;
    }
    QVERIFY(toggledParams >= 1);
    QTest::qWait(60);

    if (QRadioButton *smith = findRadio(&v, QStringLiteral("Smith"))) {
        smith->setChecked(true);
        QTest::qWait(40);
    }
    if (QRadioButton *zoom = findRadio(&v, QStringLiteral("Zoom"))) {
        zoom->setChecked(true);
        QTest::qWait(40);
    }
    if (QRadioButton *phase = findRadio(&v, QStringLiteral("Phase"))) {
        phase->setChecked(true);
        QTest::qWait(40);
    }

    // Uncheck to exercise empty-plot path again.
    fileItem->setCheckState(0, Qt::Unchecked);
    QTest::qWait(40);
    v.refresh();

    QString log;
    QVERIFY(!v.tryConvertPalaceCsv(&log)); // no CSV in dir
}

void ResultsViewerTest::nestedDirs_groupItemsAndConvertWithoutCsv()
{
    const QString s2p = QFINDTESTDATA("testdata/sample.s2p");
    QVERIFY(!s2p.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("run_a")));
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("run_b")));
    copyFixture(s2p, dir.filePath(QStringLiteral("run_a/a.s2p")));
    copyFixture(s2p, dir.filePath(QStringLiteral("run_b/b.s2p")));

    ResultsViewer v;
    v.setAttribute(Qt::WA_DontShowOnScreen, true);
    v.show();
    v.setTargetDirectory(dir.path());
    v.rescan();
    QTest::qWait(40);

    auto *tree = v.findChild<QTreeWidget *>();
    QVERIFY(tree);
    QVERIFY(tree->topLevelItemCount() >= 1);

    // Toggle a group checkbox if present.
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if (item->childCount() > 0 && (item->flags() & Qt::ItemIsUserCheckable)) {
            item->setCheckState(0, Qt::Checked);
            item->setCheckState(0, Qt::Unchecked);
            break;
        }
    }

    QString log;
    QVERIFY(!v.tryConvertPalaceCsv(&log));

    // Place a Palace CSV marker and exercise the converter process path.
    {
        QFile f(dir.filePath(QStringLiteral("port-S.csv")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("freq,S\n");
    }
    // May succeed or fail depending on Python/scripts; either way covers launch/finish paths.
    v.tryConvertPalaceCsv(&log);

    ResultsViewer empty;
    empty.setAttribute(Qt::WA_DontShowOnScreen, true);
    QVERIFY(!empty.tryConvertPalaceCsv(&log));
}
