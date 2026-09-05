/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 ************************************************************************/

#include "substrate.h"
#include "stackupexpr.h"

#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QDebug>
#include <QSet>

#include <algorithm>

namespace {

const QString kGeneratorCommentPrefix =
    QStringLiteral("Created/modified using the XML Stackup Editor in");
const QString kLegacyGeneratorPrefix =
    QStringLiteral("Created/modified using the EMStudio Stackup Editor");
const QString kLegacyDescriptionPrefix = QStringLiteral("File description:");
const QString kHeaderSeparator = QString(60, QLatin1Char('='));

QString sanitizeXmlCommentText(QString text)
{
    text.replace(QStringLiteral("--"), QStringLiteral("- -"));
    while (text.endsWith(QLatin1Char('-')))
        text.chop(1);
    return text;
}

QString extractDescriptionFromHeaderComments(const QStringList &comments)
{
    if (comments.size() >= 2) {
        const QString first = comments.at(0).trimmed();
        if (first.startsWith(kGeneratorCommentPrefix, Qt::CaseInsensitive)
            || first.startsWith(kLegacyGeneratorPrefix, Qt::CaseInsensitive)) {
            if (comments.at(1).trimmed() == kHeaderSeparator) {
                QStringList lines;
                int i = 2;
                for (; i < comments.size(); ++i) {
                    const QString line = comments.at(i).trimmed();
                    if (line == kHeaderSeparator)
                        break;
                    lines << line;
                }
                if (i < comments.size())
                    return lines.join(QLatin1Char('\n'));
            }

            // Legacy setupEM format: generator + separator + single "File description:" comment.
            if (comments.size() >= 3 && comments.at(2).trimmed().startsWith(
                    kLegacyDescriptionPrefix, Qt::CaseInsensitive)) {
                return comments.at(2).trimmed().mid(kLegacyDescriptionPrefix.size()).trimmed();
            }
        }
    }

    for (const QString &comment : comments) {
        const QString text = comment.trimmed();
        if (text.startsWith(kLegacyDescriptionPrefix, Qt::CaseInsensitive))
            return text.mid(kLegacyDescriptionPrefix.size()).trimmed();
    }

    return {};
}

void writeStackupHeaderComments(QXmlStreamWriter &xml, const QString &description)
{
    xml.writeComment(QStringLiteral(" %1 EMStudio ")
                         .arg(sanitizeXmlCommentText(kGeneratorCommentPrefix)));

    const QString trimmed = description.trimmed();
    if (trimmed.isEmpty())
        return;

    xml.writeComment(QStringLiteral(" %1 ").arg(kHeaderSeparator));
    const QStringList lines = trimmed.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (const QString &line : lines)
        xml.writeComment(QStringLiteral(" %1 ").arg(sanitizeXmlCommentText(line)));
    xml.writeComment(QStringLiteral(" %1 ").arg(kHeaderSeparator));
}

} // namespace

Substrate::Substrate() = default;

bool Substrate::parseXmlFile(const QString &filePath)
{
    m_variables.clear();
    m_materials.clear();
    m_dielectrics.clear();
    m_layers.clear();
    m_derivedLayers.clear();
    m_thermalTables.clear();
    m_substrateOffset = 0.0;
    m_substrateOffsetRaw.clear();
    m_schemaVersion.clear();
    m_lengthUnit = QStringLiteral("um");
    m_description.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open substrate XML:" << filePath;
        return false;
    }

    QXmlStreamReader xml(&file);

    auto attrS = [](const QXmlStreamAttributes &a, const char *name, const QString &def = {}) {
        return a.hasAttribute(QLatin1String(name)) ? a.value(QLatin1String(name)).toString() : def;
    };
    auto attrI = [](const QXmlStreamAttributes &a, const char *name, int def = 0) {
        bool ok = false;
        const int v = a.hasAttribute(QLatin1String(name))
                          ? a.value(QLatin1String(name)).toInt(&ok)
                          : def;
        return ok ? v : def;
    };

    ThermalTable *currentTable = nullptr;
    bool inStackup = false;
    bool collectingStackupHeader = false;
    QStringList stackupHeaderComments;

    while (!xml.atEnd() && !xml.hasError()) {
        const auto token = xml.readNext();

        if (token == QXmlStreamReader::Comment) {
            const QString c = xml.text().toString().trimmed();
            if (collectingStackupHeader) {
                stackupHeaderComments.append(c);
            } else if (m_description.isEmpty()
                       && c.startsWith(kLegacyDescriptionPrefix, Qt::CaseInsensitive)) {
                m_description = c.mid(kLegacyDescriptionPrefix.size()).trimmed();
            }
            continue;
        }

        if (!xml.isStartElement())
            continue;

        const QString name = xml.name().toString();
        const auto attrs = xml.attributes();

        if (name == QLatin1String("Stackup")) {
            inStackup = true;
            collectingStackupHeader = true;
            stackupHeaderComments.clear();
            m_schemaVersion = attrS(attrs, "schemaVersion");
            continue;
        }

        if (collectingStackupHeader && inStackup) {
            m_description = extractDescriptionFromHeaderComments(stackupHeaderComments);
            collectingStackupHeader = false;
        }

        if (name == QLatin1String("ELayers")) {
            m_lengthUnit = attrS(attrs, "LengthUnit", QStringLiteral("um"));
        } else if (name == QLatin1String("Substrate")) {
            m_substrateOffsetRaw = attrS(attrs, "Offset").trimmed();
            bool ok = false;
            m_substrateOffset = m_substrateOffsetRaw.toDouble(&ok);
            if (!ok)
                m_substrateOffset = 0.0; // may still be an "=expr"; resolved in resolveGeometry()
        } else if (name == QLatin1String("Variable")) {
            StackupVariable v;
            v.name = attrS(attrs, "Name");
            v.valueRaw = attrS(attrs, "Value");
            v.type = attrS(attrs, "Type");
            if (!v.name.isEmpty())
                m_variables << v;
        } else if (name == QLatin1String("Material")) {
            Material mat;
            mat.setName(attrS(attrs, "Name"));
            mat.setType(attrS(attrs, "Type").toLower());
            if (attrs.hasAttribute(QStringLiteral("Permittivity")))
                mat.setPermittivityRaw(attrS(attrs, "Permittivity"));
            if (attrs.hasAttribute(QStringLiteral("DielectricLossTangent")))
                mat.setLossTangentRaw(attrS(attrs, "DielectricLossTangent"));
            if (attrs.hasAttribute(QStringLiteral("Conductivity")))
                mat.setConductivityRaw(attrS(attrs, "Conductivity"));
            if (attrs.hasAttribute(QStringLiteral("Color")))
                mat.setColorHex(attrS(attrs, "Color"));
            if (attrs.hasAttribute(QStringLiteral("ThermalConductivity")))
                mat.setThermalConductivityRaw(attrS(attrs, "ThermalConductivity"));
            if (attrs.hasAttribute(QStringLiteral("ThermalConductivityTable")))
                mat.setThermalConductivityTable(attrS(attrs, "ThermalConductivityTable"));
            if (attrs.hasAttribute(QStringLiteral("Rs")))
                mat.setRsRaw(attrS(attrs, "Rs"));
            m_materials << mat;
        } else if (name == QLatin1String("Dielectric")) {
            Dielectric d;
            d.setName(attrS(attrs, "Name"));
            d.setMaterial(attrS(attrs, "Material"));
            d.setThicknessRaw(attrS(attrs, "Thickness"));
            d.setReference(attrS(attrs, "Reference"));
            d.setReferenceEdge(attrS(attrs, "ReferenceEdge"));
            m_dielectrics << d;
        } else if (name == QLatin1String("Layer") && xml.prefix().isEmpty()) {
            // Distinguish from DerivedLayer Operand Layer attribute by element name only
            Layer lay;
            lay.setName(attrS(attrs, "Name"));
            lay.setType(attrS(attrs, "Type").toLower());
            lay.setZminRaw(attrS(attrs, "Zmin", QStringLiteral("0")));
            lay.setZmaxRaw(attrS(attrs, "Zmax", QStringLiteral("0")));
            lay.setMaterial(attrS(attrs, "Material"));
            lay.setLayerNumber(attrI(attrs, "Layer"));
            lay.setReference(attrS(attrs, "Reference"));
            lay.setReferenceEdge(attrS(attrs, "ReferenceEdge"));
            m_layers << lay;
        } else if (name == QLatin1String("DerivedLayer")) {
            DerivedLayer dl;
            dl.name = attrS(attrs, "Name");
            dl.layerNumber = attrI(attrs, "Layer");
            dl.operation = attrS(attrs, "Operation").toUpper();
            dl.sizeValue = attrS(attrs, "Size");
            m_derivedLayers << dl;
        } else if (name == QLatin1String("Operand")) {
            if (!m_derivedLayers.isEmpty())
                m_derivedLayers.last().operands << attrI(attrs, "Layer");
        } else if (name == QLatin1String("Table")) {
            ThermalTable t;
            t.name = attrS(attrs, "Name");
            m_thermalTables << t;
            currentTable = &m_thermalTables.last();
        } else if (name == QLatin1String("Entry") || name == QLatin1String("Point")) {
            if (currentTable) {
                ThermalTablePoint p;
                p.temperatureRaw = attrS(attrs, "Temperature", attrS(attrs, "T"));
                p.valueRaw = attrS(attrs, "Value", attrS(attrs, "Conductivity"));
                currentTable->points << p;
            }
        }
    }

    file.close();

    if (collectingStackupHeader && m_description.isEmpty())
        m_description = extractDescriptionFromHeaderComments(stackupHeaderComments);

    if (xml.hasError()) {
        qWarning() << "XML parsing error:" << xml.errorString();
        return false;
    }

    QString err;
    if (!resolve({}, &err))
        qWarning() << "Stackup resolve warning:" << err;

    return true;
}

bool Substrate::writeXmlFile(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "Failed to write substrate XML:" << filePath;
        return false;
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(2);
    xml.writeStartDocument();

    const QString schema = computeMinimumSchemaVersion();
    xml.writeStartElement(QStringLiteral("Stackup"));
    xml.writeAttribute(QStringLiteral("schemaVersion"), schema);

    writeStackupHeaderComments(xml, m_description);

    if (!m_variables.isEmpty()) {
        xml.writeStartElement(QStringLiteral("Variables"));
        for (const StackupVariable &v : m_variables) {
            xml.writeEmptyElement(QStringLiteral("Variable"));
            xml.writeAttribute(QStringLiteral("Name"), v.name);
            xml.writeAttribute(QStringLiteral("Value"), v.valueRaw);
            if (!v.type.isEmpty())
                xml.writeAttribute(QStringLiteral("Type"), v.type);
        }
        xml.writeEndElement();
    }

    xml.writeStartElement(QStringLiteral("Materials"));
    for (const Material &mat : m_materials) {
        xml.writeEmptyElement(QStringLiteral("Material"));
        xml.writeAttribute(QStringLiteral("Name"), mat.name());
        const QString type = mat.type().isEmpty()
                                 ? QStringLiteral("Dielectric")
                                 : (mat.type().left(1).toUpper() + mat.type().mid(1));
        xml.writeAttribute(QStringLiteral("Type"), type);
        if (!mat.permittivityRaw().isEmpty())
            xml.writeAttribute(QStringLiteral("Permittivity"), mat.permittivityRaw());
        if (!mat.lossTangentRaw().isEmpty())
            xml.writeAttribute(QStringLiteral("DielectricLossTangent"), mat.lossTangentRaw());
        if (!mat.conductivityRaw().isEmpty())
            xml.writeAttribute(QStringLiteral("Conductivity"), mat.conductivityRaw());
        if (!mat.rsRaw().isEmpty())
            xml.writeAttribute(QStringLiteral("Rs"), mat.rsRaw());
        if (!mat.thermalConductivityRaw().isEmpty())
            xml.writeAttribute(QStringLiteral("ThermalConductivity"), mat.thermalConductivityRaw());
        if (!mat.thermalConductivityTable().isEmpty())
            xml.writeAttribute(QStringLiteral("ThermalConductivityTable"), mat.thermalConductivityTable());
        if (!mat.colorHex().isEmpty())
            xml.writeAttribute(QStringLiteral("Color"), mat.colorHex());
    }
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("ELayers"));
    xml.writeAttribute(QStringLiteral("LengthUnit"), m_lengthUnit);

    xml.writeStartElement(QStringLiteral("Dielectrics"));
    for (const Dielectric &d : m_dielectrics) {
        xml.writeEmptyElement(QStringLiteral("Dielectric"));
        xml.writeAttribute(QStringLiteral("Name"), d.name());
        if (!d.reference().isEmpty()) {
            xml.writeAttribute(QStringLiteral("Reference"), d.reference());
            if (!d.referenceEdge().isEmpty())
                xml.writeAttribute(QStringLiteral("ReferenceEdge"), d.referenceEdge());
        }
        xml.writeAttribute(QStringLiteral("Material"), d.material());
        xml.writeAttribute(QStringLiteral("Thickness"),
                           d.thicknessRaw().isEmpty()
                               ? QString::number(d.thickness(), 'f', 4)
                               : d.thicknessRaw());
    }
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("Layers"));
    if (!m_substrateOffsetRaw.isEmpty()
        || m_substrateOffset != 0.0) {
        xml.writeEmptyElement(QStringLiteral("Substrate"));
        xml.writeAttribute(QStringLiteral("Offset"),
                           m_substrateOffsetRaw.isEmpty()
                               ? QString::number(m_substrateOffset)
                               : m_substrateOffsetRaw);
    }
    for (const Layer &lay : m_layers) {
        xml.writeEmptyElement(QStringLiteral("Layer"));
        xml.writeAttribute(QStringLiteral("Name"), lay.name());
        xml.writeAttribute(QStringLiteral("Type"), lay.type());
        if (!lay.reference().isEmpty()) {
            xml.writeAttribute(QStringLiteral("Reference"), lay.reference());
            if (!lay.referenceEdge().isEmpty())
                xml.writeAttribute(QStringLiteral("ReferenceEdge"), lay.referenceEdge());
        }
        xml.writeAttribute(QStringLiteral("Material"), lay.material());
        xml.writeAttribute(QStringLiteral("Layer"), QString::number(lay.layerNumber()));
        xml.writeAttribute(QStringLiteral("Zmin"),
                           lay.zminRaw().isEmpty() ? QString::number(lay.zmin(), 'f', 4)
                                                   : lay.zminRaw());
        xml.writeAttribute(QStringLiteral("Zmax"),
                           lay.zmaxRaw().isEmpty() ? QString::number(lay.zmax(), 'f', 4)
                                                   : lay.zmaxRaw());
    }
    xml.writeEndElement();

    if (!m_derivedLayers.isEmpty()) {
        xml.writeStartElement(QStringLiteral("DerivedLayers"));
        for (const DerivedLayer &dl : m_derivedLayers) {
            xml.writeStartElement(QStringLiteral("DerivedLayer"));
            xml.writeAttribute(QStringLiteral("Name"), dl.name);
            xml.writeAttribute(QStringLiteral("Layer"), QString::number(dl.layerNumber));
            xml.writeAttribute(QStringLiteral("Operation"), dl.operation);
            if (!dl.sizeValue.isEmpty())
                xml.writeAttribute(QStringLiteral("Size"), dl.sizeValue);
            for (int op : dl.operands) {
                xml.writeEmptyElement(QStringLiteral("Operand"));
                xml.writeAttribute(QStringLiteral("Layer"), QString::number(op));
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }

    xml.writeEndElement(); // ELayers

    if (!m_thermalTables.isEmpty()) {
        xml.writeStartElement(QStringLiteral("Tables"));
        for (const ThermalTable &t : m_thermalTables) {
            xml.writeStartElement(QStringLiteral("Table"));
            xml.writeAttribute(QStringLiteral("Name"), t.name);
            for (const ThermalTablePoint &p : t.points) {
                xml.writeEmptyElement(QStringLiteral("Entry"));
                xml.writeAttribute(QStringLiteral("Temperature"), p.temperatureRaw);
                xml.writeAttribute(QStringLiteral("Value"), p.valueRaw);
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }

    xml.writeEndElement(); // Stackup
    xml.writeEndDocument();
    return true;
}

bool Substrate::resolve(const QHash<QString, QVariant> &overrides, QString *error)
{
    QHash<QString, QVariant> vars;
    if (!resolveVariables(overrides, &vars, error))
        return false;
    return resolveGeometry(vars, error);
}

bool Substrate::resolveVariables(const QHash<QString, QVariant> &overrides,
                                 QHash<QString, QVariant> *outVars,
                                 QString *error)
{
    QHash<QString, QVariant> vars;

    // Seed with plain literals / overridden values first
    for (StackupVariable &v : m_variables) {
        if (overrides.contains(v.name)) {
            vars.insert(v.name, overrides.value(v.name));
            v.resolved = overrides.value(v.name).toString();
            continue;
        }
        if (!v.isComputed()) {
            if (v.type.compare(QLatin1String("string"), Qt::CaseInsensitive) == 0) {
                vars.insert(v.name, v.valueRaw);
                v.resolved = v.valueRaw;
            } else {
                bool ok = false;
                const double d = v.valueRaw.toDouble(&ok);
                if (ok) {
                    vars.insert(v.name, d);
                    v.resolved = QString::number(d);
                } else {
                    vars.insert(v.name, v.valueRaw);
                    v.resolved = v.valueRaw;
                }
            }
        }
    }

    // Resolve computed variables (multi-pass for dependency order)
    for (int pass = 0; pass < m_variables.size() + 2; ++pass) {
        bool progress = false;
        bool pending = false;
        for (StackupVariable &v : m_variables) {
            if (overrides.contains(v.name))
                continue;
            if (!v.isComputed())
                continue;
            if (vars.contains(v.name) && pass > 0)
                continue;

            QString err;
            const QVariant val = StackupExpr::eval(v.valueRaw, vars, &err);
            if (!err.isEmpty() || !val.isValid()) {
                pending = true;
                continue;
            }
            vars.insert(v.name, val);
            v.resolved = (val.type() == QVariant::String) ? val.toString()
                                                          : QString::number(val.toDouble());
            progress = true;
        }
        if (!pending)
            break;
        if (!progress) {
            if (error)
                *error = QStringLiteral("Failed to resolve stackup variables (cycle or unknown name)");
            return false;
        }
    }

    *outVars = vars;
    return true;
}

bool Substrate::resolveGeometry(const QHash<QString, QVariant> &vars, QString *error)
{
    auto evalT = [&](const QString &raw, double *out) -> bool {
        QString err;
        if (!StackupExpr::evalNumber(raw.isEmpty() ? QStringLiteral("0") : raw, vars, out, &err)) {
            if (error)
                *error = err;
            return false;
        }
        return true;
    };

    // Substrate Offset may be a literal or "=expr" (e.g. "=substrate_thickness").
    {
        const QString raw = m_substrateOffsetRaw.isEmpty()
                                ? QString::number(m_substrateOffset)
                                : m_substrateOffsetRaw;
        double off = 0.0;
        if (!evalT(raw, &off))
            return false;
        m_substrateOffset = off;
    }

    // Resolve dielectric thicknesses
    for (Dielectric &d : m_dielectrics) {
        double t = 0.0;
        if (!evalT(d.thicknessRaw().isEmpty() ? QString::number(d.thickness())
                                              : d.thicknessRaw(),
                   &t))
            return false;
        d.setThickness(t);
    }

    const bool hasDielectricRefs = std::any_of(
        m_dielectrics.begin(), m_dielectrics.end(),
        [](const Dielectric &d) { return !d.reference().isEmpty(); });

    QHash<QString, QPair<double, double>> dielBounds; // name -> (zmin,zmax)

    if (!hasDielectricRefs) {
        // Legacy: reverse stack from bottom (after reversing file order which is top-first)
        QList<Dielectric> bottomUp = m_dielectrics;
        std::reverse(bottomUp.begin(), bottomUp.end());
        double z = -m_substrateOffset;
        for (Dielectric &d : bottomUp) {
            d.setResolvedZmin(z);
            d.setResolvedZmax(z + d.thickness());
            dielBounds.insert(d.name(), qMakePair(d.resolvedZmin(), d.resolvedZmax()));
            z += d.thickness();
        }
        // Write resolved values back into m_dielectrics by name
        for (Dielectric &d : m_dielectrics) {
            if (dielBounds.contains(d.name())) {
                d.setResolvedZmin(dielBounds.value(d.name()).first);
                d.setResolvedZmax(dielBounds.value(d.name()).second);
            }
        }
    } else {
        QSet<QString> placed;
        for (int pass = 0; pass < m_dielectrics.size() + 2; ++pass) {
            bool progress = false;
            for (Dielectric &d : m_dielectrics) {
                if (placed.contains(d.name()))
                    continue;

                if (d.reference().isEmpty()) {
                    d.setResolvedZmin(0.0);
                    d.setResolvedZmax(d.thickness());
                    dielBounds.insert(d.name(), qMakePair(d.resolvedZmin(), d.resolvedZmax()));
                    placed.insert(d.name());
                    progress = true;
                    continue;
                }

                if (!dielBounds.contains(d.reference()))
                    continue;

                const auto ref = dielBounds.value(d.reference());
                const bool fromTop =
                    d.referenceEdge().compare(QLatin1String("Top"), Qt::CaseInsensitive) == 0;
                const double edge = fromTop ? ref.second : ref.first;
                d.setResolvedZmin(edge);
                d.setResolvedZmax(edge + d.thickness());
                dielBounds.insert(d.name(), qMakePair(d.resolvedZmin(), d.resolvedZmax()));
                placed.insert(d.name());
                progress = true;
            }
            if (placed.size() == m_dielectrics.size())
                break;
            if (!progress) {
                if (error)
                    *error = QStringLiteral("Failed to resolve dielectric Reference chain");
                return false;
            }
        }
    }

    // Also index layers already placed for layer-to-layer references
    QHash<QString, QPair<double, double>> layerBounds;

    const bool hasLayerRefs = std::any_of(
        m_layers.begin(), m_layers.end(),
        [](const Layer &l) { return !l.reference().isEmpty(); });

    auto edgeOf = [&](const QString &refName, const QString &edge, bool *ok) -> double {
        *ok = true;
        if (dielBounds.contains(refName)) {
            const auto b = dielBounds.value(refName);
            return edge.compare(QLatin1String("Top"), Qt::CaseInsensitive) == 0 ? b.second
                                                                               : b.first;
        }
        if (layerBounds.contains(refName)) {
            const auto b = layerBounds.value(refName);
            return edge.compare(QLatin1String("Top"), Qt::CaseInsensitive) == 0 ? b.second
                                                                               : b.first;
        }
        *ok = false;
        return 0.0;
    };

    if (!hasLayerRefs) {
        for (Layer &lay : m_layers) {
            double z0 = 0.0, z1 = 0.0;
            if (!evalT(lay.zminRaw(), &z0) || !evalT(lay.zmaxRaw(), &z1))
                return false;
            lay.setZmin(z0);
            lay.setZmax(z1);
            layerBounds.insert(lay.name(), qMakePair(z0, z1));
        }
    } else {
        QSet<QString> placed;
        for (int pass = 0; pass < m_layers.size() + 2; ++pass) {
            bool progress = false;
            for (Layer &lay : m_layers) {
                if (placed.contains(lay.name()))
                    continue;

                double off0 = 0.0, off1 = 0.0;
                if (!evalT(lay.zminRaw(), &off0) || !evalT(lay.zmaxRaw(), &off1))
                    return false;

                if (lay.reference().isEmpty()) {
                    lay.setZmin(off0);
                    lay.setZmax(off1);
                    layerBounds.insert(lay.name(), qMakePair(off0, off1));
                    placed.insert(lay.name());
                    progress = true;
                    continue;
                }

                bool ok = false;
                const double edge = edgeOf(lay.reference(),
                                           lay.referenceEdge().isEmpty()
                                               ? QStringLiteral("Top")
                                               : lay.referenceEdge(),
                                           &ok);
                if (!ok)
                    continue;

                lay.setZmin(edge + off0);
                lay.setZmax(edge + off1);
                layerBounds.insert(lay.name(), qMakePair(lay.zmin(), lay.zmax()));
                placed.insert(lay.name());
                progress = true;
            }
            if (placed.size() == m_layers.size())
                break;
            if (!progress) {
                if (error)
                    *error = QStringLiteral("Failed to resolve layer Reference chain");
                return false;
            }
        }
    }

    // Resolve material numeric fields that may be expressions
    for (Material &mat : m_materials) {
        double v = 0.0;
        if (!mat.permittivityRaw().isEmpty() && evalT(mat.permittivityRaw(), &v))
            mat.setPermittivity(v);
        if (!mat.lossTangentRaw().isEmpty() && evalT(mat.lossTangentRaw(), &v))
            mat.setLossTangent(v);
        if (!mat.conductivityRaw().isEmpty() && evalT(mat.conductivityRaw(), &v))
            mat.setConductivity(v);
    }

    return true;
}

const QList<StackupVariable> &Substrate::variables() const { return m_variables; }
QList<StackupVariable> &Substrate::variables() { return m_variables; }

const QList<Material> &Substrate::materials() const { return m_materials; }
QList<Material> &Substrate::materials() { return m_materials; }

const QList<Dielectric> &Substrate::dielectrics() const { return m_dielectrics; }
QList<Dielectric> &Substrate::dielectrics() { return m_dielectrics; }

const QList<Layer> &Substrate::layers() const { return m_layers; }
QList<Layer> &Substrate::layers() { return m_layers; }

const QList<DerivedLayer> &Substrate::derivedLayers() const { return m_derivedLayers; }
QList<DerivedLayer> &Substrate::derivedLayers() { return m_derivedLayers; }

const QList<ThermalTable> &Substrate::thermalTables() const { return m_thermalTables; }
QList<ThermalTable> &Substrate::thermalTables() { return m_thermalTables; }

double Substrate::substrateOffset() const { return m_substrateOffset; }
void Substrate::setSubstrateOffset(double offset)
{
    m_substrateOffset = offset;
    m_substrateOffsetRaw = QString::number(offset);
}

QString Substrate::substrateOffsetRaw() const { return m_substrateOffsetRaw; }
void Substrate::setSubstrateOffsetRaw(const QString &raw)
{
    m_substrateOffsetRaw = raw.trimmed();
    bool ok = false;
    const double d = m_substrateOffsetRaw.toDouble(&ok);
    m_substrateOffset = ok ? d : 0.0;
}

const QString &Substrate::schemaVersion() const { return m_schemaVersion; }
void Substrate::setSchemaVersion(const QString &v) { m_schemaVersion = v; }

const QString &Substrate::lengthUnit() const { return m_lengthUnit; }
void Substrate::setLengthUnit(const QString &u) { m_lengthUnit = u; }

const QString &Substrate::description() const { return m_description; }
void Substrate::setDescription(const QString &d) { m_description = d; }

QList<StackupVariable> Substrate::overridableVariables() const
{
    QList<StackupVariable> out;
    for (const StackupVariable &v : m_variables) {
        if (!v.isComputed())
            out << v;
    }
    return out;
}

QString Substrate::computeMinimumSchemaVersion() const
{
    bool needs31 = !m_variables.isEmpty();
    bool needs30 = !m_derivedLayers.isEmpty();

    auto looksExpr = [](const QString &s) {
        return s.trimmed().startsWith(QLatin1Char('='));
    };

    for (const Dielectric &d : m_dielectrics) {
        if (!d.reference().isEmpty())
            needs30 = true;
        if (looksExpr(d.thicknessRaw()))
            needs31 = true;
    }
    for (const Layer &l : m_layers) {
        if (!l.reference().isEmpty())
            needs30 = true;
        if (looksExpr(l.zminRaw()) || looksExpr(l.zmaxRaw()))
            needs31 = true;
    }
    for (const Material &m : m_materials) {
        if (looksExpr(m.permittivityRaw()) || looksExpr(m.conductivityRaw())
            || looksExpr(m.lossTangentRaw()) || looksExpr(m.thermalConductivityTable())
            || looksExpr(m.thermalConductivityRaw()))
            needs31 = true;
    }

    if (needs31)
        return QStringLiteral("3.1");
    if (needs30)
        return QStringLiteral("3.0");
    if (!m_schemaVersion.isEmpty())
        return m_schemaVersion;
    return QStringLiteral("2.0");
}
