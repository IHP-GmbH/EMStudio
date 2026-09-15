/************************************************************************
 *  EMStudio – Elmer EM / Thermal excitation UI helpers (MainWindow).
 ************************************************************************/

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QIODevice>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProcess>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

bool MainWindow::isElmerFamilyKey(const QString &key) const
{
    const QString k = normalizeSimToolKey(key);
    return k == QLatin1String("elmer_em") || k == QLatin1String("elmer_thermal");
}

bool MainWindow::isElmerThermalKey(const QString &key) const
{
    return normalizeSimToolKey(key) == QLatin1String("elmer_thermal");
}

bool MainWindow::isElmerEmKey(const QString &key) const
{
    return normalizeSimToolKey(key) == QLatin1String("elmer_em");
}

QString MainWindow::normalizeSimToolKey(const QString &key) const
{
    const QString k = key.trimmed().toLower();
    if (k == QLatin1String("elmer"))
        return QStringLiteral("elmer_em"); // legacy preference / scripts
    return k;
}

void MainWindow::setupThermalObjectsUi()
{
    if (!m_ui || !m_ui->verticalLayout_6 || m_tblThermalObjects)
        return;

    m_tblThermalObjects = new QTableWidget(m_ui->tab_2);
    m_tblThermalObjects->setColumnCount(4);
    m_tblThermalObjects->setHorizontalHeaderLabels(
        QStringList() << tr("Type") << tr("Value") << tr("Source Layer") << tr("Target Layer"));
    m_tblThermalObjects->horizontalHeader()->setStretchLastSection(true);
    m_tblThermalObjects->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tblThermalObjects->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tblThermalObjects->setVisible(false);

    // Insert above the ports table so show/hide keeps button row shared.
    m_ui->verticalLayout_6->insertWidget(0, m_tblThermalObjects);
}

void MainWindow::updateExcitationUiForCurrentTool()
{
    if (!m_ui)
        return;

    setupThermalObjectsUi();

    const bool thermal = isElmerThermalKey(currentSimToolKey());
    if (m_ui->tblPorts)
        m_ui->tblPorts->setVisible(!thermal);
    if (m_tblThermalObjects)
        m_tblThermalObjects->setVisible(thermal);
    if (m_ui->cbSubLayerNames)
        m_ui->cbSubLayerNames->setVisible(!thermal);

    const QString portsTitle = thermal ? QStringLiteral("Thermal") : QStringLiteral("Ports");

    // Stable index from setupTabMapping (tabSettings usually shows only one page).
    int portsWidgetIdx = -1;
    for (int i = 0; i < m_tabWidgets.size(); ++i) {
        if (m_tabWidgets[i] == m_ui->tab_2) {
            portsWidgetIdx = i;
            break;
        }
    }
    if (portsWidgetIdx >= 0 && portsWidgetIdx < m_tabTitles.size())
        m_tabTitles[portsWidgetIdx] = portsTitle;

    const int shownIdx = m_ui->tabSettings->indexOf(m_ui->tab_2);
    if (shownIdx >= 0)
        m_ui->tabSettings->setTabText(shownIdx, portsTitle);

    // Keep Run Control navigation in sync (item text drives m_tabMap lookup).
    if (portsWidgetIdx >= 0) {
        m_tabMap.insert(QStringLiteral("Ports"), portsWidgetIdx);
        m_tabMap.insert(QStringLiteral("Thermal"), portsWidgetIdx);
    }

    if (m_ui->lstRunControl) {
        for (int i = 0; i < m_ui->lstRunControl->count(); ++i) {
            QListWidgetItem *it = m_ui->lstRunControl->item(i);
            if (!it)
                continue;
            const QString t = it->text();
            if (t == QLatin1String("Ports") || t == QLatin1String("Thermal"))
                it->setText(portsTitle);
            else if (t == QLatin1String("Results"))
                it->setHidden(thermal);
        }
    }

    // Leave Results page if it is visible while switching to Elmer Thermal.
    if (thermal && m_ui->tabSettings->count() > 0
        && m_ui->tabSettings->tabText(0).compare(QStringLiteral("Results"), Qt::CaseInsensitive) == 0) {
        const int simulateIdx = m_tabMap.value(QStringLiteral("Simulate"), -1);
        if (simulateIdx >= 0)
            showTab(simulateIdx);
    }
}

QString MainWindow::findThermalResultsVtu(const QString &runDir) const
{
    if (runDir.isEmpty())
        return {};

    auto pickBest = [](const QStringList &files) -> QString {
        if (files.isEmpty())
            return {};
        for (const QString &f : files) {
            if (QFileInfo(f).fileName().startsWith(QStringLiteral("thermal_results"),
                                                   Qt::CaseInsensitive))
                return f;
        }
        return files.first();
    };

    QStringList found;
    QDir base(runDir);
    if (base.exists()) {
        const QFileInfoList local = base.entryInfoList(QStringList{QStringLiteral("*.vtu")},
                                                       QDir::Files, QDir::Name);
        for (const QFileInfo &fi : local)
            found << fi.absoluteFilePath();

        if (found.isEmpty()) {
            QDirIterator it(runDir, QStringList{QStringLiteral("*.vtu")},
                            QDir::Files, QDirIterator::Subdirectories);
            int guard = 0;
            while (it.hasNext() && guard++ < 64)
                found << it.next();
        }
    }

    return pickBest(found);
}

/*!*******************************************************************************************************************
 * \brief After a successful Elmer Thermal run: Substrate tab + Field view at hottest Z.
 *
 * Uses Layout Field (PyVista slice export) instead of an external viewer.
 **********************************************************************************************************************/
void MainWindow::openThermalResultsInFieldView(const QString &runDir)
{
    if (m_headless)
        return;

    QString dir = runDir.trimmed();
    if (dir.isEmpty())
        dir = resolveResultsDirectory();

    m_fieldDumpSearchDir = dir;
    m_fieldLastDumpPath.clear();
    m_fieldPreferAutoZ = true;

    const QString vtu = findThermalResultsVtu(dir);
    if (vtu.isEmpty()) {
        appendToSimulationLog(
            QStringLiteral("\n[Field] No thermal .vtu found under:\n  %1\n"
                           "  Expected thermal_results*.vtu after a successful Elmer Thermal run.\n")
                .arg(dir.isEmpty() ? QStringLiteral("(empty)") : dir)
                .toUtf8());
    } else {
        appendToSimulationLog(
            QStringLiteral("\n[Field] Opening thermal results in Layout Field view:\n  %1\n")
                .arg(vtu)
                .toUtf8());
    }

    const int subIdx = m_tabMap.value(QStringLiteral("Substrate"), -1);
    if (subIdx >= 0)
        showTab(subIdx);

    if (!m_ui || !m_ui->layoutView)
        return;

    if (!m_ui->layoutView->isFieldMode())
        m_ui->layoutView->setFieldMode(true);
    else
        scheduleFieldOverlayRefresh(true);
}

void MainWindow::appendThermalObjectRow(const QString &type,
                                        double value,
                                        int sourceLayer,
                                        const QString &targetLayer)
{
    setupThermalObjectsUi();
    if (!m_tblThermalObjects)
        return;

    const int row = m_tblThermalObjects->rowCount();
    m_tblThermalObjects->insertRow(row);

    auto *typeBox = new QComboBox(m_tblThermalObjects);
    typeBox->addItems(QStringList() << QStringLiteral("heatsource") << QStringLiteral("consttemp"));
    const int typeIdx = typeBox->findText(type, Qt::MatchFixedString);
    typeBox->setCurrentIndex(typeIdx >= 0 ? typeIdx : 0);
    m_tblThermalObjects->setCellWidget(row, 0, typeBox);

    m_tblThermalObjects->setItem(row, 1, new QTableWidgetItem(QString::number(value, 'g', 12)));
    m_tblThermalObjects->setItem(row, 2, new QTableWidgetItem(QString::number(sourceLayer)));

    auto *targetBox = new QComboBox(m_tblThermalObjects);
    targetBox->setEditable(true);
    targetBox->addItem(QString());
    QStringList names = m_subLayers;
    names.removeDuplicates();
    std::sort(names.begin(), names.end(),
              [](const QString &a, const QString &b) {
                  return QString::localeAwareCompare(a, b) < 0;
              });
    for (const QString &nm : names)
        targetBox->addItem(nm);
    if (!targetLayer.isEmpty()) {
        int idx = targetBox->findText(targetLayer);
        if (idx < 0) {
            targetBox->addItem(targetLayer);
            idx = targetBox->findText(targetLayer);
        }
        if (idx >= 0)
            targetBox->setCurrentIndex(idx);
    }
    m_tblThermalObjects->setCellWidget(row, 3, targetBox);
}

void MainWindow::addThermalObjectRow()
{
    appendThermalObjectRow(QStringLiteral("heatsource"), 0.1, 201, QString());
    setStateChanged();
}

void MainWindow::removeSelectedThermalObjectRow()
{
    if (!m_tblThermalObjects)
        return;
    const int row = m_tblThermalObjects->currentRow();
    if (row >= 0)
        m_tblThermalObjects->removeRow(row);
    setStateChanged();
}

void MainWindow::removeAllThermalObjectRows()
{
    if (!m_tblThermalObjects)
        return;
    m_tblThermalObjects->setRowCount(0);
    setStateChanged();
}

QString MainWindow::buildThermalCodeFromGuiTable() const
{
    if (!m_tblThermalObjects || m_tblThermalObjects->rowCount() == 0)
        return QString();

    auto pyQuote = [](QString s) -> QString {
        s.replace('\\', "\\\\");
        s.replace('\'', "\\'");
        return "'" + s + "'";
    };

    QString code;
    code += "thermal_objects = simulation_setup.all_thermal_objects()\n";

    for (int row = 0; row < m_tblThermalObjects->rowCount(); ++row) {
        auto *typeBox = qobject_cast<QComboBox *>(m_tblThermalObjects->cellWidget(row, 0));
        auto *valItem = m_tblThermalObjects->item(row, 1);
        auto *srcItem = m_tblThermalObjects->item(row, 2);
        auto *tgtBox = qobject_cast<QComboBox *>(m_tblThermalObjects->cellWidget(row, 3));

        const QString type = typeBox ? typeBox->currentText().trimmed().toLower()
                                     : QStringLiteral("heatsource");
        const QString val = valItem ? valItem->text().trimmed() : QStringLiteral("0");
        const QString src = srcItem ? srcItem->text().trimmed() : QString();
        const QString tgt = tgtBox ? tgtBox->currentText().trimmed() : QString();

        if (src.isEmpty() || tgt.isEmpty())
            continue;

        if (type == QLatin1String("consttemp")) {
            code += QStringLiteral(
                        "thermal_objects.add_consttemp(simulation_setup.constanttemp("
                        "temp=%1, source_layernum=%2, target_layername=%3))\n")
                        .arg(val, src, pyQuote(tgt));
        } else {
            code += QStringLiteral(
                        "thermal_objects.add_heatsource(simulation_setup.heatsource("
                        "power=%1, source_layernum=%2, target_layername=%3))\n")
                        .arg(val, src, pyQuote(tgt));
        }
    }

    return code;
}

void MainWindow::ensureThermalTableInitializedFromScript(const QString &script)
{
    setupThermalObjectsUi();
    if (!m_tblThermalObjects || m_tblThermalObjects->rowCount() != 0)
        return;

    QRegularExpression reHeat(
        R"(add_heatsource\s*\(\s*simulation_setup\.heatsource\s*\(([^)]*)\))",
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpression reTemp(
        R"(add_consttemp\s*\(\s*simulation_setup\.constanttemp\s*\(([^)]*)\))",
        QRegularExpression::CaseInsensitiveOption);

    auto parseArgs = [](const QString &args, double *value, int *layer, QString *target) {
        QRegularExpression rePower(R"(power\s*=\s*([0-9eE+\-\.]+))");
        QRegularExpression reT(R"(temp\s*=\s*([0-9eE+\-\.]+))");
        QRegularExpression reLayer(R"(source_layernum\s*=\s*(\d+))");
        QRegularExpression reTarget(R"(target_layername\s*=\s*['\"]([^'\"]+)['\"])");
        const auto mp = rePower.match(args);
        const auto mt = reT.match(args);
        if (mp.hasMatch())
            *value = mp.captured(1).toDouble();
        else if (mt.hasMatch())
            *value = mt.captured(1).toDouble();
        const auto ml = reLayer.match(args);
        if (ml.hasMatch())
            *layer = ml.captured(1).toInt();
        const auto mtg = reTarget.match(args);
        if (mtg.hasMatch())
            *target = mtg.captured(1);
    };

    auto itHeat = reHeat.globalMatch(script);
    while (itHeat.hasNext()) {
        const auto m = itHeat.next();
        double value = 0.0;
        int layer = 0;
        QString target;
        parseArgs(m.captured(1), &value, &layer, &target);
        appendThermalObjectRow(QStringLiteral("heatsource"), value, layer, target);
    }

    auto itTemp = reTemp.globalMatch(script);
    while (itTemp.hasNext()) {
        const auto m = itTemp.next();
        double value = 298.0;
        int layer = 0;
        QString target;
        parseArgs(m.captured(1), &value, &layer, &target);
        appendThermalObjectRow(QStringLiteral("consttemp"), value, layer, target);
    }
}

QVector<QPair<int, int>> MainWindow::findThermalBlocks(const QString &script)
{
    auto isAddLine = [](const QString &t) -> bool {
        return t.startsWith(QStringLiteral("thermal_objects.add_"));
    };

    QVector<QPair<int, int>> blocks;
    QRegularExpression startRe(
        R"((?m)^[ \t]*thermal_objects\s*=\s*simulation_setup\.all_thermal_objects\(\)\s*(?:#.*)?\r?\n?)");

    int searchPos = 0;
    while (true) {
        QRegularExpressionMatch m = startRe.match(script, searchPos);
        if (!m.hasMatch())
            break;

        const int blockStart = m.capturedStart();
        int scan = m.capturedEnd();
        while (scan < script.size()) {
            int lineEnd = script.indexOf('\n', scan);
            if (lineEnd < 0)
                lineEnd = script.size();
            const QString t = script.mid(scan, lineEnd - scan).trimmed();
            if (t.isEmpty() || t.startsWith('#') || isAddLine(t)) {
                scan = (lineEnd < script.size()) ? (lineEnd + 1) : lineEnd;
                continue;
            }
            break;
        }
        blocks.push_back({blockStart, scan});
        searchPos = scan;
    }
    return blocks;
}

void MainWindow::replaceOrInsertThermalSection(QString &script, const QString &thermalCode)
{
    if (thermalCode.trimmed().isEmpty())
        return;

    auto blocks = findThermalBlocks(script);
    if (!blocks.isEmpty()) {
        // Remove later duplicates first
        for (int i = blocks.size() - 1; i >= 1; --i)
            script.remove(blocks[i].first, blocks[i].second - blocks[i].first);
        blocks = findThermalBlocks(script);
        script.replace(blocks.first().first,
                       blocks.first().second - blocks.first().first,
                       thermalCode.endsWith('\n') ? thermalCode : thermalCode + '\n');
        return;
    }

    QRegularExpression marker(R"((?m)^[ \t]*#?[ \t]*={3,}.*simulation.*={3,})");
    const auto m = marker.match(script);
    if (m.hasMatch()) {
        script.insert(m.capturedStart(), thermalCode.endsWith('\n') ? thermalCode + '\n'
                                                                     : thermalCode + "\n\n");
    } else {
        if (!script.endsWith('\n'))
            script += '\n';
        script += '\n' + thermalCode;
        if (!script.endsWith('\n'))
            script += '\n';
    }
}

void MainWindow::applyElmerThermalWorkflowToScript(QString &script)
{
    QRegularExpression reKey(R"(\w+\s*\[\s*['"]elmer_thermal['"]\s*\]\s*=\s*.*$)");
    if (reKey.match(script).hasMatch()) {
        script.replace(QRegularExpression(R"((\w+\s*\[\s*['"]elmer_thermal['"]\s*\]\s*=\s*).*$)",
                                          QRegularExpression::MultilineOption),
                       QStringLiteral("\\1True"));
    } else {
        QRegularExpression reCreate(
            R"(config_name,\s*data_dir\s*=\s*simulation_setup\.create_(?:palace|elmer|elmer_thermal)\s*\()");
        const QRegularExpressionMatch m = reCreate.match(script);
        if (m.hasMatch())
            script.insert(m.capturedStart(), QStringLiteral("settings['elmer_thermal'] = True\n"));
        else
            script.prepend(QStringLiteral("settings['elmer_thermal'] = True\n"));
    }

    script.replace(
        QRegularExpression(
            R"(config_name,\s*data_dir\s*=\s*simulation_setup\.create_palace\s*\([^\n]*)"),
        QStringLiteral("config_name, data_dir = simulation_setup.create_elmer_thermal (settings)"));
    script.replace(
        QRegularExpression(
            R"(config_name,\s*data_dir\s*=\s*simulation_setup\.create_elmer\s*\([^\n]*)"),
        QStringLiteral("config_name, data_dir = simulation_setup.create_elmer_thermal (settings)"));

    script.replace(
        QRegularExpression(
            R"(utilities\.create_sim_path\s*\(\s*script_path\s*,\s*model_basename\s*\))"),
        QStringLiteral("utilities.create_sim_path(script_path, model_basename, dirname='elmer_model')"));
}
