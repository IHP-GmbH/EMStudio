/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
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

#include "simcfgimport.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>

namespace {

/*! Frequency keys: GHz in a .simcfg, Hz in the model script. */
bool isFrequencyKey(const QString &key)
{
    return key == QLatin1String("fstart") || key == QLatin1String("fstop") || key == QLatin1String("fstep");
}

bool isFrequencyListKey(const QString &key)
{
    return key == QLatin1String("fpoint") || key == QLatin1String("fdump");
}

QString number(double v)
{
    return QString::number(v, 'g', 12);
}

QString quoted(const QString &s)
{
    QString t = s;
    t.replace(QLatin1Char('\\'), QLatin1Char('/'));     // Windows paths, as setupEM writes them
    t.replace(QLatin1Char('\''), QLatin1String("\\'"));
    return QLatin1Char('\'') + t + QLatin1Char('\'');
}

QString literal(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Bool:
        return v.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    case QJsonValue::Double:
        return number(v.toDouble());
    case QJsonValue::String:
        return quoted(v.toString());
    case QJsonValue::Array: {
        QStringList items;
        for (const QJsonValue &item : v.toArray())
            items << literal(item);
        return QLatin1Char('[') + items.join(QStringLiteral(", ")) + QLatin1Char(']');
    }
    case QJsonValue::Object: {
        QStringList items;
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            items << quoted(it.key()) + QStringLiteral(": ") + literal(it.value());
        return QLatin1Char('{') + items.join(QStringLiteral(", ")) + QLatin1Char('}');
    }
    default:
        return QStringLiteral("None");
    }
}

QString jsonString(const QJsonValue &v)
{
    return v.isString() ? v.toString() : QString();
}

} // namespace

namespace SimcfgImport {

/*!*******************************************************************************************************************
 * \brief Keys of \c saved_values that are no model settings.
 *
 * GdsFile, SubstrateFile and cellname go to the Layout File / Stackup / Top Cell inputs; model_basename and sim_path
 * are derived by the script itself; fdump_enabled is setupEM's Elmer checkbox and iterative an Elmer solver option.
 * setupEM leaves the same keys out when it writes a Palace script.
 *
 * \param key Key of \c saved_values.
 * \return True if the key is not written as a setting.
 **********************************************************************************************************************/
bool isSkippedKey(const QString &key)
{
    static const QStringList skipped = {
        QStringLiteral("GdsFile"), QStringLiteral("SubstrateFile"), QStringLiteral("cellname"),
        QStringLiteral("model_basename"), QStringLiteral("sim_path"), QStringLiteral("fdump_enabled"),
        QStringLiteral("iterative")};
    return skipped.contains(key);
}

/*!*******************************************************************************************************************
 * \brief Python literal of a .simcfg value, as setupEM writes it into a model script.
 *
 * fstart/fstop/fstep and the fpoint/fdump lists are GHz in setupEM and Hz in the script: the literal gets "e9",
 * e.g. 2.5 → 2.5e9. Strings are quoted with backslashes turned into slashes, lists and dicts are written element
 * by element, null is None.
 *
 * \param key   Setting key (decides the frequency conversion).
 * \param value Value from \c saved_values.
 * \return Python literal.
 **********************************************************************************************************************/
QString pythonLiteral(const QString &key, const QJsonValue &value)
{
    if (isFrequencyKey(key) && value.isDouble())
        return number(value.toDouble()) + QStringLiteral("e9");
    if (isFrequencyListKey(key) && value.isArray()) {
        QStringList items;
        for (const QJsonValue &item : value.toArray())
            items << (item.isDouble() ? number(item.toDouble()) + QStringLiteral("e9") : literal(item));
        return QLatin1Char('[') + items.join(QStringLiteral(", ")) + QLatin1Char(']');
    }
    return literal(value);
}

/*!*******************************************************************************************************************
 * \brief Reads a .simcfg file.
 *
 * \param path Path of the .simcfg file.
 * \return Its content; \c ok is false with an \c error text when the file cannot be read or is no setupEM file.
 **********************************************************************************************************************/
Data read(const QString &path)
{
    Data data;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        data.error = QObject::tr("cannot open the file: %1").arg(file.errorString());
        return data;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull()) {
        data.error = QObject::tr("not a JSON file: %1").arg(parseError.errorString());
        return data;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("application")).toString() != QLatin1String("setupEM")
        || !root.value(QStringLiteral("saved_values")).isObject()) {
        data.error = QObject::tr("not a setupEM settings file (application \"setupEM\" with saved_values)");
        return data;
    }

    const QJsonObject values = root.value(QStringLiteral("saved_values")).toObject();
    data.values = values;
    data.gdsFile = jsonString(values.value(QStringLiteral("GdsFile")));
    data.substrateFile = jsonString(values.value(QStringLiteral("SubstrateFile")));
    data.cellname = jsonString(values.value(QStringLiteral("cellname")));
    data.elmerMode = root.value(QStringLiteral("elmer_mode")).toBool(false);
    // alphabetical (QJsonObject); the import places each key by the keyword file's order anyway
    for (const QString &key : values.keys()) {
        if (!isSkippedKey(key))
            data.settings.append({key, pythonLiteral(key, values.value(key))});
    }

    for (const QJsonValue &v : root.value(QStringLiteral("ports")).toArray()) {
        const QJsonObject o = v.toObject();
        Port p;
        p.portnumber = o.value(QStringLiteral("portnumber")).toInt();
        p.sourceLayer = o.value(QStringLiteral("source_layernum")).toInt();
        p.voltage = o.value(QStringLiteral("voltage")).toDouble(1.0);
        p.z0 = o.value(QStringLiteral("port_Z0")).toDouble(50.0);
        p.fromLayer = jsonString(o.value(QStringLiteral("from_layername")));
        p.toLayer = jsonString(o.value(QStringLiteral("to_layername")));
        p.targetLayer = jsonString(o.value(QStringLiteral("target_layername")));
        p.direction = jsonString(o.value(QStringLiteral("direction")));
        data.ports.append(p);
    }
    data.ok = true;
    return data;
}

/*!*******************************************************************************************************************
 * \brief The settings of a .simcfg that a new model for \a simKey gets.
 *
 * Palace: all of them (\c data.settings; setupEM writes gds2palace settings). OpenEMS: only those that mean the same in
 * gds2openEMS: unit, purpose, GDS preprocessing and via merging, fill factor correction, fstart and fstop; setupEM's
 * fstep becomes numfreq. The FEM mesh, solver and boundary settings (refined_cellsize, order, boundary, margin, ...)
 * mean something else in FDTD or don't exist there: the openEMS template's values stay.
 *
 * \param data    Content of the .simcfg.
 * \param simKey  "palace" or "openems".
 * \param skipped Optional: receives the keys that are left out.
 * \return key -> Python literal.
 **********************************************************************************************************************/
QVector<QPair<QString, QString>> settingsFor(const Data &data, const QString &simKey, QStringList *skipped)
{
    if (simKey != QLatin1String("openems"))
        return data.settings;
    static const QStringList shared = {
        QStringLiteral("unit"), QStringLiteral("purpose"), QStringLiteral("preprocess_gds"),
        QStringLiteral("merge_polygon_size"), QStringLiteral("fill_factor_correction"),
        QStringLiteral("fstart"), QStringLiteral("fstop")};
    QVector<QPair<QString, QString>> out;
    for (const auto &kv : data.settings) {
        if (shared.contains(kv.first))
            out.append(kv);
        else if (skipped && kv.first != QLatin1String("fstep"))
            skipped->append(kv.first);
    }
    const double fstart = data.values.value(QStringLiteral("fstart")).toDouble(0.0);
    const QJsonValue fstop = data.values.value(QStringLiteral("fstop"));
    const double fstep = data.values.value(QStringLiteral("fstep")).toDouble(0.0);
    if (fstop.isDouble() && fstep > 0.0 && fstop.toDouble() > fstart)
        out.append({QStringLiteral("numfreq"),
                    QString::number(qRound((fstop.toDouble() - fstart) / fstep) + 1)});
    return out;
}

} // namespace SimcfgImport

/*!*******************************************************************************************************************
 * \brief Adds File → Import setupEM Config (.simcfg)... below Load Python Model.
 **********************************************************************************************************************/
void MainWindow::setupSimcfgImportAction()
{
    if (!m_ui->menuFile || m_ui->menuFile->findChild<QAction *>(QStringLiteral("actionImportSimcfg")))
        return;
    QAction *a = new QAction(tr("Import setupEM Config (.simcfg)..."), this);
    a->setObjectName(QStringLiteral("actionImportSimcfg"));
    a->setToolTip(tr("Palace or OpenEMS model from a setupEM settings file, e.g. an export of the KLayout plug-in "
                     "klayout_em_export. If <name>.py exists next to it, only its ports, layout file, stackup, "
                     "top cell and purpose are updated."));
    connect(a, &QAction::triggered, this, [this]() {
        const QString start = m_sysSettings.value(QStringLiteral("LastSimcfgDir")).toString();
        const QString path = QFileDialog::getOpenFileName(this, tr("Import setupEM Config"), start,
                                                          tr("setupEM settings (*.simcfg);;All files (*)"));
        if (path.isEmpty())
            return;
        m_sysSettings[QStringLiteral("LastSimcfgDir")] = QFileInfo(path).absolutePath();
        importSimcfg(path);
    });
    m_ui->menuFile->insertAction(m_ui->actionRecent, a);
}

/*!*******************************************************************************************************************
 * \brief The model a .simcfg belongs to: <name>.py in the same folder as <name>.simcfg.
 *
 * \param simcfgPath Path of the .simcfg file.
 * \return Path of the model script (may not exist).
 **********************************************************************************************************************/
QString MainWindow::simcfgModelPath(const QString &simcfgPath)
{
    const QFileInfo fi(simcfgPath);
    return fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".py"));
}

/*!*******************************************************************************************************************
 * \brief Imports a setupEM settings file (*.simcfg) as a Palace or OpenEMS model.
 *
 * Without <name>.py next to the file: a new model from the default template of \a simKey (asked when empty, see
 * askSimcfgImportTool), with the files, cell and ports, and the settings that tool shares with setupEM
 * (SimcfgImport::settingsFor: all for Palace; frequencies converted from GHz to Hz). Its path is set to <name>.py, so
 * a later import of the same .simcfg finds it; nothing is saved.
 *
 * With <name>.py: that model is loaded and only its ports, layout file, stackup, top cell and purpose are replaced,
 * without asking. Everything else (frequencies, mesh, boundaries, your own code) stays: a re-export from KLayout
 * changes the layout and the ports, while the simulation settings are edited here. Nothing is saved either.
 *
 * \param path   Path of the .simcfg file.
 * \param simKey "palace" or "openems" for a new model; empty: ask.
 * \return True on success.
 **********************************************************************************************************************/
bool MainWindow::importSimcfg(const QString &path, const QString &simKeyIn)
{
    const SimcfgImport::Data data = SimcfgImport::read(path);
    if (!data.ok) {
        error(tr("Cannot import %1: %2").arg(path, data.error), false);
        return false;
    }
    if (!confirmDiscardUnsavedModel(tr("Import setupEM Config"), tr("Discard them and import %1?").arg(path)))
        return false;

    const QString modelPath = simcfgModelPath(path);
    const bool update = QFileInfo::exists(modelPath);
    QString simKey;                             // the new model's tool; an existing model keeps its own
    if (update) {
        if (!loadPythonModel(modelPath))
            return false;
    } else {
        simKey = simKeyIn.isEmpty() ? askSimcfgImportTool(path) : simKeyIn.toLower();
        if (simKey.isEmpty())
            return false;
        const int idx = m_ui->cbxSimTool->findData(simKey);
        if (idx < 0 || !m_ui->cbxSimTool->isEnabled()) {
            error(tr("%1 is not configured. Set its path in Setup → Preferences.")
                      .arg(simKey == QLatin1String("openems") ? tr("OpenEMS") : tr("Palace")), false);
            return false;
        }
        m_ui->cbxSimTool->setCurrentIndex(idx);
        refreshKeywordTipsForCurrentTool();     // the tool's keyword order places inserted settings; no signal
                                                // when the tool was already selected
        clearModelInputs();
        clearSimulationLog(false);
        if (!generateDefaultModelScript(false))
            return false;
        m_ui->txtRunPythonScript->setText(modelPath);
        m_simSettings[QStringLiteral("RunPythonScript")] = modelPath;
        reparseEditorIntoGrid();        // the template's settings and where they are written (writeMode)
    }
    if (data.elmerMode && !update)
        info(tr("%1 was saved in setupEM's Elmer mode; it is imported as a %2 model.")
                 .arg(path, m_ui->cbxSimTool->currentText()), false);

    // Layout file, cell, stackup, then the ports (their layer combos need the GDS layers and stackup names)
    m_simSettings[QStringLiteral("TopCell")] = data.cellname;
    setGdsFile(data.gdsFile);
    setTopCell(data.cellname);
    setSubstrateFile(data.substrateFile);
    rebuildLayerMapping();

    QVector<PortInfo> ports;
    for (const SimcfgImport::Port &p : data.ports) {
        PortInfo pi;
        pi.portnumber = p.portnumber;
        pi.voltage = p.voltage;
        pi.z0 = p.z0;
        pi.sourceLayer = QString::number(p.sourceLayer);
        pi.sourceIsNumber = true;
        pi.fromLayer = p.targetLayer.isEmpty() ? p.fromLayer : QString();
        pi.toLayer = p.targetLayer.isEmpty() ? p.toLayer : p.targetLayer;    // in-plane: target in To Layer
        pi.direction = p.direction.toLower();       // the sign stays: the exporter chose the polarity
        ports.append(pi);
    }
    m_ui->tblPorts->setRowCount(0);
    appendParsedPortsToTable(ports);
    syncGuiSettingsToPythonEditor();

    // Settings: for a new model those its tool shares with setupEM's (all for Palace), for an existing one only
    // purpose (it belongs to the layout)
    QString script = m_ui->editRunPythonScript->toPlainText();
    QStringList notWritten, skipped;
    const auto settings = update ? data.settings : SimcfgImport::settingsFor(data, simKey, &skipped);
    for (const auto &kv : settings) {
        if (update && kv.first != QLatin1String("purpose"))
            continue;
        if (sameSettingValue(kv.first, kv.second))
            continue;                           // e.g. 1e-6 stays as written, not 1e-06
        if (!writeSettingValueToScript(script, kv.first, kv.second)
            && !insertSettingIntoScript(script, kv.first, kv.second)) {
            notWritten << kv.first;
            continue;
        }
        m_simSettings.remove(kv.first);         // the re-parse below takes the new value
    }
    setEditorScriptPreservingState(script);
    reparseEditorIntoGrid();
    setStateChanged();

    if (!notWritten.isEmpty())
        error(tr("Not written (the model has no settings dict): %1").arg(notWritten.join(QStringLiteral(", "))),
              false);
    if (update)
        info(tr("Updated %1 from %2: ports, layout file, stackup, top cell and purpose. Its other settings are "
                "kept. Save (Ctrl+S) to keep the changes.").arg(modelPath, path), false);
    else
        info(tr("New %1 model from %2 (%3 ports). Save (Ctrl+S) writes it to %4.")
                 .arg(m_ui->cbxSimTool->currentText(), path).arg(data.ports.size()).arg(modelPath), false);
    if (!skipped.isEmpty())
        info(tr("Not taken from the .simcfg (setupEM's Palace settings, which mean something else for %1): %2")
                 .arg(m_ui->cbxSimTool->currentText(), skipped.join(QStringLiteral(", "))), false);
    return true;
}

/*!*******************************************************************************************************************
 * \brief Asks which tool a new model from a .simcfg is for: Palace or OpenEMS, of those configured.
 *
 * With only one of them configured, that one is taken without asking. Test builds take m_testSimcfgImportTool
 * (default palace).
 *
 * \param path Path of the .simcfg file, for the question.
 * \return "palace", "openems", or empty when cancelled or none is configured.
 **********************************************************************************************************************/
QString MainWindow::askSimcfgImportTool(const QString &path)
{
    QStringList tools;
    for (const QString &key : {QStringLiteral("palace"), QStringLiteral("openems")})
        if (m_ui->cbxSimTool->findData(key) >= 0)
            tools << key;
    if (tools.isEmpty()) {
        error(tr("Neither Palace nor OpenEMS is configured. Set one of them in Setup → Preferences."), false);
        return QString();
    }
#ifdef EMSTUDIO_TESTING
    Q_UNUSED(path);
    const QString wanted = m_testSimcfgImportTool.isEmpty() ? QStringLiteral("palace") : m_testSimcfgImportTool;
    return tools.contains(wanted) ? wanted : tools.first();
#else
    if (tools.size() == 1)
        return tools.first();
    QMessageBox box(QMessageBox::Question, tr("Import setupEM Config"),
                    tr("Create a new model from %1 for which simulator?\n\n"
                       "Palace (FEM) gets all settings of the .simcfg. OpenEMS (FDTD) gets the layout, ports and "
                       "frequencies; its mesh and boundaries start from the OpenEMS template.")
                        .arg(QFileInfo(path).fileName()),
                    QMessageBox::Cancel, this);
    QPushButton *palace = box.addButton(tr("Palace"), QMessageBox::AcceptRole);
    QPushButton *openems = box.addButton(tr("OpenEMS"), QMessageBox::AcceptRole);
    box.setDefaultButton(palace);
    box.exec();
    if (box.clickedButton() == palace)
        return QStringLiteral("palace");
    if (box.clickedButton() == openems)
        return QStringLiteral("openems");
    return QString();
#endif
}

/*!*******************************************************************************************************************
 * \brief Does the model script already set \a key to this value?
 *
 * Numbers compare by value (1e-6 equals 1e-06, 50e9 equals 5e10), anything else by its text without spaces.
 *
 * \param key       Setting key.
 * \param pyLiteral New value as Python literal.
 * \return True if the script's value is the same, so the line can stay as written.
 **********************************************************************************************************************/
bool MainWindow::sameSettingValue(const QString &key, const QString &pyLiteral) const
{
    const auto it = m_curPythonData.settings.constFind(key);
    if (it == m_curPythonData.settings.constEnd())
        return false;
    bool okOld = false, okNew = false;
    const double oldValue = it.value().toString().toDouble(&okOld);
    const double newValue = pyLiteral.toDouble(&okNew);
    if (okOld && okNew)
        return qFuzzyCompare(1.0 + oldValue, 1.0 + newValue);
    QString a = it.value().toString(), b = pyLiteral;
    a.remove(QLatin1Char(' '));
    b.remove(QLatin1Char(' '));
    a.replace(QLatin1Char('"'), QLatin1Char('\''));
    b.replace(QLatin1Char('"'), QLatin1Char('\''));
    return a == b;
}

/*!*******************************************************************************************************************
 * \brief Saves the current model to a given path without a dialog (command line -save).
 *
 * \param path Path of the model script to write.
 * \return True if the file was written.
 **********************************************************************************************************************/
bool MainWindow::saveModelAs(const QString &path)
{
    const QString absPath = QFileInfo(path).absoluteFilePath();
    m_ui->txtRunPythonScript->setText(absPath);
    m_simSettings[QStringLiteral("RunPythonScript")] = absPath;
    on_actionSave_triggered();
    return QFileInfo::exists(absPath) && !isStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Text of the main log, for the command line: -save prints it when the import fails.
 *
 * \return Log text.
 **********************************************************************************************************************/
QString MainWindow::mainLogText() const
{
    return m_ui && m_ui->txtLog ? m_ui->txtLog->toPlainText() : QString();
}
