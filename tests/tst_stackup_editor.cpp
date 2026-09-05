/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_stackup_editor.h"

#include <QtTest/QtTest>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QKeyEvent>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QPainter>
#include <QPixmap>
#include <QComboBox>
#include <QPlainTextEdit>

#include "stackupeditor.h"
#include "substrate.h"

namespace {

QPushButton *findButtonByText(QWidget *root, const QString &text)
{
    const auto buttons = root->findChildren<QPushButton *>();
    for (QPushButton *b : buttons) {
        if (b->text() == text)
            return b;
    }
    return nullptr;
}

} // namespace

void StackupEditorTest::colorDelegate_parseAndIcon()
{
    QVERIFY(StackupColorDelegate::isExpression(QStringLiteral("=metal_color")));
    QVERIFY(!StackupColorDelegate::isExpression(QStringLiteral("ff0000")));

    const QColor hex = StackupColorDelegate::parseColor(QStringLiteral("ff0000"));
    QVERIFY(hex.isValid());
    QCOMPARE(hex.name(QColor::HexRgb).toLower(), QStringLiteral("#ff0000"));

    const QColor hash = StackupColorDelegate::parseColor(QStringLiteral("#00ff00"));
    QVERIFY(hash.isValid());

    QVERIFY(!StackupColorDelegate::parseColor(QStringLiteral("=expr")).isValid());
    QVERIFY(!StackupColorDelegate::parseColor(QString()).isValid());

    const QIcon icon = StackupColorDelegate::colorIcon(hex);
    QVERIFY(!icon.isNull());
    QVERIFY(!StackupColorDelegate::colorIcon(QColor()).isNull());

    StackupColorDelegate del;
    QStandardItemModel model(1, 1);
    model.setData(model.index(0, 0), QStringLiteral("336699"));
    QStyleOptionViewItem opt;
    opt.rect = QRect(0, 0, 80, 20);
    QPixmap pm(80, 20);
    pm.fill(Qt::white);
    QPainter p(&pm);
    del.paint(&p, opt, model.index(0, 0));
    p.end();

    QWidget host;
    QWidget *ed = del.createEditor(&host, opt, model.index(0, 0));
    QVERIFY(ed);
    del.setEditorData(ed, model.index(0, 0));
    del.setModelData(ed, &model, model.index(0, 0));
}

void StackupEditorTest::loadGolden_roundTripAndSelect()
{
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "golden XML missing");

    Substrate sub;
    QVERIFY(sub.parseXmlFile(xmlPath));

    StackupEditor ed;
    ed.setAttribute(Qt::WA_DontShowOnScreen, true);
    ed.setSubstrate(sub);
    ed.setFilePath(xmlPath);
    QVERIFY(!ed.isModified());
    QCOMPARE(ed.filePath(), xmlPath);

    const Substrate collected = ed.substrate();
    QVERIFY(collected.materials().size() >= sub.materials().size() - 1);
    QVERIFY(!collected.layers().isEmpty());
    QVERIFY(!collected.dielectrics().isEmpty());

    ed.selectStackItem(QStringLiteral("Metal1"), QStringLiteral("layer"));
    ed.selectStackItem(QStringLiteral("SiO2"), QStringLiteral("dielectric"));
    ed.selectStackItem(QStringLiteral("Metal1"), QStringLiteral("material"));
    ed.clearTableSelection();

    ed.markModified();
    QVERIFY(ed.isModified());

    // Persist via Save (path already set) to clear dirty flag without Save As dialog.
    auto *box = ed.findChild<QDialogButtonBox *>();
    QVERIFY(box);
    QPushButton *saveBtn = box->button(QDialogButtonBox::Save);
    QVERIFY(saveBtn);
    // Write to a temp copy so we do not mutate golden fixture.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.filePath(QStringLiteral("out.xml"));
    ed.setFilePath(out);
    QTest::mouseClick(saveBtn, Qt::LeftButton);
    QVERIFY(QFileInfo::exists(out));
    QVERIFY(!ed.isModified());
}

void StackupEditorTest::addRemoveRow_andSave()
{
    Substrate sub;
    StackupVariable v;
    v.name = QStringLiteral("a");
    v.valueRaw = QStringLiteral("1");
    sub.variables().append(v);

    Material mat;
    mat.setName(QStringLiteral("Si"));
    mat.setType(QStringLiteral("Dielectric"));
    mat.setColorHex(QStringLiteral("aaaaaa"));
    sub.materials().append(mat);

    Dielectric d;
    d.setName(QStringLiteral("D1"));
    d.setMaterial(QStringLiteral("Si"));
    d.setThicknessRaw(QStringLiteral("10"));
    sub.dielectrics().append(d);

    StackupEditor ed;
    ed.setAttribute(Qt::WA_DontShowOnScreen, true);
    ed.show();
    ed.setSubstrate(sub);

    auto *tabs = ed.findChild<QTabWidget *>();
    QVERIFY(tabs);
    // Variables is tab 0
    tabs->setCurrentIndex(0);

    QPushButton *add = findButtonByText(&ed, QStringLiteral("Add row"));
    QPushButton *remove = findButtonByText(&ed, QStringLiteral("Remove row"));
    QPushButton *recompute = findButtonByText(&ed, QStringLiteral("Recompute"));
    QVERIFY(add);
    QVERIFY(remove);
    QVERIFY(recompute);

    const auto tables = ed.findChildren<QTableWidget *>();
    QVERIFY(!tables.isEmpty());
    QTableWidget *vars = nullptr;
    for (QTableWidget *t : tables) {
        if (t->columnCount() == 4 && t->horizontalHeaderItem(0)
            && t->horizontalHeaderItem(0)->text().contains(QStringLiteral("Name"))) {
            // Prefer Variables table (Name/Value/Type/Resolved)
            if (t->horizontalHeaderItem(1)
                && t->horizontalHeaderItem(1)->text().contains(QStringLiteral("Value"))) {
                vars = t;
                break;
            }
        }
    }
    QVERIFY(vars);
    const int before = vars->rowCount();

    QTest::mouseClick(add, Qt::LeftButton);
    QCOMPARE(vars->rowCount(), before + 1);

    vars->setCurrentCell(vars->rowCount() - 1, 0);
    QTest::mouseClick(remove, Qt::LeftButton);
    QCOMPARE(vars->rowCount(), before);

    QTest::mouseClick(recompute, Qt::LeftButton);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.filePath(QStringLiteral("mini.xml"));
    ed.setFilePath(out);
    auto *box = ed.findChild<QDialogButtonBox *>();
    QTest::mouseClick(box->button(QDialogButtonBox::Save), Qt::LeftButton);
    QVERIFY(QFileInfo::exists(out));

    Substrate loaded;
    QVERIFY(loaded.parseXmlFile(out));
    QVERIFY(!loaded.materials().isEmpty());
}

void StackupEditorTest::escape_emitsClearHighlight()
{
    StackupEditor ed;
    ed.setAttribute(Qt::WA_DontShowOnScreen, true);
    ed.show();
    QSignalSpy spy(&ed, &StackupEditor::clearStackHighlightRequested);
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&ed, &esc);
    QCOMPARE(spy.count(), 1);
}

void StackupEditorTest::visitAllTabs_addRowsAndComboDelegate()
{
    Substrate sub;
    Material mat;
    mat.setName(QStringLiteral("Si"));
    mat.setType(QStringLiteral("Dielectric"));
    mat.setColorHex(QStringLiteral("=metal_color"));
    sub.materials().append(mat);
    StackupVariable colorVar;
    colorVar.name = QStringLiteral("metal_color");
    colorVar.valueRaw = QStringLiteral("ff00aa");
    colorVar.type = QStringLiteral("string");
    sub.variables().append(colorVar);

    Dielectric d;
    d.setName(QStringLiteral("Ox"));
    d.setMaterial(QStringLiteral("Si"));
    d.setThicknessRaw(QStringLiteral("2"));
    sub.dielectrics().append(d);

    Layer lay;
    lay.setName(QStringLiteral("M1"));
    lay.setType(QStringLiteral("conductor"));
    lay.setMaterial(QStringLiteral("Si"));
    lay.setLayerNumber(1);
    lay.setZminRaw(QStringLiteral("0"));
    lay.setZmaxRaw(QStringLiteral("1"));
    sub.layers().append(lay);

    StackupEditor ed;
    ed.setAttribute(Qt::WA_DontShowOnScreen, true);
    ed.show();
    ed.setSubstrate(sub);

    auto *tabs = ed.findChild<QTabWidget *>();
    QVERIFY(tabs);
    QPushButton *add = findButtonByText(&ed, QStringLiteral("Add row"));
    QVERIFY(add);

    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i);
        QTest::mouseClick(add, Qt::LeftButton);
    }

    // Expression color → line-edit editor path
    StackupColorDelegate colorDel;
    QStandardItemModel model(1, 1);
    model.setData(model.index(0, 0), QStringLiteral("=metal_color"));
    QStyleOptionViewItem opt;
    opt.rect = QRect(0, 0, 40, 20);
    QWidget host;
    QWidget *edExpr = colorDel.createEditor(&host, opt, model.index(0, 0));
    QVERIFY(edExpr);
    colorDel.setEditorData(edExpr, model.index(0, 0));
    colorDel.setModelData(edExpr, &model, model.index(0, 0));

    // Combo delegate paths
    StackupComboDelegate comboDel([]() {
        return QStringList{QStringLiteral("A"), QStringLiteral("B")};
    });
    QStandardItemModel comboModel(1, 1);
    comboModel.setData(comboModel.index(0, 0), QStringLiteral("A"));
    QWidget *cb = comboDel.createEditor(&host, opt, comboModel.index(0, 0));
    QVERIFY(qobject_cast<QComboBox *>(cb));
    comboDel.setEditorData(cb, comboModel.index(0, 0));
    comboDel.updateEditorGeometry(cb, opt, comboModel.index(0, 0));
    comboDel.setModelData(cb, &comboModel, comboModel.index(0, 0));
    QCOMPARE(comboModel.data(comboModel.index(0, 0)).toString(), QStringLiteral("A"));

    // Change description + material color to exercise style helpers
    auto *desc = ed.findChild<QPlainTextEdit *>();
    if (desc)
        desc->setPlainText(QStringLiteral("edited by test"));

    for (QTableWidget *t : ed.findChildren<QTableWidget *>()) {
        if (t->columnCount() < 9)
            continue;
        if (!t->horizontalHeaderItem(8)
            || !t->horizontalHeaderItem(8)->text().contains(QStringLiteral("Color")))
            continue;
        tabs->setCurrentWidget(t);
        if (t->rowCount() > 0) {
            t->setItem(0, 8, new QTableWidgetItem(QStringLiteral("00ff00")));
            t->setItem(0, 8, new QTableWidgetItem(QStringLiteral("=metal_color")));
        }
        break;
    }

    QPushButton *recompute2 = findButtonByText(&ed, QStringLiteral("Recompute"));
    if (recompute2)
        QTest::mouseClick(recompute2, Qt::LeftButton);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ed.setFilePath(dir.filePath(QStringLiteral("tabs.xml")));
    auto *box = ed.findChild<QDialogButtonBox *>();
    QTest::mouseClick(box->button(QDialogButtonBox::Save), Qt::LeftButton);
    QVERIFY(QFileInfo::exists(ed.filePath()));

    // Non-Escape key falls through
    QKeyEvent tabKey(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(&ed, &tabKey);
}

void StackupEditorTest::importXmlViaTestingHook_andResolveAdsScript()
{
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY(!xmlPath.isEmpty());

    StackupEditor ed;
    ed.setAttribute(Qt::WA_DontShowOnScreen, true);
    ed.show();

    QVERIFY(ed.testLoadImportedXml(xmlPath, QStringLiteral("golden")));
    const Substrate s = ed.substrate();
    QVERIFY(s.materials().size() > 5);
    QVERIFY(s.layers().size() > 5);

    const QString ads = ed.testResolveAdsConvertScript();
    QVERIFY(ads.contains(QStringLiteral("ads_convert.py")));

    // Host python resolution should return something non-crashy (may be empty).
    ed.testResolveHostPython();

    QString outTxt, errTxt;
    // Exercise process launch path; exit code may be non-zero for unknown args.
    ed.testRunAdsConvert(QStringList{QStringLiteral("--help")}, &outTxt, &errTxt);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.filePath(QStringLiteral("imported_roundtrip.xml"));
    QVERIFY(ed.testSaveToPath(out));
    QVERIFY(QFileInfo::exists(out));
    QVERIFY(!ed.isModified());
}
