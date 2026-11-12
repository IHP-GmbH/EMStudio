/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_emmarkup.h"

#include <QtTest/QtTest>
#include <QTabWidget>
#include <QPushButton>
#include <QLabel>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QAction>
#include <cmath>

#include "emexport.h"
#include "emmarkup.h"
#include "emmarkuppanel.h"
#include "gdslayout.h"
#include "layoutview.h"
#include "mainwindow.h"
#include "test_utils.h"

namespace {

using namespace GdsTestWriter;

QVector<QPoint> rectDbu(int x1, int y1, int x2, int y2)
{
    return {QPoint(x1, y1), QPoint(x2, y1), QPoint(x2, y2), QPoint(x1, y2), QPoint(x1, y1)};
}

QByteArray boundary(int layer, const QVector<QPoint> &pts, const QString &prop200 = QString())
{
    QByteArray b = record(0x08, 0x00) + record(0x0D, 0x02, int16(layer)) + record(0x0E, 0x02, int16(0)) + xy(pts);
    if (!prop200.isEmpty())
        b += record(0x2B, 0x02, int16(200)) + record(0x2C, 0x06, prop200.toLatin1());
    return b + record(0x11, 0x00);
}

/*! Layout as klayout_em_export leaves it: a Metal1 line, port 1 with settings, the EM region box,
 *  and a plain EMStudio marker on 202 (no settings: not a markup port). 1 nm units. */
QByteArray annotatedLibrary()
{
    QByteArray g;
    g += record(0x00, 0x02, int16(600));
    g += record(0x01, 0x02, QByteArray(24, '\0'));
    g += record(0x02, 0x06, "LIB");
    g += record(0x05, 0x02, QByteArray(24, '\0'));
    g += record(0x06, 0x06, "TOP");
    g += boundary(8, rectDbu(0, 0, 100000, 10000));
    g += boundary(201, rectDbu(0, 0, 2000, 10000), QStringLiteral("port:dir=Z;from=Metal1;to=TopMetal2;z0=50;v=1"));
    g += boundary(202, rectDbu(98000, 0, 100000, 10000));
    g += boundary(800, rectDbu(-5000, -5000, 105000, 15000));
    g += record(0x07, 0x00);
    g += record(0x04, 0x00);
    return g;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(data) == data.size();
}

double areaOf(const QPolygonF &p)
{
    double a = 0.0;
    for (int i = 0; i < p.size(); ++i) {
        const QPointF &u = p.at(i);
        const QPointF &v = p.at((i + 1) % p.size());
        a += u.x() * v.y() - v.x() * u.y();
    }
    return std::abs(a) / 2.0;
}

double totalArea(const QVector<QPolygonF> &polys)
{
    double a = 0.0;
    for (const QPolygonF &p : polys)
        a += areaOf(p);
    return a;
}

QString modelText(const QString &gds, const QString &xml)
{
    return QStringLiteral(
               "from openEMS import openEMS\n"
               "gds_filename = \"%1\"\n"
               "cellname = \"TOP\"\n"
               "XML_filename = \"%2\"\n"
               "simulation_ports = simulation_setup.all_simulation_ports()\n"
               "allpolygons = gds_reader.read_gds(gds_filename, layernumbers, purposelist=[0], "
               "metals_list=metals_list, cellname=cellname)\n")
        .arg(QDir::fromNativeSeparators(gds), QDir::fromNativeSeparators(xml));
}

QJsonObject readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(f.readAll()).object();
}

QByteArray md5Of(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5);
}

} // namespace

/*!*******************************************************************************************************************
 * \brief Port settings read and written exactly like klayout_em_export's property 200 text.
 **********************************************************************************************************************/
void EmMarkupTest::portSettings_matchKLayoutEmExport()
{
    const QStringList texts = {
        QStringLiteral("port:dir=Z;from=TopMetal2;to=Metal1;z0=50;v=1"),
        QStringLiteral("port:dir=X;target=TopMetal2;z0=50;v=1"),
        QStringLiteral("port:dir=-Z;from=Metal3;to=TopMetal2;z0=25;v=0"),
    };
    for (const QString &text : texts) {
        EmMarkupPort p;
        QVERIFY2(EmMarkupPort::decode(text, &p), qPrintable(text));
        QCOMPARE(p.encode(), text);
    }

    EmMarkupPort inPlane;
    QVERIFY(EmMarkupPort::decode(texts.at(1), &inPlane));
    QVERIFY(!inPlane.isVia());
    QCOMPARE(inPlane.target, QStringLiteral("TopMetal2"));

    EmMarkupPort other;
    QVERIFY(!EmMarkupPort::decode(QStringLiteral("boundary"), &other));
    QVERIFY(!EmMarkupPort::decode(QStringLiteral("port:dir=Z;z0=abc"), &other));
}

/*!*******************************************************************************************************************
 * \brief The GDS reader keeps shape properties; the plugin's ports and region become markup, plain
 *        EMStudio marker layers do not.
 **********************************************************************************************************************/
void EmMarkupTest::gdsReader_keepsPropertiesAndImportsMarkup()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString gds = dir.filePath(QStringLiteral("annotated.gds"));
    QVERIFY(writeFile(gds, annotatedLibrary()));

    QVector<GdsFlatPolygon> polys;
    QString err;
    QVERIFY2(GdsLayout::flattenTopCell(gds, QStringLiteral("TOP"), &polys, &err), qPrintable(err));
    QCOMPARE(polys.size(), 4);
    int withProps = 0;
    for (const GdsFlatPolygon &p : polys) {
        if (p.layer == 201) {
            QCOMPARE(p.properties.value(200), QStringLiteral("port:dir=Z;from=Metal1;to=TopMetal2;z0=50;v=1"));
            ++withProps;
        } else {
            QVERIFY(p.properties.isEmpty());
        }
    }
    QCOMPARE(withProps, 1);

    QVERIFY(EmMarkup::layoutHasMarkup(polys));
    QStringList warnings;
    const EmMarkup m = EmMarkup::fromLayout(polys, &warnings);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    QVERIFY(m.hasRegion);
    QCOMPARE(m.regionUm, QRectF(QPointF(-5, -5), QPointF(105, 15)));
    QCOMPARE(m.ports.size(), 1);
    const EmMarkupPort &p1 = m.ports.first();
    QCOMPARE(p1.number, 1);
    QCOMPARE(p1.fromLayer, QStringLiteral("Metal1"));
    QCOMPARE(p1.toLayer, QStringLiteral("TopMetal2"));
    QCOMPARE(p1.shapesUm.size(), 1);
    QCOMPARE(p1.shapesUm.first().boundingRect(), QRectF(QPointF(0, 0), QPointF(2, 10)));

    // A layout without the plugin's markup has nothing to import.
    QVector<GdsFlatPolygon> plain;
    for (const GdsFlatPolygon &p : polys)
        if (p.layer == 8 || p.layer == 202)
            plain << p;
    QVERIFY(!EmMarkup::layoutHasMarkup(plain));
    QVERIFY(EmMarkup::fromLayout(plain).isEmpty());
}

void EmMarkupTest::sidecar_roundTrip()
{
    QCOMPARE(QFileInfo(EmMarkup::sidecarPath(QStringLiteral("C:/models/inductor.py"))).fileName(),
             QStringLiteral("inductor.markup.json"));
    QVERIFY(EmMarkup::sidecarPath(QString()).isEmpty());

    EmMarkup m;
    m.layoutFile = QStringLiteral("inductor.gds");
    m.cell = QStringLiteral("L1");
    m.hasRegion = true;
    m.regionUm = QRectF(QPointF(-10, -20), QPointF(30, 40));
    EmMarkupPort p;
    QVERIFY(EmMarkupPort::decode(QStringLiteral("port:dir=-X;target=Metal5;z0=25;v=1"), &p));
    p.number = 3;
    p.shapesUm << QPolygonF(QVector<QPointF>{QPointF(0, 0), QPointF(0, 5)});   // zero-width port wall
    m.ports << p;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("inductor.markup.json"));
    QString err;
    QVERIFY2(m.save(path, &err), qPrintable(err));
    EmMarkup back;
    QVERIFY2(EmMarkup::load(path, &back, &err), qPrintable(err));
    QVERIFY(back == m);

    QVERIFY(writeFile(path, "not json"));
    QVERIFY(!EmMarkup::load(path, &back, &err));
    QVERIFY(!err.isEmpty());
}

void EmMarkupTest::problems_reportBadPorts()
{
    EmMarkup m;
    m.hasRegion = true;
    m.regionUm = QRectF(0, 0, 10, 10);
    EmMarkupPort same;
    QVERIFY(EmMarkupPort::decode(QStringLiteral("port:dir=Z;from=Metal1;to=Metal1;z0=50;v=1"), &same));
    same.number = 1;
    same.shapesUm << QPolygonF(QRectF(1, 1, 1, 1));
    EmMarkupPort outside;
    QVERIFY(EmMarkupPort::decode(QStringLiteral("port:dir=X;target=Metal9;z0=50;v=1"), &outside));
    outside.number = 1;
    outside.shapesUm << QPolygonF(QRectF(50, 50, 1, 1));
    m.ports << same << outside;

    const QString text = m.problems({QStringLiteral("Metal1"), QStringLiteral("TopMetal2")}).join(QLatin1Char('\n'));
    QVERIFY2(text.contains(QStringLiteral("from and to are the same layer")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("number used twice")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("'Metal9' is not a metal")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("outside the EM region")), qPrintable(text));
    QCOMPARE(m.nextFreePortNumber(), 2);
}

/*!*******************************************************************************************************************
 * \brief The markup is drawn on top of the layout without becoming layout geometry, survives a
 *        layout reload and is left out of Iso3D.
 **********************************************************************************************************************/
void EmMarkupTest::layoutView_drawsMarkupAsOverlay()
{
    LayoutView view;
    view.resize(600, 400);
    GdsFlatPolygon metal;
    metal.layer = 8;
    metal.pointsUm = QPolygonF(QRectF(0, 0, 100, 10));
    LayoutView::LayerStyle st;
    st.name = QStringLiteral("Metal1");
    st.kind = QStringLiteral("conductor");
    st.color = Qt::blue;
    view.setPolygons({metal}, {{8, st}});
    QCOMPARE(view.markupItemCount(), 0);

    EmMarkup m;
    m.hasRegion = true;
    m.regionUm = QRectF(-5, -5, 110, 20);
    EmMarkupPort p;
    QVERIFY(EmMarkupPort::decode(QStringLiteral("port:dir=Z;from=Metal1;to=TopMetal2;z0=50;v=1"), &p));
    p.number = 1;
    p.shapesUm << QPolygonF(QRectF(0, 0, 2, 10));
    m.ports << p;

    view.setMarkup(m);
    const int items = view.markupItemCount();
    QVERIFY(items >= 4);   // region box + label, port shape + label
    QCOMPARE(view.drawnPolygons().size(), 1);

    view.setPolygons({metal}, {{8, st}});
    QCOMPARE(view.markupItemCount(), items);

    view.setViewMode(LayoutView::ViewMode::Iso3D);
    QCOMPARE(view.markupItemCount(), 0);
    view.setViewMode(LayoutView::ViewMode::Top2D);
    QCOMPARE(view.markupItemCount(), items);

    view.setMarkup(EmMarkup());
    QCOMPARE(view.markupItemCount(), 0);
}

/*!*******************************************************************************************************************
 * \brief Layers | Markup tabs: a layout marked up with klayout_em_export is imported on load, saved
 *        beside the model (the GDS stays untouched) and loaded from there next time.
 **********************************************************************************************************************/
void EmMarkupTest::mainWindow_markupTab_importsSavesAndReloads()
{
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "golden/SG13G2_200um.xml missing");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString gds = dir.filePath(QStringLiteral("annotated.gds"));
    QVERIFY(writeFile(gds, annotatedLibrary()));
    const QByteArray gdsHash = md5Of(gds);
    const QString model = dir.filePath(QStringLiteral("annotated.py"));
    QVERIFY(writeFile(model, QStringLiteral(
                          "from openEMS import openEMS\n"
                          "gds_filename = \"%1\"\n"
                          "cellname = \"TOP\"\n"
                          "XML_filename = \"%2\"\n"
                          "simulation_ports = simulation_setup.all_simulation_ports()\n"
                          "allpolygons = gds_reader.read_gds(gds_filename, layernumbers, purposelist=[0], "
                          "metals_list=metals_list, cellname=cellname)\n")
                          .arg(QDir::fromNativeSeparators(gds), QDir::fromNativeSeparators(xmlPath)).toUtf8()));
    const QString sidecar = dir.filePath(QStringLiteral("annotated.markup.json"));

    {
        MainWindow w;
        w.setAttribute(Qt::WA_DontShowOnScreen, true);
        w.resize(1200, 800);
        w.show();
        QTest::qWait(30);

        auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabLayoutSide"));
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(tabs->tabText(0), QStringLiteral("Layers"));
        QCOMPARE(tabs->tabText(1), QStringLiteral("Markup"));
        QCOMPARE(tabs->currentIndex(), 0);

        QVERIFY(w.loadPythonModel(model));
        auto *panel = w.findChild<EmMarkupPanel *>();
        QVERIFY(panel);
        QCOMPARE(panel->markup().ports.size(), 1);
        QVERIFY(panel->markup().hasRegion);
        auto *status = panel->findChild<QLabel *>(QStringLiteral("lblMarkupStatus"));
        QVERIFY(status);
        QVERIFY2(status->text().contains(QStringLiteral("klayout_em_export")), qPrintable(status->text()));
        auto *view = w.findChild<LayoutView *>();
        QVERIFY(view);
        QVERIFY(view->markupItemCount() > 0);

        // No separate markup Save: the model's Save (Ctrl+S) writes the sidecar.
        QVERIFY(!panel->findChild<QPushButton *>(QStringLiteral("btnMarkupSave")));
        auto *save = w.findChild<QAction *>(QStringLiteral("actionSave"));
        QVERIFY(save);
        QVERIFY(!QFileInfo::exists(sidecar));
        save->trigger();
        QVERIFY(QFileInfo::exists(sidecar));
    }
    QCOMPARE(md5Of(gds), gdsHash);

    // Next session: the sidecar wins; an edit there is what comes back.
    EmMarkup saved;
    QVERIFY(EmMarkup::load(sidecar, &saved));
    saved.ports.first().z0 = 25.0;
    QVERIFY(saved.save(sidecar));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.resize(1200, 800);
    w.show();
    QTest::qWait(30);
    QVERIFY(w.loadPythonModel(model));
    auto *panel = w.findChild<EmMarkupPanel *>();
    QVERIFY(panel);
    QCOMPARE(panel->markup().ports.size(), 1);
    QCOMPARE(panel->markup().ports.first().z0, 25.0);
    auto *status = panel->findChild<QLabel *>(QStringLiteral("lblMarkupStatus"));
    QVERIFY2(status->text().startsWith(QStringLiteral("Loaded from")), qPrintable(status->text()));
}

/*!*******************************************************************************************************************
 * \brief Overlapping shapes become one polygon, the region clips them, and a hole is cut open (GDS
 *        boundaries have none) without losing area.
 **********************************************************************************************************************/
void EmMarkupTest::clipAndMerge_mergesClipsAndOpensHoles()
{
    const QVector<QPolygonF> overlapping = {QPolygonF(QRectF(0, 0, 10, 2)), QPolygonF(QRectF(8, 0, 10, 2))};
    QVector<QPolygonF> merged = EmExport::clipAndMerge(overlapping, QRectF(), true);
    QCOMPARE(merged.size(), 1);
    QVERIFY(std::abs(totalArea(merged) - 36.0) < 1e-6);
    QCOMPARE(EmExport::clipAndMerge(overlapping, QRectF(), false).size(), 2);

    const QVector<QPolygonF> clipped = EmExport::clipAndMerge(overlapping, QRectF(QPointF(-1, -1), QPointF(5, 1)), true);
    QCOMPARE(clipped.size(), 1);
    QVERIFY(std::abs(totalArea(clipped) - 5.0) < 1e-6);
    QCOMPARE(clipped.first().boundingRect(), QRectF(QPointF(0, 0), QPointF(5, 1)));

    // A ring: 10x10 with a 4x4 opening, drawn as four bars; one polygon with a cut line to the opening.
    const QVector<QPolygonF> ring = {QPolygonF(QRectF(0, 0, 10, 3)), QPolygonF(QRectF(0, 7, 10, 3)),
                                     QPolygonF(QRectF(0, 0, 3, 10)), QPolygonF(QRectF(7, 0, 3, 10))};
    const QVector<QPolygonF> opened = EmExport::clipAndMerge(ring, QRectF(), true);
    QString dump;
    for (const QPolygonF &p : opened) {
        dump += QStringLiteral("[");
        for (const QPointF &pt : p)
            dump += QStringLiteral("(%1,%2)").arg(pt.x()).arg(pt.y());
        dump += QStringLiteral("] ");
    }
    QVERIFY2(opened.size() == 1, qPrintable(dump));
    QVERIFY2(std::abs(totalArea(opened) - 84.0) < 1e-6, qPrintable(QString::number(totalArea(opened)) + QLatin1Char(' ') + dump));
    for (const QPolygonF &p : opened) {
        QVERIFY(p.size() >= 3);
        QVERIFY(!p.containsPoint(QPointF(5, 5), Qt::OddEvenFill));
    }
}

/*!*******************************************************************************************************************
 * \brief The EM layout has the stackup layers clipped to the region, the ports renumbered onto 201..,
 *        nothing else; the .simcfg and port map match klayout_em_export; setupEM's own settings stay.
 **********************************************************************************************************************/
void EmMarkupTest::export_writesGdsSimcfgAndPortmap()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = dir.filePath(QStringLiteral("annotated.gds"));
    QVERIFY(writeFile(src, annotatedLibrary()));
    QVector<GdsFlatPolygon> polys;
    QVERIFY(GdsLayout::flattenTopCell(src, QStringLiteral("TOP"), &polys));

    EmMarkup m = EmMarkup::fromLayout(polys);
    m.regionUm = QRectF(QPointF(-5, -5), QPointF(60, 15));       // cuts the Metal1 line at x = 60
    // Port 7: an edge port on the region's right side (a port wall), in-plane on Metal1.
    EmMarkupPort wall;
    QVERIFY(EmMarkupPort::decode(QStringLiteral("port:dir=-X;target=Metal1;z0=50;v=0"), &wall));
    wall.number = 7;
    wall.shapesUm << QPolygonF(QVector<QPointF>{QPointF(60, 0), QPointF(60, 10)});
    // Port 9: outside the region, left out.
    EmMarkupPort outside = wall;
    outside.number = 9;
    outside.shapesUm = {QPolygonF(QRectF(90, 0, 2, 10))};
    m.ports << wall << outside;

    const QString out = dir.filePath(QStringLiteral("model_em.gds"));
    {
        QFile f(dir.filePath(QStringLiteral("model_em.simcfg")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"application": "setupEM", "data_format": "1.0",
                    "saved_values": {"unit": 1e-06, "fstop": 50e9, "boundary": "PEC"}, "ports": []})");
    }
    EmExport::Options opt;
    opt.outGds = out;
    opt.cell = QStringLiteral("TOP");
    opt.substrateFile = dir.filePath(QStringLiteral("stack.xml"));
    opt.stackLayers = {8};
    opt.purposes = {0};
    opt.metals = QStringList{QStringLiteral("Metal1"), QStringLiteral("TopMetal2")};
    const EmExport::Result r = EmExport::run(polys, m, opt);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.ports.size(), 2);
    QCOMPARE(r.ports.at(0).port.number, 1);
    QCOMPARE(r.ports.at(1).number, 2);
    QCOMPARE(r.ports.at(1).port.number, 7);
    QCOMPARE(r.portWalls, QStringList{QStringLiteral("xmax")});
    QVERIFY(r.updatedSimcfg);
    QVERIFY2(r.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("P9 is outside")), qPrintable(r.warnings.join('\n')));

    QVector<GdsFlatPolygon> back;
    QString err;
    QVERIFY2(GdsLayout::flattenTopCell(out, QStringLiteral("TOP"), &back, &err), qPrintable(err));
    QMap<int, QRectF> boxes;
    for (const GdsFlatPolygon &p : back)
        boxes[p.layer] = boxes.value(p.layer).isNull() ? p.pointsUm.boundingRect() : boxes[p.layer].united(p.pointsUm.boundingRect());
    QCOMPARE(boxes.keys(), (QList<int>{8, 201, 202}));
    QCOMPARE(boxes.value(8), QRectF(QPointF(0, 0), QPointF(60, 10)));
    QCOMPARE(boxes.value(201), QRectF(QPointF(0, 0), QPointF(2, 10)));
    QCOMPARE(boxes.value(202).left(), 60.0);
    QCOMPARE(boxes.value(202).width(), 0.0);

    const QJsonObject simcfg = readJson(r.simcfg);
    const QJsonObject saved = simcfg.value(QStringLiteral("saved_values")).toObject();
    QCOMPARE(saved.value(QStringLiteral("fstop")).toDouble(), 50e9);
    QCOMPARE(saved.value(QStringLiteral("cellname")).toString(), QStringLiteral("TOP"));
    QCOMPARE(saved.value(QStringLiteral("GdsFile")).toString(), QFileInfo(out).absoluteFilePath());
    const QJsonArray boundary = saved.value(QStringLiteral("boundary")).toArray();
    QCOMPARE(boundary.size(), 6);
    QCOMPARE(boundary.at(0).toString(), QStringLiteral("PEC"));
    QCOMPARE(boundary.at(1).toString(), QStringLiteral("PORTWALL"));
    const QJsonArray ports = simcfg.value(QStringLiteral("ports")).toArray();
    QCOMPARE(ports.size(), 2);
    const QJsonObject p1 = ports.at(0).toObject();
    QCOMPARE(p1.value(QStringLiteral("source_layernum")).toInt(), 201);
    QCOMPARE(p1.value(QStringLiteral("from_layername")).toString(), QStringLiteral("Metal1"));
    QCOMPARE(p1.value(QStringLiteral("to_layername")).toString(), QStringLiteral("TopMetal2"));
    QVERIFY(p1.value(QStringLiteral("target_layername")).isNull());
    QCOMPARE(p1.value(QStringLiteral("direction")).toString(), QStringLiteral("Z"));
    const QJsonObject p2 = ports.at(1).toObject();
    QCOMPARE(p2.value(QStringLiteral("target_layername")).toString(), QStringLiteral("Metal1"));
    QCOMPARE(p2.value(QStringLiteral("direction")).toString(), QStringLiteral("-X"));
    QCOMPARE(p2.value(QStringLiteral("voltage")).toDouble(), 0.0);

    const QJsonArray rows = readJson(r.portmap).value(QStringLiteral("ports")).toArray();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(1).toObject().value(QStringLiteral("layout_port")).toInt(), 7);
    QCOMPARE(rows.at(1).toObject().value(QStringLiteral("layer")).toInt(), 202);
}

/*!*******************************************************************************************************************
 * \brief An armed port tool turns a drag into a rectangle snapped to the layout corners; a thin drag
 *        gives a line; the tool is off afterwards.
 **********************************************************************************************************************/
void EmMarkupTest::layoutView_drawTool_emitsSnappedRect()
{
    LayoutView view;
    view.resize(600, 400);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    GdsFlatPolygon metal;
    metal.layer = 8;
    metal.pointsUm = QPolygonF(QRectF(0, 0, 100, 10));
    LayoutView::LayerStyle st;
    st.name = QStringLiteral("Metal1");
    st.kind = QStringLiteral("conductor");
    st.color = Qt::blue;
    view.setPolygons({metal}, {{8, st}});

    qRegisterMetaType<LayoutView::MarkupTool>();
    QSignalSpy drawn(&view, &LayoutView::markupRectDrawn);
    QSignalSpy toolChanged(&view, &LayoutView::markupToolChanged);
    auto viewPos = [&view](qreal xUm, qreal yUm) { return view.mapFromScene(QPointF(xUm, -yUm)); };
    auto drag = [&](const QPoint &a, const QPoint &b) {
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(view.viewport(), (a + b) / 2);
        QTest::mouseMove(view.viewport(), b);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, b);
    };

    view.setMarkupTool(LayoutView::MarkupTool::Port);
    QCOMPARE(view.markupTool(), LayoutView::MarkupTool::Port);
    drag(viewPos(0, 0) + QPoint(2, 1), viewPos(5, 10) + QPoint(0, -1));   // near the line's left end
    QCOMPARE(drawn.size(), 1);
    QCOMPARE(drawn.at(0).at(0).value<LayoutView::MarkupTool>(), LayoutView::MarkupTool::Port);
    const QRectF rect = drawn.at(0).at(1).toRectF();
    QCOMPARE(rect.left(), 0.0);
    QCOMPARE(rect.top(), 0.0);
    QCOMPARE(rect.bottom(), 10.0);
    QCOMPARE(view.markupTool(), LayoutView::MarkupTool::None);
    QVERIFY(toolChanged.size() >= 2);

    view.setMarkupTool(LayoutView::MarkupTool::Port);
    drag(viewPos(100, 0) + QPoint(1, 0), viewPos(100, 10) + QPoint(1, 0));   // thin: an edge port
    QCOMPARE(drawn.size(), 2);
    const QRectF line = drawn.at(1).at(1).toRectF();
    QCOMPARE(line.width(), 0.0);
    QCOMPARE(line.left(), 100.0);
    QCOMPARE(line.height(), 10.0);

    // A click (no drag) and Esc draw nothing.
    view.setMarkupTool(LayoutView::MarkupTool::Region);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, viewPos(50, 5));
    QCOMPARE(drawn.size(), 2);
    QTest::keyClick(&view, Qt::Key_Escape);
    QCOMPARE(view.markupTool(), LayoutView::MarkupTool::None);
}

/*!*******************************************************************************************************************
 * \brief File → Export EM Layout writes <model>_em.gds / .simcfg / _portmap.json and fills the Ports table; the
 *        model's Layout File and the source layout stay as they were.
 **********************************************************************************************************************/
void EmMarkupTest::mainWindow_exportUsesEmLayoutInModel()
{
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "golden/SG13G2_200um.xml missing");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString gds = dir.filePath(QStringLiteral("annotated.gds"));
    QVERIFY(writeFile(gds, annotatedLibrary()));
    const QByteArray gdsHash = md5Of(gds);
    const QString model = dir.filePath(QStringLiteral("line.py"));
    QVERIFY(writeFile(model, modelText(gds, xmlPath).toUtf8()));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.resize(1200, 800);
    w.show();
    QTest::qWait(30);
    QVERIFY(w.loadPythonModel(model));
    auto *panel = w.findChild<EmMarkupPanel *>();
    QVERIFY(panel);
    QCOMPARE(panel->markup().ports.size(), 1);

    // A second port drawn in the layout: over the line's right end.
    auto *view = w.findChild<LayoutView *>();
    QVERIFY(view);
    emit view->markupRectDrawn(LayoutView::MarkupTool::Port, QRectF(QPointF(98, 0), QPointF(100, 10)));
    QCOMPARE(panel->markup().ports.size(), 2);
    const EmMarkupPort *p3 = panel->markup().port(3);
    QVERIFY(p3);
    QCOMPARE(p3->direction, QStringLiteral("Z"));
    QCOMPARE(p3->fromLayer, QStringLiteral("Metal1"));

    QVERIFY(w.findChild<QAction *>(QStringLiteral("actionExportEmLayout")));
    const QString em = dir.filePath(QStringLiteral("line_em.gds"));
    QVERIFY(w.testExportEmLayout(em));
    QVERIFY(QFileInfo::exists(em));
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("line_em.simcfg"))));
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("line_em_portmap.json"))));
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("line.markup.json"))));
    QCOMPARE(md5Of(gds), gdsHash);

    auto *gdsEdit = w.findChild<QLineEdit *>(QStringLiteral("txtGdsFile"));
    QVERIFY(gdsEdit);
    // The model keeps its layout: Run switches to the EM layout by itself.
    QCOMPARE(QFileInfo(gdsEdit->text()).absoluteFilePath(), QFileInfo(gds).absoluteFilePath());
    QCOMPARE(w.testPortsRowCount(), 2);
    // The markup still belongs to the source layout, so the export can be repeated.
    QCOMPARE(QFileInfo(panel->markup().layoutFile).absoluteFilePath(), QFileInfo(gds).absoluteFilePath());
    QCOMPARE(view->markupItemCount() > 0, true);
}

/*!*******************************************************************************************************************
 * \brief Markup ports and the model's Ports table stay in step (matched by port number): drawing, edits on
 *        either side and removal carry over.
 **********************************************************************************************************************/
void EmMarkupTest::mainWindow_markupPortsFollowPortsTable()
{
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "golden/SG13G2_200um.xml missing");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString gds = dir.filePath(QStringLiteral("annotated.gds"));
    QVERIFY(writeFile(gds, annotatedLibrary()));
    const QString model = dir.filePath(QStringLiteral("line.py"));
    QVERIFY(writeFile(model, modelText(gds, xmlPath).toUtf8()));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.resize(1200, 800);
    w.show();
    QTest::qWait(30);
    QVERIFY(w.loadPythonModel(model));
    auto *panel = w.findChild<EmMarkupPanel *>();
    auto *view = w.findChild<LayoutView *>();
    auto *ports = w.findChild<QTableWidget *>(QStringLiteral("tblPorts"));
    auto *markupPorts = panel ? panel->findChild<QTableWidget *>(QStringLiteral("tblMarkupPorts")) : nullptr;
    QVERIFY(panel && view && ports && markupPorts);
    QCOMPARE(panel->markup().ports.size(), 1);
    const int rowsBefore = ports->rowCount();

    auto rowOf = [ports](int number) {
        for (int r = 0; r < ports->rowCount(); ++r)
            if (ports->item(r, 0) && ports->item(r, 0)->text().toInt() == number)
                return r;
        return -1;
    };

    // Drawn in the layout: a row in the model.
    emit view->markupRectDrawn(LayoutView::MarkupTool::Port, QRectF(QPointF(98, 0), QPointF(100, 10)));
    QCOMPARE(panel->markup().ports.size(), 2);
    QCOMPARE(ports->rowCount(), rowsBefore + 1);
    int row = rowOf(3);
    QVERIFY(row >= 0);
    QCOMPARE(qobject_cast<QComboBox *>(ports->cellWidget(row, 3))->currentText(), QStringLiteral("203"));
    QCOMPARE(qobject_cast<QComboBox *>(ports->cellWidget(row, 6))->currentText(), QStringLiteral("z"));

    // Markup edit → model row.
    auto *z0Box = qobject_cast<QDoubleSpinBox *>(markupPorts->cellWidget(1, 4));
    QVERIFY(z0Box);
    z0Box->setValue(25.0);
    QCOMPARE(ports->item(rowOf(3), 2)->text(), QStringLiteral("25"));

    // Model row edit → markup.
    row = rowOf(3);
    ports->item(row, 1)->setText(QStringLiteral("0"));
    QCOMPARE(panel->markup().port(3)->voltage, 0.0);
    qobject_cast<QComboBox *>(ports->cellWidget(row, 6))->setCurrentText(QStringLiteral("-z"));
    QCOMPARE(panel->markup().port(3)->direction, QStringLiteral("-Z"));
    QCOMPARE(view->markup().port(3)->direction, QStringLiteral("-Z"));

    // Removed in the model → gone from the markup.
    ports->setCurrentCell(rowOf(3), 0);
    auto *removeRow = w.findChild<QPushButton *>(QStringLiteral("btnReomovePort"));
    QVERIFY(removeRow);
    removeRow->click();
    QVERIFY(!panel->markup().port(3));
    QCOMPARE(ports->rowCount(), rowsBefore);

    // Removed in the markup → gone from the model.
    emit view->markupRectDrawn(LayoutView::MarkupTool::Port, QRectF(QPointF(98, 0), QPointF(100, 10)));
    QVERIFY(rowOf(3) >= 0);
    markupPorts->selectRow(1);
    auto *removePort = panel->findChild<QPushButton *>(QStringLiteral("btnMarkupRemovePort"));
    QVERIFY(removePort && removePort->isEnabled());
    removePort->click();
    QVERIFY(!panel->markup().port(3));
    QCOMPARE(rowOf(3), -1);
}

namespace {

/*! Top view of a Metal1 line 0..100 x 0..10 µm. */
void setLineLayout(LayoutView &view)
{
    view.resize(600, 400);
    view.show();
    GdsFlatPolygon metal;
    metal.layer = 8;
    metal.pointsUm = QPolygonF(QRectF(0, 0, 100, 10));
    LayoutView::LayerStyle st;
    st.name = QStringLiteral("Metal1");
    st.kind = QStringLiteral("conductor");
    st.color = Qt::blue;
    view.setPolygons({metal}, {{8, st}});
}

} // namespace

/*!*******************************************************************************************************************
 * \brief A picked markup port shows handles: an edge handle resizes it (snapping to the layout), a drag inside
 *        moves it, Delete asks to remove it.
 **********************************************************************************************************************/
void EmMarkupTest::layoutView_portHandlesResizeAndMove()
{
    LayoutView view;
    setLineLayout(view);
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    EmMarkup markup;
    EmMarkupPort port;
    port.number = 1;
    port.direction = QStringLiteral("Z");
    port.fromLayer = QStringLiteral("Metal1");
    port.toLayer = QStringLiteral("TopMetal2");
    port.shapesUm << QPolygonF(QRectF(QPointF(10, 2), QPointF(20, 8)));
    markup.ports << port;
    view.setMarkup(markup);

    QSignalSpy selected(&view, &LayoutView::markupPortSelected);
    QSignalSpy reshaped(&view, &LayoutView::markupPortReshaped);
    QSignalSpy removeAsked(&view, &LayoutView::markupPortRemoveRequested);
    auto viewPos = [&view](qreal xUm, qreal yUm) { return view.mapFromScene(QPointF(xUm, -yUm)); };
    auto drag = [&](const QPoint &a, const QPoint &b) {
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(view.viewport(), (a + b) / 2);
        QTest::mouseMove(view.viewport(), b);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, b);
    };

    // A click picks the port.
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, viewPos(15, 5));
    QCOMPARE(view.selectedMarkupPort(), 1);
    QCOMPARE(selected.size(), 1);
    QCOMPARE(selected.at(0).at(0).toInt(), 1);
    QCOMPARE(reshaped.size(), 0);
    QCOMPARE(view.markupPortHandleAt(viewPos(20, 5)), 2);
    QCOMPARE(view.markupPortHandleAt(viewPos(10, 8)), 1 | 4);
    QCOMPARE(view.markupPortHandleAt(viewPos(15, 5)), 16);

    // Right edge dragged to near the line's top edge... x: free, then the top edge onto the line (y = 10).
    drag(viewPos(20, 5), viewPos(30, 5));
    QCOMPARE(reshaped.size(), 1);
    QCOMPARE(reshaped.at(0).at(0).toInt(), 1);
    QRectF r = reshaped.at(0).at(1).toRectF();
    QVERIFY2(std::abs(r.left() - 10) < 1e-9 && std::abs(r.right() - 30) < 0.2 && r.top() == 2 && r.bottom() == 8,
             qPrintable(QStringLiteral("%1 %2 %3 %4").arg(r.left()).arg(r.top()).arg(r.right()).arg(r.bottom())));
    drag(viewPos(15, 8), viewPos(15, 10) + QPoint(0, 2));
    QCOMPARE(reshaped.size(), 2);
    r = reshaped.at(1).at(1).toRectF();
    QCOMPARE(r.bottom(), 10.0);   // snapped onto the line's top edge
    QCOMPARE(r.top(), 2.0);

    // A drag inside moves it; the size stays.
    drag(viewPos(15, 5), viewPos(25, 5));
    QCOMPARE(reshaped.size(), 3);
    r = reshaped.at(2).at(1).toRectF();
    QVERIFY(std::abs(r.width() - 10.0) < 1e-9);
    QVERIFY(std::abs(r.height() - 6.0) < 1e-9);
    QVERIFY(r.left() > 14.0);

    QTest::keyClick(view.viewport(), Qt::Key_Delete);
    QTest::keyClick(&view, Qt::Key_Delete);
    QVERIFY(removeAsked.size() >= 1);
    QCOMPARE(removeAsked.at(0).at(0).toInt(), 1);

    // A click beside it clears the pick.
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, viewPos(60, 30));
    QCOMPARE(view.selectedMarkupPort(), 0);
}

/*!*******************************************************************************************************************
 * \brief The Ruler button arms the measure mode; its points snap to layout vertices.
 **********************************************************************************************************************/
void EmMarkupTest::layoutView_rulerSnapsToLayout()
{
    LayoutView view;
    setLineLayout(view);
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *ruler = view.findChild<QToolButton *>(QStringLiteral("layoutViewRulerBtn"));
    QVERIFY(ruler);
    QVERIFY(ruler->isVisible());
    QSignalSpy armed(&view, &LayoutView::measureArmedChanged);
    QSignalSpy measured(&view, &LayoutView::measureChanged);
    ruler->click();
    QVERIFY(view.isMeasureArmed());
    QCOMPARE(armed.size(), 1);

    auto viewPos = [&view](qreal xUm, qreal yUm) { return view.mapFromScene(QPointF(xUm, -yUm)); };
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, viewPos(0, 0) + QPoint(2, -2));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, viewPos(100, 10) + QPoint(-2, 2));
    QVERIFY(!measured.isEmpty());
    const QList<QVariant> last = measured.last();
    QCOMPARE(last.at(0).toBool(), true);
    QCOMPARE(last.at(1).toDouble(), 100.0);
    QCOMPARE(last.at(2).toDouble(), 10.0);

    QTest::keyClick(&view, Qt::Key_Escape);
    QVERIFY(!view.isMeasureArmed());
    QVERIFY(!ruler->isChecked());
}

/*!*******************************************************************************************************************
 * \brief A port resized in the layout keeps its settings and Ports-table row; the export renumbers the markup like
 *        the EM layout, so markup and table keep matching.
 **********************************************************************************************************************/
void EmMarkupTest::mainWindow_reshapeAndExportRenumbersMarkup()
{
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "golden/SG13G2_200um.xml missing");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString gds = dir.filePath(QStringLiteral("annotated.gds"));
    QVERIFY(writeFile(gds, annotatedLibrary()));
    const QString model = dir.filePath(QStringLiteral("line.py"));
    QVERIFY(writeFile(model, modelText(gds, xmlPath).toUtf8()));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.resize(1200, 800);
    w.show();
    QTest::qWait(30);
    QVERIFY(w.loadPythonModel(model));
    auto *panel = w.findChild<EmMarkupPanel *>();
    auto *view = w.findChild<LayoutView *>();
    auto *ports = w.findChild<QTableWidget *>(QStringLiteral("tblPorts"));
    QVERIFY(panel && view && ports);

    emit view->markupRectDrawn(LayoutView::MarkupTool::Port, QRectF(QPointF(98, 0), QPointF(100, 10)));
    QVERIFY(panel->markup().port(3));
    panel->removePort(1);
    QVERIFY(!panel->markup().port(1));
    const int rows = ports->rowCount();

    // Picked in the layout: the panel row follows; resized: shape only.
    emit view->markupPortSelected(3);
    QCOMPARE(panel->selectedPort(), 3);
    const QString settings = panel->markup().port(3)->encode();
    emit view->markupPortReshaped(3, QRectF(QPointF(96, 0), QPointF(100, 10)));
    QCOMPARE(panel->markup().port(3)->shapesUm.size(), 1);
    QCOMPARE(panel->markup().port(3)->shapesUm.first().boundingRect(), QRectF(QPointF(96, 0), QPointF(100, 10)));
    QCOMPARE(panel->markup().port(3)->encode(), settings);
    QCOMPARE(ports->rowCount(), rows);

    QVERIFY(w.testExportEmLayout(dir.filePath(QStringLiteral("line_em.gds"))));
    QCOMPARE(panel->markup().ports.size(), 1);
    QCOMPARE(panel->markup().ports.first().number, 1);
    QCOMPARE(ports->rowCount(), 1);
    QCOMPARE(ports->item(0, 0)->text(), QStringLiteral("1"));
}
