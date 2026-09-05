/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_substrate.h"

#include <QtTest/QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "substrate.h"

void SubstrateTest::parseGoldenXml_andResolve()
{
    const QString path = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!path.isEmpty(), "golden/SG13G2_200um.xml not found");

    Substrate sub;
    QVERIFY(sub.parseXmlFile(path));
    QVERIFY(!sub.materials().isEmpty());
    QVERIFY(!sub.dielectrics().isEmpty());
    QVERIFY(!sub.layers().isEmpty());
    QCOMPARE(sub.substrateOffset(), 183.75);
    QCOMPARE(sub.lengthUnit(), QStringLiteral("um"));

    QString err;
    QVERIFY2(sub.resolve({}, &err), qPrintable(err));

    bool foundM1 = false;
    for (const Layer &lay : sub.layers()) {
        if (lay.name() != QLatin1String("Metal1"))
            continue;
        foundM1 = true;
        QCOMPARE(lay.zmin(), 1.04);
        QCOMPARE(lay.zmax(), 1.46);
    }
    QVERIFY(foundM1);
}

void SubstrateTest::writeRoundTrip_withThermalDerivedAndDescription()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("full_stack.xml"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "<?xml version='1.0' encoding='UTF-8'?>\n"
            << "<Stackup schemaVersion=\"3.1\">\n"
            << "<!-- Created/modified using the XML Stackup Editor in EMStudio -->\n"
            << "<!-- ============================================================ -->\n"
            << "<!-- Round-trip description line 1 -->\n"
            << "<!-- line 2 -->\n"
            << "<!-- ============================================================ -->\n"
            << "  <Variables>\n"
            << "    <Variable Name=\"t_sub\" Value=\"100\" />\n"
            << "    <Variable Name=\"t_ox\" Value=\"=t_sub/10\" />\n"
            << "    <Variable Name=\"label\" Value=\"demo\" Type=\"string\" />\n"
            << "  </Variables>\n"
            << "  <Materials>\n"
            << "    <Material Name=\"Si\" Type=\"Semiconductor\" Permittivity=\"11.9\" "
               "Conductivity=\"2\" ThermalConductivity=\"=150\" "
               "ThermalConductivityTable=\"kSi\" Color=\"01e0ff\"/>\n"
            << "    <Material Name=\"Metal\" Type=\"Conductor\" Conductivity=\"1e7\" Color=\"ff0000\"/>\n"
            << "  </Materials>\n"
            << "  <ELayers LengthUnit=\"um\">\n"
            << "    <Dielectrics>\n"
            << "      <Dielectric Name=\"Ox\" Material=\"Si\" Thickness=\"=t_ox\" />\n"
            << "      <Dielectric Name=\"Sub\" Material=\"Si\" Thickness=\"=t_sub\" />\n"
            << "    </Dielectrics>\n"
            << "    <Layers>\n"
            << "      <Substrate Offset=\"=t_sub\" />\n"
            << "      <Layer Name=\"M1\" Type=\"conductor\" Material=\"Metal\" Layer=\"1\" "
               "Zmin=\"0\" Zmax=\"1\" />\n"
            << "      <Layer Name=\"M2\" Type=\"conductor\" Material=\"Metal\" Layer=\"2\" "
               "Reference=\"M1\" ReferenceEdge=\"top\" Zmin=\"0.5\" Zmax=\"1.5\" />\n"
            << "    </Layers>\n"
            << "    <DerivedLayers>\n"
            << "      <DerivedLayer Name=\"OR12\" Layer=\"99\" Operation=\"OR\">\n"
            << "        <Operand Layer=\"1\" />\n"
            << "        <Operand Layer=\"2\" />\n"
            << "      </DerivedLayer>\n"
            << "      <DerivedLayer Name=\"SZ\" Layer=\"100\" Operation=\"SIZE\" Size=\"0.1\">\n"
            << "        <Operand Layer=\"1\" />\n"
            << "      </DerivedLayer>\n"
            << "    </DerivedLayers>\n"
            << "  </ELayers>\n"
            << "  <Tables>\n"
            << "    <Table Name=\"kSi\">\n"
            << "      <Entry Temperature=\"300\" Value=\"148\" />\n"
            << "      <Entry Temperature=\"400\" Value=\"110\" />\n"
            << "    </Table>\n"
            << "  </Tables>\n"
            << "</Stackup>\n";
    }

    Substrate sub;
    QVERIFY2(sub.parseXmlFile(path), "parse failed");
    QVERIFY(sub.description().contains(QStringLiteral("Round-trip")));
    QCOMPARE(sub.variables().size(), 3);
    QCOMPARE(sub.derivedLayers().size(), 2);
    QCOMPARE(sub.thermalTables().size(), 1);
    QCOMPARE(sub.thermalTables().at(0).points.size(), 2);
    QCOMPARE(sub.substrateOffset(), 100.0);
    QCOMPARE(sub.computeMinimumSchemaVersion(), QStringLiteral("3.1"));

    const QList<StackupVariable> overs = sub.overridableVariables();
    QVERIFY(overs.size() >= 2); // t_sub + label (not computed)

    const QString outPath = dir.filePath(QStringLiteral("rewritten.xml"));
    sub.setDescription(QStringLiteral("Saved by test"));
    QVERIFY(sub.writeXmlFile(outPath));

    Substrate again;
    QVERIFY(again.parseXmlFile(outPath));
    QCOMPARE(again.derivedLayers().size(), 2);
    QCOMPARE(again.thermalTables().at(0).name, QStringLiteral("kSi"));
    QCOMPARE(again.description(), QStringLiteral("Saved by test"));
    QCOMPARE(again.substrateOffsetRaw(), QStringLiteral("=t_sub"));
}

void SubstrateTest::resolve_overridesComputedVariables()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ovr.xml"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f)
            << "<Stackup schemaVersion=\"3.1\">\n"
            << "  <Variables>\n"
            << "    <Variable Name=\"base\" Value=\"10\" />\n"
            << "    <Variable Name=\"thick\" Value=\"=base*2\" />\n"
            << "  </Variables>\n"
            << "  <Materials><Material Name=\"Si\" Type=\"Dielectric\" Color=\"ffffff\"/></Materials>\n"
            << "  <ELayers LengthUnit=\"um\">\n"
            << "    <Dielectrics>\n"
            << "      <Dielectric Name=\"D\" Material=\"Si\" Thickness=\"=thick\" />\n"
            << "    </Dielectrics>\n"
            << "    <Layers/>\n"
            << "  </ELayers>\n"
            << "</Stackup>\n";
    }

    Substrate sub;
    QVERIFY(sub.parseXmlFile(path));
    QCOMPARE(sub.dielectrics().first().thickness(), 20.0);

    QHash<QString, QVariant> ovr;
    ovr.insert(QStringLiteral("base"), 7.0);
    QString err;
    QVERIFY2(sub.resolve(ovr, &err), qPrintable(err));
    QCOMPARE(sub.dielectrics().first().thickness(), 14.0);
}

void SubstrateTest::computeMinimumSchemaVersion_detectsFeatures()
{
    Substrate plain;
    plain.setSchemaVersion(QStringLiteral("2.0"));
    QCOMPARE(plain.computeMinimumSchemaVersion(), QStringLiteral("2.0"));

    DerivedLayer dl;
    dl.name = QStringLiteral("X");
    dl.operation = QStringLiteral("OR");
    plain.derivedLayers().append(dl);
    QCOMPARE(plain.computeMinimumSchemaVersion(), QStringLiteral("3.0"));
}

void SubstrateTest::parseMissingFile_fails()
{
    Substrate sub;
    QVERIFY(!sub.parseXmlFile(QStringLiteral("Z:/missing/stackup.xml")));
    QVERIFY(!sub.writeXmlFile(QStringLiteral("Z:/missing/dir/out.xml")));
}

void SubstrateTest::resolve_dielectricAndLayerReferences()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("refs.xml"));
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
            << "      <Dielectric Name=\"Base\" Material=\"Si\" Thickness=\"10\" />\n"
            << "      <Dielectric Name=\"TopD\" Material=\"Si\" Thickness=\"5\" "
               "Reference=\"Base\" ReferenceEdge=\"Top\" />\n"
            << "    </Dielectrics>\n"
            << "    <Layers>\n"
            << "      <Layer Name=\"M1\" Type=\"conductor\" Material=\"Met\" Layer=\"1\" "
               "Zmin=\"0\" Zmax=\"1\" />\n"
            << "      <Layer Name=\"M2\" Type=\"conductor\" Material=\"Met\" Layer=\"2\" "
               "Reference=\"M1\" ReferenceEdge=\"top\" Zmin=\"0.2\" Zmax=\"1.2\" />\n"
            << "    </Layers>\n"
            << "  </ELayers>\n"
            << "</Stackup>\n";
    }

    Substrate sub;
    QVERIFY(sub.parseXmlFile(path));
    QString err;
    QVERIFY2(sub.resolve({}, &err), qPrintable(err));

    bool foundTopD = false;
    for (const Dielectric &d : sub.dielectrics()) {
        if (d.name() != QLatin1String("TopD"))
            continue;
        foundTopD = true;
        QCOMPARE(d.resolvedZmin(), 10.0);
        QCOMPARE(d.resolvedZmax(), 15.0);
    }
    QVERIFY(foundTopD);

    bool foundM2 = false;
    for (const Layer &lay : sub.layers()) {
        if (lay.name() != QLatin1String("M2"))
            continue;
        foundM2 = true;
        QCOMPARE(lay.zmin(), 1.2);  // M1 top (1.0) + 0.2
        QCOMPARE(lay.zmax(), 2.2);  // M1 top + 1.2
    }
    QVERIFY(foundM2);
    QCOMPARE(sub.computeMinimumSchemaVersion(), QStringLiteral("3.0"));
}
