/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 ************************************************************************/

#include <QMenu>
#include <QFile>
#include <QDebug>
#include <QAction>
#include <QProcess>
#include <QFileInfo>
#include <QSettings>
#include <QJsonArray>
#include <QScrollBar>
#include <QJsonValue>
#include <QFileDialog>
#include <QTextStream>
#include <QJsonObject>
#include <QMessageBox>
#include <QCloseEvent>
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QProcessEnvironment>

#include "extension/variantmanager.h"
#include "extension/variantfactory.h"

#include "QtPropertyBrowser/qtvariantproperty.h"
#include "QtPropertyBrowser/qttreepropertybrowser.h"

#include "mainwindow.h"
#include "preferences.h"
#include "ui_mainwindow.h"
#include "substrateview.h"
#include "pythonparser.h"

/*!*******************************************************************************************************************
 * \brief Saves the current Python script, re-parses it and updates the simulation setup.
 *
 * Writes the contents of the embedded Python editor to the file referenced by
 * txtRunPythonScript, then runs PythonParser on that file and rebuilds the
 * simulation-related GUI state (simulation settings, GDS/substrate paths,
 * run directory and internal m_simSettings entries) from the parsed result.
 *
 * The editor document is marked unmodified on success so that further tab
 * changes do not trigger an unnecessary apply prompt.
 *
 * \return true on successful save and parse, false otherwise.
 **********************************************************************************************************************/
bool MainWindow::applyPythonScriptFromEditor()
{
    QString filePath = m_ui->txtRunPythonScript->text().trimmed();

    info(filePath, true);

    if (filePath.isEmpty()) {
        if (!ensurePythonScriptPathBySaveAs(false))
            return false;
        filePath = m_ui->txtRunPythonScript->text().trimmed();
        if (filePath.isEmpty())
            return false;
    }

    if (filePath.isEmpty()) {
        error(tr("No Python script file specified."), false);
        return false;
    }

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        error(tr("Failed to save Python script:\n%1").arg(f.errorString()), false);
        return false;
    }

    QTextStream out(&f);
    out << m_ui->editRunPythonScript->toPlainText();
    f.close();

    m_preferences["PALACE_MODEL_FILE"] = filePath;

    PythonParser::Result res = PythonParser::parseSettings(filePath);
    if (!res.ok) {
        error(tr("Failed to parse Python model file:\n%1").arg(res.error), false);
        return false;
    }

    m_curPythonData = res;

    rebuildSimulationSettingsFromPalace(res.settings, res.settingTips, res.topLevel);

    QFileInfo fi(filePath);
    const QDir modelDir(fi.absolutePath());

    if (!res.gdsFilename.isEmpty()) {
        QString gdsPath = fromWslPath(res.gdsFilename);
        if (QFileInfo(gdsPath).isRelative())
            gdsPath = modelDir.filePath(gdsPath);

        m_ui->txtGdsFile->setText(gdsPath);
        m_simSettings["GdsFile"] = gdsPath;
        m_sysSettings["GdsDir"]  = QFileInfo(gdsPath).absolutePath();
    }

    if (!res.xmlFilename.isEmpty()) {
        QString subPath = fromWslPath(res.xmlFilename);
        if (QFileInfo(subPath).isRelative())
            subPath = modelDir.filePath(subPath);

        m_ui->txtSubstrate->setText(subPath);
        m_simSettings["SubstrateFile"] = subPath;
        m_sysSettings["SubstrateDir"]  = QFileInfo(subPath).absolutePath();
    }

    m_simSettings["RunPythonScript"] = filePath;
    m_ui->editRunPythonScript->document()->setModified(false);

    setLineEditPalette(m_ui->txtRunPythonScript, filePath);

    updateSimulationSettings();

    return true;
}

/*!*******************************************************************************************************************
 * \brief Rebuilds the "Simulation Settings" property group using values parsed from a Palace Python model.
 *
 * This method is called when a Palace model file is opened and parsed successfully.
 * All existing properties within the "Simulation Settings" group are removed,
 * and new properties are created dynamically for each key–value pair found in the parsed data.
 *
 * The property type (bool, int/double, string) is inferred from the QVariant content.
 * Numeric values are forced to Double to ensure SciDoubleSpinBox usage (scientific input).
 *
 * \param settings  Map of key–value pairs extracted from the Palace Python model.
 * \param tips      Optional map of tooltips for each key.
 **********************************************************************************************************************/
void MainWindow::rebuildSimulationSettingsFromPalace(const QMap<QString, QVariant>& settings,
                                                     const QMap<QString, QString>& tips,
                                                     const QMap<QString, QVariant>& topLevelVars)
{
    if (!m_simSettingsGroup || !m_variantManager)
        return;

    clearSimSettingsGroup();

    const QString simTool = m_ui->cbxSimTool->currentText().trimmed();

    // -------------------------------------------------------------------------------------------------
    // Boundaries (special handling)
    // -------------------------------------------------------------------------------------------------
    const QString boundariesKey = findBoundariesKeyCaseInsensitive(settings);
    if (!boundariesKey.isEmpty()) {
        const QStringList items = parseBoundariesItems(settings.value(boundariesKey));
        if (items.size() == 6)
            applyBoundariesToUiAndSettings(items, simTool);
    }

    QMap<QString, QVariant> merged = topLevelVars; // low priority
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it)
        merged[it.key()] = it.value();             // overwrite => high priority

    // -------------------------------------------------------------------------------------------------
    // Generic settings
    // -------------------------------------------------------------------------------------------------
    for (auto it = merged.constBegin(); it != merged.constEnd(); ++it) {
        const QString& key = it.key();
        const QVariant& val = it.value();

        if (shouldSkipPalaceSettingKey(key))
            continue;

        const PalacePropInfo info = inferPalacePropertyInfo(key, val);

        if (shouldSkipStringSelfReference(key, info))
            continue;

        QtVariantProperty* prop = m_variantManager->addProperty(info.propType, key);
        if (!prop)
            continue;

        applyTipIfAny(prop, key, tips);

        if (info.propType == QVariant::Double)
            setupDoubleAttributes(prop, info);

        prop->setValue(info.value);
        m_simSettingsGroup->addSubProperty(prop);
    }
}

/*!*******************************************************************************************************************
 * \brief Removes all existing sub-properties from the Simulation Settings group.
 **********************************************************************************************************************/
void MainWindow::clearSimSettingsGroup()
{
    const auto children = m_simSettingsGroup->subProperties();
    for (QtProperty* child : children)
        delete child;
}

/*!*******************************************************************************************************************
 * \brief Finds the first key in \a settings that matches "Boundaries" or "Boundary" case-insensitively.
 *
 * \param settings Map of settings parsed from the Palace model.
 *
 * \return The matching key as stored in \a settings, or an empty string if not found.
 **********************************************************************************************************************/
QString MainWindow::findBoundariesKeyCaseInsensitive(const QMap<QString, QVariant> &settings) const
{
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        const QString k = it.key();
        if (k.compare(QLatin1String("Boundaries"), Qt::CaseInsensitive) == 0 ||
            k.compare(QLatin1String("Boundary"),   Qt::CaseInsensitive) == 0) {
            return k;
        }
    }
    return QString();
}

/*!*******************************************************************************************************************
 * \brief Parses boundary items from a QVariant.
 *
 * Supported formats:
 * - String: Python-like list expression, e.g. "['PEC','PEC',...]" (quotes may be single or double)
 * - Map:   QVariantMap with keys "X-", "X+", "Y-", "Y+", "Z-", "Z+"
 *
 * \param v Variant holding boundaries.
 *
 * \return List of six boundary names in the order X-,X+,Y-,Y+,Z-,Z+. May be empty/short on parse failure.
 **********************************************************************************************************************/
QStringList MainWindow::parseBoundariesItems(const QVariant &v) const
{
    QStringList items;
    const QStringList sides{
        QStringLiteral("X-"), QStringLiteral("X+"),
        QStringLiteral("Y-"), QStringLiteral("Y+"),
        QStringLiteral("Z-"), QStringLiteral("Z+")
    };

    if (v.type() == QVariant::String) {
        QString expr = v.toString().trimmed();
        if (expr.startsWith('[') && expr.endsWith(']'))
            expr = expr.mid(1, expr.size() - 2);

        QRegularExpression itemRe(R"(['"]([^'"]+)['"])");
        auto itItems = itemRe.globalMatch(expr);
        while (itItems.hasNext()) {
            QRegularExpressionMatch m = itItems.next();
            items << m.captured(1).trimmed();
        }
        return items;
    }

    if (v.type() == QVariant::Map) {
        const QVariantMap m = v.toMap();
        for (const QString& s : sides)
            items << m.value(s, QStringLiteral("PEC")).toString();
        return items;
    }

    return items;
}

/*!*******************************************************************************************************************
 * \brief Applies boundaries to the Boundaries UI group (enum mapping) and stores them into \c m_simSettings.
 *
 * This function:
 * - Finds top-level "Boundaries" property in the property browser
 * - Updates each side sub-property based on enumNames mapping
 * - Applies tool-dependent mapping:
 *   - Palace: MUR/PML_8/ABC -> Absorbing
 *   - OpenEMS: Absorbing -> MUR
 * - Stores the final result into \c m_simSettings["Boundaries"] as a QVariantMap.
 *
 * \param items   Six boundary values in order X-,X+,Y-,Y+,Z-,Z+.
 * \param simTool Current simulation tool name (UI string).
 **********************************************************************************************************************/
void MainWindow::applyBoundariesToUiAndSettings(const QStringList &items, const QString &simTool)
{
    const QStringList sides{
        QStringLiteral("X-"), QStringLiteral("X+"),
        QStringLiteral("Y-"), QStringLiteral("Y+"),
        QStringLiteral("Z-"), QStringLiteral("Z+")
    };

    // Locate Boundaries group among top-level props
    QtProperty* boundariesGroup = nullptr;
    const auto topProps = m_propertyBrowser->properties();
    for (QtProperty* top : topProps) {
        if (top->propertyName() == QLatin1String("Boundaries")) {
            boundariesGroup = top;
            break;
        }
    }

    QStringList mappedItems = items;

    if (boundariesGroup) {
        const auto subProps = boundariesGroup->subProperties();
        for (QtProperty* sub : subProps) {
            const QString sideName = sub->propertyName();
            const int idx = sides.indexOf(sideName);
            if (idx < 0 || idx >= mappedItems.size())
                continue;

            QString mappedValue = mappedItems.at(idx);

            if (simTool.compare(QLatin1String("Palace"), Qt::CaseInsensitive) == 0) {
                if (mappedValue.compare(QLatin1String("MUR"),   Qt::CaseInsensitive) == 0 ||
                    mappedValue.compare(QLatin1String("PML_8"), Qt::CaseInsensitive) == 0 ||
                    mappedValue.compare(QLatin1String("ABC"),   Qt::CaseInsensitive) == 0) {
                    mappedValue = QStringLiteral("Absorbing");
                }
            } else if (simTool.compare(QLatin1String("OpenEMS"), Qt::CaseInsensitive) == 0) {
                if (mappedValue.compare(QLatin1String("Absorbing"), Qt::CaseInsensitive) == 0) {
                    mappedValue = QStringLiteral("MUR");
                }
            }

            mappedItems[idx] = mappedValue;

            const QStringList enumNames =
                m_variantManager->attributeValue(sub, QLatin1String("enumNames")).toStringList();
            const int enumIndex = enumNames.indexOf(mappedValue);
            if (enumIndex >= 0)
                m_variantManager->setValue(sub, enumIndex);
        }
    }

    QVariantMap bndMap;
    for (int i = 0; i < mappedItems.size() && i < sides.size(); ++i)
        bndMap[sides.at(i)] = mappedItems.at(i);

    m_simSettings[QStringLiteral("Boundaries")] = bndMap;
}

/*!*******************************************************************************************************************
 * \brief Returns \c true if this key should be skipped in generic Palace settings rebuild.
 *
 * Boundaries/Boundary are handled separately. Other complex keys (Ports, paths, rundir/script)
 * may exist in parsed data but are not intended to become generic properties here.
 *
 * \param key Setting key name.
 **********************************************************************************************************************/
bool MainWindow::shouldSkipPalaceSettingKey(const QString &key) const
{
    if (key.compare(QLatin1String("Boundaries"), Qt::CaseInsensitive) == 0 ||
        key.compare(QLatin1String("Boundary"),   Qt::CaseInsensitive) == 0)
        return true;

    // Keep consistent with other parts of the app where these keys are special.
    if (key.compare(QLatin1String("Ports"),          Qt::CaseInsensitive) == 0 ||
        key.compare(QLatin1String("GdsFile"),        Qt::CaseInsensitive) == 0 ||
        key.compare(QLatin1String("SubstrateFile"),  Qt::CaseInsensitive) == 0 ||
        key.compare(QLatin1String("RunDir"),         Qt::CaseInsensitive) == 0 ||
        key.compare(QLatin1String("RunPythonScript"),Qt::CaseInsensitive) == 0)
        return true;

    return false;
}

/*!*******************************************************************************************************************
 * \brief Infers QtPropertyBrowser property metadata (type/value/decimals/step) from a Palace setting value.
 *
 * Rules preserved from the original code:
 * - Bool -> Bool
 * - Numeric types (intish/double) -> Double (so SciDoubleSpinBox is used)
 * - String:
 *   - if empty: keep String
 *   - else try parse as double using C locale; if parse ok -> Double
 *     and if string has no '.' and no 'e/E' -> treat like integer (decimals=0, step=1)
 *   - otherwise keep String
 * - Fallback -> String
 *
 * \param key Setting key name (used only for downstream filters).
 * \param val Setting value.
 *
 * \return Filled PalacePropInfo.
 **********************************************************************************************************************/
MainWindow::PalacePropInfo MainWindow::inferPalacePropertyInfo(const QString &key, const QVariant &val) const
{
    Q_UNUSED(key);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QMetaType::Type t = static_cast<QMetaType::Type>(val.typeId());
    const bool isString = (t == QMetaType::QString);
    const bool isBool   = (t == QMetaType::Bool);
    const bool isIntish = (t == QMetaType::Int) || (t == QMetaType::UInt) ||
                          (t == QMetaType::LongLong) || (t == QMetaType::ULongLong);
    const bool isDouble = (t == QMetaType::Double);
#else
    const QVariant::Type t = val.type();
    const bool isString = (t == QVariant::String);
    const bool isBool   = (t == QVariant::Bool);
    const bool isIntish = (t == QVariant::Int) || (t == QVariant::UInt) ||
                          (t == QVariant::LongLong) || (t == QVariant::ULongLong);
    const bool isDouble = (t == QVariant::Double);
#endif

    PalacePropInfo info;
    info.propType  = QVariant::String;
    info.value     = val.toString();
    info.decimals  = 12;
    info.step      = 0.0;

    if (isBool) {
        info.propType = QVariant::Bool;
        info.value = val.toBool();
        return info;
    }

    if (isString) {
        const QString s = val.toString().trimmed();
        if (s.isEmpty()) {
            info.propType = QVariant::String;
            info.value = s;
            return info;
        }

        bool ok = false;
        const double d = QLocale::c().toDouble(s, &ok);
        if (ok) {
            info.propType = QVariant::Double;
            info.value = d;

            const bool stringLooksInt =
                !s.contains(QLatin1Char('.')) &&
                !s.contains(QLatin1Char('e'), Qt::CaseInsensitive);

            if (stringLooksInt) {
                info.decimals = 0;
                info.step = 1.0;
            } else {
                info.decimals = 12;
                info.step = 0.0;
            }
            return info;
        }

        info.propType = QVariant::String;
        info.value = s;
        return info;
    }

    if (isIntish || isDouble) {
        // Force ALL numeric types to Double so SciDoubleSpinBox is used
        info.propType = QVariant::Double;
        info.value = val.toDouble();

        if (isIntish) {
            info.decimals = 0;
            info.step = 1.0;
        } else {
            info.decimals = 12;
            info.step = 0.0;
        }
        return info;
    }

    info.propType = QVariant::String;
    info.value = val.toString();
    return info;
}

/*!*******************************************************************************************************************
 * \brief Applies the original "string self-reference" filter used to skip unwanted properties.
 *
 * Preserved logic:
 * If the property type is String and either:
 * - key equals the string value, OR
 * - the string value contains a dot '.'
 * then the property is skipped.
 *
 * \param key  Property key name.
 * \param info Property inference result.
 *
 * \return \c true if the property should be skipped.
 **********************************************************************************************************************/
bool MainWindow::shouldSkipStringSelfReference(const QString &key, const PalacePropInfo &info) const
{
    if (info.propType != QVariant::String)
        return false;

    const QString s = info.value.toString();
    if (key == s || s.contains(QLatin1Char('.')))
        return true;

    return false;
}

/*!*******************************************************************************************************************
 * \brief Applies a tooltip to a property if \a tips contains an entry for \a key.
 *
 * \param prop Property to apply tooltip to.
 * \param key  Setting key.
 * \param tips Map of tooltips by key.
 **********************************************************************************************************************/
void MainWindow::applyTipIfAny(QtVariantProperty *prop, const QString &key, const QMap<QString, QString> &tips) const
{
    auto itTip = tips.constFind(key);
    if (itTip != tips.constEnd())
        prop->setToolTip(itTip.value());
}

/*!*******************************************************************************************************************
 * \brief Configures numeric property attributes for SciDoubleSpinBox.
 *
 * Sets decimals, min/max and singleStep using \a info.
 *
 * \param prop Numeric property (must be Double).
 * \param info Inferred numeric attributes.
 **********************************************************************************************************************/
void MainWindow::setupDoubleAttributes(QtVariantProperty *prop, const PalacePropInfo &info) const
{
    prop->setAttribute(QLatin1String("decimals"), info.decimals);
    prop->setAttribute(QLatin1String("minimum"), -std::numeric_limits<double>::max());
    prop->setAttribute(QLatin1String("maximum"),  std::numeric_limits<double>::max());
    prop->setAttribute(QLatin1String("singleStep"), info.step);
}
