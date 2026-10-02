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
#include <algorithm>
#include <QFile>
#include <QDebug>
#include <QAction>
#include <QProcess>
#include <QRegularExpression>
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
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>

#include "extension/variantmanager.h"
#include "extension/variantfactory.h"

#include "QtPropertyBrowser/qtvariantproperty.h"
#include "QtPropertyBrowser/qttreepropertybrowser.h"

#include "mainwindow.h"
#include "addsettingdialog.h"
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
    if (m_ui->layoutView)
        m_ui->layoutView->setViaMergeSize(currentViaMergeSize());

    const QString parsedTop = res.getCellName().trimmed();
    if (!parsedTop.isEmpty()) {
        m_simSettings["TopCell"]      = parsedTop;
        m_simSettings["gds_cellname"] = parsedTop;
        m_simSettings["cellname"]     = parsedTop;
    }

    rebuildSimulationSettingsFromPalace(res.settings, res.settingTips, res.topLevel);

    QFileInfo fi(filePath);
    const QDir modelDir(fi.absolutePath());

    if (!res.gdsFilename.isEmpty())
    {
        const QString gdsPath = resolveModelInputFile(res.gdsFilename, modelDir);

        {
            QSignalBlocker b(m_ui->txtGdsFile);
            m_ui->txtGdsFile->setText(gdsPath);
        }

        m_simSettings["GdsFile"] = gdsPath;
        m_sysSettings["GdsDir"]  = QFileInfo(gdsPath).absolutePath();

        updateGdsUserInfo();

        const QString cellName = res.getCellName().trimmed();
        if (!cellName.isEmpty()) {
            m_simSettings["TopCell"]      = cellName;
            m_simSettings["gds_cellname"] = cellName;
            m_simSettings["cellname"]     = cellName;

            QSignalBlocker b(m_ui->cbxTopCell);
            const int idx = m_ui->cbxTopCell->findText(cellName);
            if (idx >= 0) m_ui->cbxTopCell->setCurrentIndex(idx);
        }
    }

    if (!res.xmlFilename.isEmpty()) {
        const QString subPath = resolveModelInputFile(res.xmlFilename, modelDir);

        m_ui->txtSubstrate->setText(subPath);
        m_simSettings["SubstrateFile"] = subPath;
        m_sysSettings["SubstrateDir"]  = QFileInfo(subPath).absolutePath();
    }

    m_simSettings["RunPythonScript"] = filePath;
    m_ui->editRunPythonScript->document()->setModified(false);

    setLineEditPalette(m_ui->txtRunPythonScript, filePath);

    updateSimulationSettings();
    // The script may now read other GDS datatypes (purposelist).
    refreshLayoutPreviewIfPurposesChanged();

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

    // New groups are inserted expanded; that must not reset the user's collapsed topics.
    m_rebuildingSettingsGrid = true;
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
    // Generic settings, grouped by the topics of keywords/<tool>.csv (file order), unknown keys
    // alphabetically under "Other".
    // -------------------------------------------------------------------------------------------------
    const QString otherTopic = tr("Other");
    QStringList topics;
    QHash<QString, QString> keyTopic;
    QHash<QString, int> keyOrder;
    QSet<QString> requiredKeys;
    for (int i = 0; i < m_keywordTable.size(); ++i) {
        const KeywordEntry &e = m_keywordTable.at(i);
        if (e.required)
            requiredKeys.insert(e.keyword);
        const QString topic = e.topic.isEmpty() ? otherTopic : e.topic;
        if (topic != otherTopic && !topics.contains(topic))
            topics << topic;
        keyTopic.insert(e.keyword, topic);
        keyOrder.insert(e.keyword, i);
    }
    topics << otherTopic;

    struct Row
    {
        int topicIndex;
        int order;
        QString key;
        QtVariantProperty *prop;
    };
    QVector<Row> rows;

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

        // Tooltip: the model's own text (# @brief) or else the keyword file's description, plus
        // "Required." and "Default: ..." from the keyword file; loose variables passed to a
        // workflow parameter of another name say so.
        const QString keyword = settingKeyword(key);
        const KeywordEntry *entry = nullptr;
        for (const KeywordEntry &e : m_keywordTable)
            if (e.keyword == keyword)
                entry = &e;
        QString tip = m_curPythonData.settingTips.value(key);
        if (tip.isEmpty())
            tip = entry ? entry->description : tips.value(key);
        if (entry && entry->required)
            tip = tip.isEmpty() ? tr("Required.") : tr("Required. %1").arg(tip);
        if (entry && !entry->defaultValue.isEmpty() && !tip.contains(QLatin1String("Default:")))
            tip += (tip.isEmpty() ? QString() : QStringLiteral("\n"))
                   + tr("Default: %1").arg(entry->defaultValue);
        if (keyword != key)
            tip += (tip.isEmpty() ? QString() : QStringLiteral("\n"))
                   + tr("Passed to the workflow as: %1").arg(keyword);
        if (!tip.isEmpty())
            prop->setToolTip(tip);
        if (key.compare(QLatin1String("fdump"), Qt::CaseInsensitive) == 0
            && isElmerEmKey(currentSimToolKey())) {
            prop->setToolTip(tr("Enable field dump at all frequencies.\n"
                                "Elmer has no per-frequency SaveStep like Palace — any non-empty "
                                "fdump dumps fields at every solved frequency (sweep + fpoint)."));
        }

        if (info.propType == QVariant::Double)
            setupDoubleAttributes(prop, info);

        prop->setValue(info.value);
        // QtTreePropertyBrowser draws "modified" property names in bold: used for required keys.
        prop->setModified(requiredKeys.contains(keyword));

        const QString topic = keyTopic.value(keyword, otherTopic);
        rows.push_back({int(topics.indexOf(topic)), keyOrder.value(keyword, INT_MAX), key, prop});
    }

    std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        if (a.topicIndex != b.topicIndex)
            return a.topicIndex < b.topicIndex;
        if (a.order != b.order)
            return a.order < b.order;
        return a.key.compare(b.key, Qt::CaseInsensitive) < 0;
    });

    QtVariantProperty *group = nullptr;
    int groupTopic = -1;
    for (const Row &row : rows) {
        if (row.topicIndex != groupTopic) {
            groupTopic = row.topicIndex;
            group = m_variantManager->addProperty(QtVariantPropertyManager::groupTypeId(),
                                                  topics.at(groupTopic));
            m_settingTopicGroups.insert(group);
            m_simSettingsGroup->addSubProperty(group);
        }
        group->addSubProperty(row.prop);
    }

    // Collapsed topics stay collapsed across rebuilds (every Save rebuilds the grid).
    if (m_propertyBrowser) {
        for (QtProperty *g : m_simSettingsGroup->subProperties()) {
            const bool collapsed = m_collapsedSettingTopics.contains(g->propertyName());
            for (QtBrowserItem *item : m_propertyBrowser->items(g))
                m_propertyBrowser->setExpanded(item, !collapsed);
        }
    }
    m_rebuildingSettingsGrid = false;
    applySettingsFilter();
    updateAddSettingAvailability();

    updateBoundaryTooltipsForCurrentTool();
}

/*!*******************************************************************************************************************
 * \brief The GDS datatypes (purposes) the model's read_gds call reads, for the layout preview.
 *
 * The workflows ignore shapes on other datatypes, so the preview hides them too. The purposelist
 * argument is resolved from the script (PythonParser::readGdsPurposes); when it names a setting or a
 * loose variable, the grid's current value wins. When it can't be determined (no read_gds call, a
 * computed list), datatype 0 is assumed, as in the templates.
 *
 * \return The datatypes.
 **********************************************************************************************************************/
QSet<int> MainWindow::currentGdsPurposes() const
{
    const PythonParser::GdsPurposes ref =
            PythonParser::readGdsPurposes(m_ui->editRunPythonScript->toPlainText());
    const QString key = !ref.settingsKey.isEmpty() ? ref.settingsKey : ref.variable;
    if (!key.isEmpty() && m_simSettings.contains(key)) {
        QSet<int> fromGrid;
        if (PythonParser::parseIntList(m_simSettings.value(key).toString(), &fromGrid))
            return fromGrid;
    }
    return ref.known ? ref.purposes : QSet<int>({0});
}

/*!*******************************************************************************************************************
 * \brief currentGdsPurposes() as text ("0,2"), to see whether the preview is stale.
 **********************************************************************************************************************/
QString MainWindow::currentGdsPurposesKey() const
{
    QList<int> sorted = currentGdsPurposes().values();
    std::sort(sorted.begin(), sorted.end());
    QStringList parts;
    for (int p : sorted)
        parts << QString::number(p);
    return parts.join(QLatin1Char(','));
}

void MainWindow::refreshLayoutPreviewIfPurposesChanged()
{
    if (!m_layoutPreviewKey.isEmpty() && currentGdsPurposesKey() != m_layoutPreviewPurposes)
        refreshLayoutPreview();
}

/*!*******************************************************************************************************************
 * \brief The model's via array merge distance (merge_polygon_size), for the layout preview.
 *
 * Looks for the setting under its own name or as a loose variable passed to read_gds()'s
 * merge_polygon_size (PythonParser::Result::keywordAlias); the grid's current value wins.
 *
 * \return Distance in µm, or -1 when the model doesn't set it (or not as a plain number).
 **********************************************************************************************************************/
qreal MainWindow::currentViaMergeSize() const
{
    QStringList keys = m_curPythonData.settings.keys();
    keys += m_curPythonData.topLevel.keys();
    for (const QString &key : keys) {
        if (settingKeyword(key) != QLatin1String("merge_polygon_size"))
            continue;
        const QVariant v = m_simSettings.contains(key) ? m_simSettings.value(key)
                         : m_curPythonData.settings.contains(key) ? m_curPythonData.settings.value(key)
                                                                   : m_curPythonData.topLevel.value(key);
        bool ok = false;
        const double um = v.toString().trimmed().toDouble(&ok);
        if (ok && um >= 0.0)
            return um;
    }
    return -1.0;
}

/*!*******************************************************************************************************************
 * \brief Keyword-file name of a setting: the key itself, or the workflow parameter a loose variable is
 *        passed to (PythonParser::Result::keywordAlias).
 * \param key Setting key as written in the script.
 * \return Keyword used for topic, order and tooltip.
 **********************************************************************************************************************/
QString MainWindow::settingKeyword(const QString &key) const
{
    return m_curPythonData.keywordAlias.value(key, key);
}

/*!*******************************************************************************************************************
 * \brief Calls \a fn for every setting property in "Simulation Settings", inside the topic groups.
 * \param fn Callback; topic group properties themselves are skipped.
 **********************************************************************************************************************/
void MainWindow::forEachSimSettingProperty(const std::function<void(QtProperty *)> &fn) const
{
    if (!m_simSettingsGroup)
        return;
    std::function<void(QtProperty *)> walk = [&](QtProperty *parent) {
        for (QtProperty *child : parent->subProperties()) {
            if (!child)
                continue;
            if (m_settingTopicGroups.contains(child))
                walk(child);
            else
                fn(child);
        }
    };
    walk(m_simSettingsGroup);
}

/*!*******************************************************************************************************************
 * \brief Removes all existing sub-properties from the Simulation Settings group.
 **********************************************************************************************************************/
void MainWindow::clearSimSettingsGroup()
{
    // Topic groups hold the settings; QtProperty doesn't delete its children.
    std::function<void(QtProperty *)> deleteTree = [&](QtProperty *p) {
        const auto children = p->subProperties();
        for (QtProperty *child : children)
            deleteTree(child);
        m_settingTopicGroups.remove(p);
        delete p;
    };
    const auto children = m_simSettingsGroup->subProperties();
    for (QtProperty* child : children)
        deleteTree(child);
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

    if (items.size() != 6)
        return;

    auto normalizeForTool = [&](QString v) -> QString {
        v = v.trimmed().toUpper();

        const bool isPalace = (simTool.compare(QLatin1String("Palace"), Qt::CaseInsensitive) == 0)
                           || simTool.contains(QLatin1String("Elmer"), Qt::CaseInsensitive);
        const bool isOpenEMS = (simTool.compare(QLatin1String("OpenEMS"), Qt::CaseInsensitive) == 0);

        if (isPalace) {
            if (v == QLatin1String("MUR"))   return QStringLiteral("ABC");
            if (v == QLatin1String("PML_8")) return QStringLiteral("PML");

            if (v == QLatin1String("PEC") || v == QLatin1String("PMC") ||
                v == QLatin1String("ABC") || v == QLatin1String("PML"))
                return v;
            return QStringLiteral("PEC");
        }

        if (isOpenEMS) {
            if (v == QLatin1String("ABC")) return QStringLiteral("MUR");
            if (v == QLatin1String("PML")) return QStringLiteral("PML_8");

            if (v == QLatin1String("PEC") || v == QLatin1String("PMC") ||
                v == QLatin1String("MUR") || v == QLatin1String("PML_8"))
                return v;
            return QStringLiteral("PEC");
        }

        if (v == QLatin1String("PEC") || v == QLatin1String("PMC"))
            return v;
        return QStringLiteral("PEC");
    };

    QtProperty* boundariesGroup = nullptr;

    for (QtProperty* top : m_propertyBrowser->properties()) {
        if (top->propertyName() == QLatin1String("Boundaries")) {
            boundariesGroup = top;
            break;
        }
    }

    if (!boundariesGroup) {
        for (QtProperty* top : m_propertyBrowser->properties()) {
            for (QtProperty* sub : top->subProperties()) {
                if (sub->propertyName() == QLatin1String("Boundaries")) {
                    boundariesGroup = sub;
                    break;
                }
            }
            if (boundariesGroup) break;
        }
    }

    QStringList normalized = items;
    for (int i = 0; i < 6; ++i)
        normalized[i] = normalizeForTool(normalized[i]);

    if (boundariesGroup) {
        //QSignalBlocker b(m_variantManager);

        for (QtProperty* sub : boundariesGroup->subProperties()) {
            const QString sideName = sub->propertyName();
            const int idxSide = sides.indexOf(sideName);
            if (idxSide < 0)
                continue;

            const QString valueWanted = normalized.at(idxSide);

            const QStringList enumNames =
                m_variantManager->attributeValue(sub, QLatin1String("enumNames")).toStringList();

            int enumIndex = enumNames.indexOf(valueWanted);
            if (enumIndex < 0) {
                enumIndex = enumNames.indexOf(QStringLiteral("PEC"));
                if (enumIndex < 0) enumIndex = 0;
            }

            m_variantManager->setValue(sub, enumIndex);

            QStringList tips =
                m_variantManager->attributeValue(sub, QLatin1String("enumToolTips")).toStringList();

            if (!tips.isEmpty())
                sub->setToolTip(tips.value(enumIndex));

            onSimulationSettingChanged(sub, QVariant(enumIndex));
        }
    }

    QVariantMap bndMap;
    for (int i = 0; i < 6; ++i)
        bndMap[sides.at(i)] = normalized.at(i);

    m_simSettings[QStringLiteral("Boundaries")] = bndMap;

    updateBoundaryTooltipsForCurrentTool();

    m_propertyBrowser->setUpdatesEnabled(false);
    m_propertyBrowser->setUpdatesEnabled(true);
    m_propertyBrowser->update();
    m_propertyBrowser->repaint();
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
        key.compare(QLatin1String("RunPythonScript"),Qt::CaseInsensitive) == 0 ||
        // Edited in the Substrate tab's override table (applyVariableOverridesToScript).
        key.compare(QLatin1String("variable_overrides"), Qt::CaseInsensitive) == 0)
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QMetaType::Type t = static_cast<QMetaType::Type>(val.typeId());
    const bool isString = (t == QMetaType::QString);
    const bool isBool   = (t == QMetaType::Bool);
    const bool isIntish = (t == QMetaType::Int) || (t == QMetaType::UInt) ||
                          (t == QMetaType::LongLong) || (t == QMetaType::ULongLong);
    const bool isDouble = (t == QMetaType::Double);
    const bool isList   = (t == QMetaType::QStringList) || (t == QMetaType::QVariantList);
#else
    const QVariant::Type t = val.type();
    const bool isString = (t == QVariant::String);
    const bool isBool   = (t == QVariant::Bool);
    const bool isIntish = (t == QVariant::Int) || (t == QVariant::UInt) ||
                          (t == QVariant::LongLong) || (t == QVariant::ULongLong);
    const bool isDouble = (t == QVariant::Double);
    const bool isList   = (t == QVariant::StringList) || (t == QVariant::List);
#endif

    // Elmer EM: fdump is an on/off dump enable (dumps every solved frequency), not a Hz list.
    if (key.compare(QLatin1String("fdump"), Qt::CaseInsensitive) == 0
        && isElmerEmKey(currentSimToolKey())) {
        PalacePropInfo info;
        info.propType = QVariant::Bool;
        info.decimals = 0;
        info.step = 0.0;
        bool enabled = false;
        if (isBool) {
            enabled = val.toBool();
        } else if (isList) {
            enabled = !val.toList().isEmpty() || !val.toStringList().isEmpty();
        } else {
            const QString s = val.toString().trimmed();
            enabled = !s.isEmpty()
                    && s != QLatin1String("[]")
                    && s != QLatin1String("None")
                    && s.compare(QLatin1String("False"), Qt::CaseInsensitive) != 0;
        }
        info.value = enabled;
        return info;
    }

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
    if (key == s)
        return true;

    // Quoted strings with a '.' are file names, handled by the file settings.
    if (m_curPythonData.quotedStrings.contains(key))
        return s.contains(QLatin1Char('.'));

    // Raw expressions: hide attribute access / calls (os.path..., foo.bar),
    // but keep lists and numbers such as [1.5e9].
    static const QRegularExpression reAttrOrCall(QStringLiteral(R"([A-Za-z_]\w*\s*[.(])"));
    return reAttrOrCall.match(s).hasMatch();
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

/*!*******************************************************************************************************************
 * \brief Resolves a GDS / XML path written in a model script.
 *
 * Relative paths are taken relative to the model's folder. If the resulting file does not
 * exist (e.g. a model copied from another machine with an absolute C:/... or /mnt/... path),
 * the same file name in the model's folder is used when it exists there; the next Save then
 * writes that path into the script.
 *
 * \param scriptValue Path as written in the script (Linux, Windows or WSL form).
 * \param modelDir    Folder of the model script.
 * \return Path to use (the original one if no better match exists).
 **********************************************************************************************************************/
QString MainWindow::resolveModelInputFile(const QString &scriptValue, const QDir &modelDir)
{
    QString path = fromWslPath(scriptValue);
    if (QFileInfo(path).isRelative())
        path = modelDir.filePath(path);
    if (path.isEmpty() || QFileInfo::exists(path))
        return path;

    // File name only; split on both separators so Windows paths work on Linux too.
    const QString raw = scriptValue.trimmed();
    const int cut = qMax(raw.lastIndexOf(QLatin1Char('/')), raw.lastIndexOf(QLatin1Char('\\')));
    const QString name = raw.mid(cut + 1);
    if (name.isEmpty())
        return path;
    const QString local = modelDir.filePath(name);
    if (!QFileInfo::exists(local))
        return path;

    info(tr("'%1' not found; using '%2' from the model's folder (written to the script on Save).")
             .arg(QDir::toNativeSeparators(path), QDir::toNativeSeparators(local)),
         false);
    return local;
}

/*!*******************************************************************************************************************
 * \brief Re-reads the editor text into the settings grid after a setting was added or removed.
 *
 * Keys that left the script leave \c m_simSettings; new keys enter it. Nothing is written to disk.
 **********************************************************************************************************************/
void MainWindow::reparseEditorIntoGrid()
{
    const PythonParser::Result res =
        PythonParser::parseSettingsFromText(m_ui->editRunPythonScript->toPlainText());
    if (!res.ok)
        return;

    QSet<QString> before;
    for (const QString &k : m_curPythonData.settings.keys())
        before.insert(k);
    for (const QString &k : m_curPythonData.topLevel.keys())
        before.insert(k);
    for (const QString &k : before)
        if (!res.settings.contains(k) && !res.topLevel.contains(k))
            m_simSettings.remove(k);
    for (auto it = res.topLevel.constBegin(); it != res.topLevel.constEnd(); ++it)
        if (!m_simSettings.contains(it.key()))
            m_simSettings.insert(it.key(), it.value());
    for (auto it = res.settings.constBegin(); it != res.settings.constEnd(); ++it)
        if (!m_simSettings.contains(it.key()))
            m_simSettings.insert(it.key(), it.value());

    m_curPythonData = res;
    rebuildSimulationSettingsFromPalace(res.settings, mergeTipsPreferModel(res.settingTips, m_keywordTips),
                                        res.topLevel);
}

/*!*******************************************************************************************************************
 * \brief Adds \c dict['key'] = value to the model script where it fits by topic, then shows it in the grid.
 *
 * Pending grid edits are written into the editor first, so nothing is lost. Nothing is saved.
 *
 * \param key     New key.
 * \param pyValue Python literal.
 * \return False when the model has no settings dict.
 **********************************************************************************************************************/
bool MainWindow::addSetting(const QString &key, const QString &pyValue)
{
    syncGuiSettingsToPythonEditor();
    QString script = m_ui->editRunPythonScript->toPlainText();
    if (!insertSettingIntoScript(script, key, pyValue)) {
        error(tr("This model has no settings dict, so a new setting would not reach the workflow."), false);
        return false;
    }
    setEditorScriptPreservingState(script);
    reparseEditorIntoGrid();
    setStateChanged();
    info(tr("Added %1['%2'] = %3 (saved with the model).").arg(settingsDictName(script), key, pyValue), false);
    return true;
}

/*!*******************************************************************************************************************
 * \brief Opens the Add Setting dialog (optionally on one topic) and adds the chosen setting.
 *
 * \param presetTopic Topic to show first; empty for all topics.
 **********************************************************************************************************************/
void MainWindow::openAddSettingDialog(const QString &presetTopic)
{
    updateAddSettingAvailability();
    if (m_btnAddSetting && !m_btnAddSetting->isEnabled()) {
        error(m_btnAddSetting->toolTip(), false);
        return;
    }
    QSet<QString> present;
    for (const QString &k : m_curPythonData.settings.keys())
        present << k << settingKeyword(k);
    for (const QString &k : m_curPythonData.topLevel.keys())
        present << k << settingKeyword(k);
    QVector<AddSettingDialog::Keyword> keywords;
    for (const KeywordEntry &e : m_keywordTable)
        keywords.push_back({e.keyword, e.description, e.topic, e.defaultValue, e.required});

    AddSettingDialog dlg(keywords, present, presetTopic, this);
    if (dlg.exec() == QDialog::Accepted)
        addSetting(dlg.key(), dlg.pythonValue());
}

/*!*******************************************************************************************************************
 * \brief Removes a setting's line from the model script (after confirmation) and from the grid.
 * \param key Setting key.
 **********************************************************************************************************************/
void MainWindow::removeSetting(const QString &key)
{
    syncGuiSettingsToPythonEditor();
    QString script = m_ui->editRunPythonScript->toPlainText();
    QString why;
    if (!canRemoveSetting(script, key, &why)) {
        error(tr("'%1' can't be removed: %2").arg(key, why), false);
        return;
    }
#ifndef EMSTUDIO_TESTING
    if (QMessageBox::question(this, tr("Remove Setting"),
                              tr("Remove '%1' from the model? The workflow then uses its default.").arg(key),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
#endif
    removeSettingFromScript(script, key);
    setEditorScriptPreservingState(script);
    m_simSettings.remove(key);
    reparseEditorIntoGrid();
    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Sets a setting to the workflow default from the keyword file (in the script and the grid).
 * \param key Setting key.
 **********************************************************************************************************************/
void MainWindow::resetSettingToDefault(const QString &key)
{
    const QString keyword = settingKeyword(key);
    QString def;
    for (const KeywordEntry &e : m_keywordTable)
        if (e.keyword == keyword)
            def = e.defaultValue;
    if (def.isEmpty())
        return;
    syncGuiSettingsToPythonEditor();
    QString script = m_ui->editRunPythonScript->toPlainText();
    if (!writeSettingValueToScript(script, key, def))
        return;
    setEditorScriptPreservingState(script);
    m_simSettings.remove(key);  // take the value from the script again
    reparseEditorIntoGrid();
    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Actions of the settings grid context menu for \a prop (topic group or setting).
 *
 * \param prop Property under the cursor.
 * \param menu Menu to fill (actions are parented to it).
 **********************************************************************************************************************/
void MainWindow::fillSettingsContextMenu(QtProperty *prop, QMenu *menu)
{
    if (!prop || !menu)
        return;
    menu->setToolTipsVisible(true);
    const bool isTopic = m_settingTopicGroups.contains(prop);
    QString topic;
    QString key;
    if (isTopic) {
        topic = prop->propertyName();
    } else {
        forEachSimSettingProperty([&](QtProperty *p) {
            if (p == prop)
                key = p->propertyName();
        });
        if (key.isEmpty())
            return;  // e.g. Boundaries
        for (QtProperty *g : m_simSettingsGroup->subProperties())
            if (g->subProperties().contains(prop))
                topic = g->propertyName();
    }

    QAction *add = menu->addAction(tr("Add setting to %1...").arg(topic));
    add->setObjectName(QStringLiteral("settingsAddToTopic"));
    // "Other" lists all keywords: custom keys land there.
    const QString preset = topic == tr("Other") ? QString() : topic;
    connect(add, &QAction::triggered, this, [this, preset]() { openAddSettingDialog(preset); });
    add->setEnabled(!m_btnAddSetting || m_btnAddSetting->isEnabled());

    if (isTopic) {
        menu->addSeparator();
        menu->addAction(tr("Collapse all topics"), this, [this]() {
            for (QtProperty *g : m_simSettingsGroup->subProperties())
                for (QtBrowserItem *item : m_propertyBrowser->items(g))
                    m_propertyBrowser->setExpanded(item, false);
        });
        menu->addAction(tr("Expand all topics"), this, [this]() {
            for (QtProperty *g : m_simSettingsGroup->subProperties())
                for (QtBrowserItem *item : m_propertyBrowser->items(g))
                    m_propertyBrowser->setExpanded(item, true);
        });
        return;
    }

    menu->addSeparator();
    QString def;
    for (const KeywordEntry &e : m_keywordTable)
        if (e.keyword == settingKeyword(key))
            def = e.defaultValue;
    QAction *reset = menu->addAction(def.isEmpty() ? tr("Reset to default")
                                                   : tr("Reset to default (%1)").arg(def));
    reset->setObjectName(QStringLiteral("settingsReset"));
    reset->setEnabled(!def.isEmpty());
    if (def.isEmpty())
        reset->setToolTip(tr("The keyword file has no default for this setting."));
    connect(reset, &QAction::triggered, this, [this, key]() { resetSettingToDefault(key); });

    QAction *remove = menu->addAction(tr("Remove from model"));
    remove->setObjectName(QStringLiteral("settingsRemove"));
    QString why;
    remove->setEnabled(canRemoveSetting(m_ui->editRunPythonScript->toPlainText(), key, &why));
    remove->setToolTip(why);
    connect(remove, &QAction::triggered, this, [this, key]() { removeSetting(key); });
}

/*!*******************************************************************************************************************
 * \brief Right-click in the settings grid: menu for the topic or setting under the cursor.
 * \param pos Position in the grid's tree widget.
 **********************************************************************************************************************/
void MainWindow::showSettingsContextMenu(const QPoint &pos)
{
    auto *tree = m_propertyBrowser ? m_propertyBrowser->findChild<QTreeWidget *>() : nullptr;
    if (!tree)
        return;
    if (QTreeWidgetItem *it = tree->itemAt(pos))
        tree->setCurrentItem(it);  // the browser's current item follows
    QtBrowserItem *bi = m_propertyBrowser->currentItem();
    if (!bi)
        return;
    QMenu menu(this);
    fillSettingsContextMenu(bi->property(), &menu);
    if (!menu.isEmpty())
        menu.exec(tree->viewport()->mapToGlobal(pos));
}

/*!*******************************************************************************************************************
 * \brief Shows only settings whose name contains the filter text (empty topics are hidden).
 **********************************************************************************************************************/
void MainWindow::applySettingsFilter()
{
    if (!m_simSettingsGroup || !m_propertyBrowser)
        return;
    const QString text = m_settingsFilter ? m_settingsFilter->text().trimmed() : QString();
    for (QtProperty *group : m_simSettingsGroup->subProperties()) {
        bool any = false;
        for (QtProperty *p : group->subProperties()) {
            const bool show = text.isEmpty() || p->propertyName().contains(text, Qt::CaseInsensitive);
            any = any || show;
            for (QtBrowserItem *item : m_propertyBrowser->items(p))
                m_propertyBrowser->setItemVisible(item, show);
        }
        for (QtBrowserItem *item : m_propertyBrowser->items(group))
            m_propertyBrowser->setItemVisible(item, any);
    }
}

/*!*******************************************************************************************************************
 * \brief Enables "Add setting" only for models with a settings dict; explains why otherwise.
 **********************************************************************************************************************/
void MainWindow::updateAddSettingAvailability()
{
    if (!m_btnAddSetting)
        return;
    const QString script = m_ui->editRunPythonScript->toPlainText();
    const bool ok = !settingsDictName(script).isEmpty();
    m_btnAddSetting->setEnabled(ok);
    m_btnAddSetting->setToolTip(
        ok ? tr("Add a setting from the keyword list (by topic) or a custom one.\n"
                "Right-click a topic or setting for more.")
           : script.trimmed().isEmpty()
                 ? tr("No model loaded.")
                 : tr("This model passes plain variables to the workflow functions instead of a settings "
                      "dict, so a new variable would have no effect. Use the settings{} style "
                      "(File > New) to add settings here."));
}
