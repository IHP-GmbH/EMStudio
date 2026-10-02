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

// File → Convert to settings dictionary: turns a loose-variable openEMS model into the
// settings[] workflow with scripts/convert_loose_to_settings.py (run with the OpenEMS Python).
// The model is backed up first and only replaced after the backup is verified.

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>

#include "mainwindow.h"
#include "sidebysidediff.h"
#include "ui_mainwindow.h"

namespace {

QString converterScriptPath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("scripts/convert_loose_to_settings.py"));
}

QString signaturesPath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("keywords/workflow_signatures.csv"));
}

QByteArray readAll(const QString &path, bool *ok)
{
    QFile f(path);
    *ok = f.open(QIODevice::ReadOnly);
    return *ok ? f.readAll() : QByteArray();
}

} // namespace

/*!*******************************************************************************************************************
 * \brief Whether the current model can be converted to the settings[] workflow.
 *
 * Needs an openEMS model saved to a file, without a settings dict, that calls the workflow.
 *
 * \param why Receives the reason when it can't (optional).
 * \return True when File → Convert to settings dictionary is possible.
 **********************************************************************************************************************/
bool MainWindow::canConvertLooseModel(QString *why) const
{
    auto fail = [why](const QString &msg) {
        if (why)
            *why = msg;
        return false;
    };
    if (currentSimToolKey().compare(QLatin1String("openems"), Qt::CaseInsensitive) != 0)
        return fail(tr("Only openEMS models use loose variables."));
    const QString model = m_ui->txtRunPythonScript->text().trimmed();
    if (model.isEmpty() || !QFileInfo(model).isFile())
        return fail(tr("Save the model to a file first."));
    const QString script = m_ui->editRunPythonScript->toPlainText();
    if (!settingsDictName(script).isEmpty())
        return fail(tr("The model already uses a settings dictionary."));
    if (!script.contains(QLatin1String("setupSimulation")))
        return fail(tr("The model doesn't call setupSimulation()."));
    if (m_looseConversionRunning)
        return fail(tr("A conversion is already running."));
    return true;
}

/*!*******************************************************************************************************************
 * \brief Enables File → Convert to settings dictionary for the current model, with the reason as tooltip.
 **********************************************************************************************************************/
void MainWindow::updateConvertLooseModelAction()
{
    QString why;
    const bool ok = canConvertLooseModel(&why);
    m_ui->actionConvertToSettings->setEnabled(ok);
    m_ui->actionConvertToSettings->setToolTip(ok ? tr("Convert loose variables to settings[...] and "
                                                      "call the workflow with settings=settings.")
                                                 : why);
}

/*!*******************************************************************************************************************
 * \brief File → Convert to settings dictionary.
 **********************************************************************************************************************/
void MainWindow::on_actionConvertToSettings_triggered()
{
    startLooseConversion();
}

/*!*******************************************************************************************************************
 * \brief Starts the conversion: checks, unsaved changes, then the converter's report.
 **********************************************************************************************************************/
void MainWindow::startLooseConversion()
{
    QString why;
    if (!canConvertLooseModel(&why)) {
        error(tr("Convert to settings dictionary: %1").arg(why), false);
        emit looseConversionFinished(false, why);
        return;
    }
    const QString python = m_preferences.value(QStringLiteral("Python Path")).toString().trimmed();
    if (python.isEmpty()) {
        const QString msg = tr("Set the OpenEMS Python (Setup → Preferences → Python Path) first: the "
                               "conversion runs with it and checks the gds2openEMS it has installed.");
        error(msg, false);
        emit looseConversionFinished(false, msg);
        return;
    }
    if (!QFileInfo(converterScriptPath()).isFile()) {
        const QString msg = tr("Converter not found: %1").arg(converterScriptPath());
        error(msg, false);
        emit looseConversionFinished(false, msg);
        return;
    }

    // The converter reads the file: the editor (with the grid synced into it) must match it.
    const QString model = m_ui->txtRunPythonScript->text().trimmed();
    syncGuiSettingsToPythonEditor();
    bool ok = false;
    const QByteArray onDisk = readAll(model, &ok);
    if (!ok || QString::fromUtf8(onDisk) != m_ui->editRunPythonScript->toPlainText()) {
#ifdef EMSTUDIO_TESTING
        const bool save = true;
#else
        const bool save = QMessageBox::question(
                              this, tr("Convert to settings dictionary"),
                              tr("The model has unsaved changes. The conversion works on the saved "
                                 "file.\n\nSave the model now?"),
                              QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save)
                          == QMessageBox::Save;
#endif
        if (!save) {
            emit looseConversionFinished(false, tr("Cancelled."));
            return;
        }
        on_actionSave_triggered();
        if (!canConvertLooseModel(&why)) {
            error(tr("Convert to settings dictionary: %1").arg(why), false);
            emit looseConversionFinished(false, why);
            return;
        }
    }

    m_looseConversionRunning = true;
    info(tr("Checking the model for conversion to settings[...] ..."), false);
    runLooseConverter({QStringLiteral("--report"), model},
                      [this, model](const QJsonObject &report, const QString &err) {
        if (!err.isEmpty() || !report.value(QStringLiteral("ok")).toBool()) {
            m_looseConversionRunning = false;
            const QString reason = err.isEmpty() ? report.value(QStringLiteral("reason")).toString() : err;
            error(tr("The model can't be converted: %1").arg(reason), false);
#ifndef EMSTUDIO_TESTING
            QMessageBox::information(this, tr("Convert to settings dictionary"),
                                     tr("The model can't be converted. Nothing was changed.\n\n%1")
                                         .arg(reason));
#endif
            emit looseConversionFinished(false, reason);
            return;
        }
        bool switchImports = false;
        if (!showLooseConversionDialog(report, &switchImports)) {
            m_looseConversionRunning = false;
            info(tr("Conversion cancelled; nothing was changed."), false);
            emit looseConversionFinished(false, tr("Cancelled."));
            return;
        }
        finishLooseConversion(model, switchImports);
    });
}

/*!*******************************************************************************************************************
 * \brief Runs the converter script asynchronously and hands its JSON report to \a done.
 *
 * \param args Converter arguments (--report / --write ...).
 * \param done Called with the report, or with an error text when the script failed.
 **********************************************************************************************************************/
void MainWindow::runLooseConverter(const QStringList &args,
                                   std::function<void(const QJsonObject &, const QString &)> done)
{
    const QString python = m_preferences.value(QStringLiteral("Python Path")).toString().trimmed();
    QStringList fullArgs{converterScriptPath()};
    fullArgs << args << QStringLiteral("--signatures") << signaturesPath();
    if (!m_testConverterPackageDir.isEmpty())
        fullArgs << QStringLiteral("--package-dir") << m_testConverterPackageDir;

    auto *proc = new QProcess(this);
    m_looseConverterProcess = proc;
    proc->setProgram(python);
    proc->setArguments(fullArgs);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [proc, done](int code, QProcess::ExitStatus status) {
        const QByteArray out = proc->readAllStandardOutput();
        const QString err = QString::fromLocal8Bit(proc->readAllStandardError()).trimmed();
        proc->deleteLater();
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(out, &pe);
        if (status != QProcess::NormalExit || code != 0 || pe.error != QJsonParseError::NoError
            || !doc.isObject()) {
            done(QJsonObject(), err.isEmpty() ? tr("the converter failed (exit code %1)").arg(code) : err);
            return;
        }
        done(doc.object(), QString());
    });
    connect(proc, &QProcess::errorOccurred, this, [proc, done, python](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        proc->deleteLater();
        done(QJsonObject(), tr("can't start %1").arg(python));
    });
    proc->start();
}

/*!*******************************************************************************************************************
 * \brief Shows what the conversion changes and lets the user confirm it.
 *
 * \param report        Converter report (converted / loose variables, import switch, diff).
 * \param switchImports Receives whether the user allowed switching the imports to gds2openEMS.
 * \return True when the user chose Convert.
 **********************************************************************************************************************/
bool MainWindow::showLooseConversionDialog(const QJsonObject &report, bool *switchImports)
{
    const bool needsSwitch = report.value(QStringLiteral("needs_import_switch")).toBool();
#ifdef EMSTUDIO_TESTING
    *switchImports = needsSwitch && m_testConvertSwitchImports;
    m_testLastConversionReport = report;
    return m_testConvertAccept && (!needsSwitch || *switchImports);
#else
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Convert to settings dictionary"));
    dlg.resize(1400, 900);
    auto *layout = new QVBoxLayout(&dlg);

    auto *intro = new QLabel(&dlg);
    intro->setWordWrap(true);
    intro->setText(tr("Each variable below becomes <b>settings['key']</b> because of how the model uses it "
                      "(the <i>Proven by</i> column); nothing is guessed from names. setupSimulation() and "
                      "runSimulation() are called with <b>settings=settings</b>, like current gds2openEMS "
                      "models. The conversion was verified to give exactly the intended code.<br><br>"
                      "A backup copy of the model is saved next to it first. The converted model "
                      "re-simulates once, because the workflow detects that the script changed."));
    layout->addWidget(intro);

    auto *split = new QSplitter(Qt::Vertical, &dlg);
    auto *table = new QTableWidget(split);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({tr("Variable"), tr("Becomes"), tr("Line"), tr("Proven by / reason")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    auto addRow = [table](const QString &name, const QString &becomes, int line, const QString &why) {
        const int r = table->rowCount();
        table->insertRow(r);
        table->setItem(r, 0, new QTableWidgetItem(name));
        table->setItem(r, 1, new QTableWidgetItem(becomes));
        table->setItem(r, 2, new QTableWidgetItem(QString::number(line)));
        auto *item = new QTableWidgetItem(why);
        item->setToolTip(why);
        table->setItem(r, 3, item);
    };
    for (const QJsonValue &v : report.value(QStringLiteral("converted")).toArray()) {
        const QJsonObject o = v.toObject();
        addRow(o.value(QStringLiteral("name")).toString(),
               QStringLiteral("settings['%1']").arg(o.value(QStringLiteral("key")).toString()),
               o.value(QStringLiteral("line")).toInt(), o.value(QStringLiteral("proof")).toString());
    }
    for (const QJsonValue &v : report.value(QStringLiteral("loose")).toArray()) {
        const QJsonObject o = v.toObject();
        addRow(o.value(QStringLiteral("name")).toString(), tr("stays a variable"),
               o.value(QStringLiteral("line")).toInt(), o.value(QStringLiteral("reason")).toString());
    }
    auto toLines = [](const QJsonValue &v) {
        QStringList out;
        for (const QJsonValue &line : v.toArray())
            out << line.toString();
        return out;
    };
    auto *diff = new SideBySideDiff(split);
    const QString name = QFileInfo(report.value(QStringLiteral("model")).toString()).fileName();
    diff->setContent(toLines(report.value(QStringLiteral("original_lines"))),
                     toLines(report.value(QStringLiteral("converted_lines"))),
                     SideBySideDiff::rowsFromJson(report.value(QStringLiteral("rows")).toArray()),
                     tr("Original: %1").arg(name), tr("Converted"));
    split->addWidget(table);
    split->addWidget(diff);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    layout->addWidget(split, 1);
    // Previous / Next select the variables assigned in the shown change; a table row jumps to its line.
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(diff, &SideBySideDiff::changeShown, table, [table](int, const QVector<int> &lines) {
        table->clearSelection();
        bool first = true;
        for (int r = 0; r < table->rowCount(); ++r) {
            const QTableWidgetItem *lineItem = table->item(r, 2);
            if (!lineItem || !lines.contains(lineItem->text().toInt()))
                continue;
            table->selectionModel()->select(table->model()->index(r, 0),
                                            QItemSelectionModel::Select | QItemSelectionModel::Rows);
            if (first)
                table->scrollToItem(table->item(r, 0));
            first = false;
        }
    });
    connect(table, &QTableWidget::cellClicked, diff, [table, diff](int row, int) {
        if (const QTableWidgetItem *lineItem = table->item(row, 2))
            diff->goToLeftLine(lineItem->text().toInt());
    });
    if (diff->changeCount() > 0)
        QTimer::singleShot(0, diff, [diff]() { diff->goToChange(0); });

    QCheckBox *switchBox = nullptr;
    if (needsSwitch) {
        const QJsonObject wf = report.value(QStringLiteral("workflow")).toObject();
        switchBox = new QCheckBox(&dlg);
        switchBox->setText(tr("Also switch the imports to the installed gds2openEMS (%1)")
                               .arg(wf.value(QStringLiteral("package_dir")).toString()));
        auto *note = new QLabel(&dlg);
        note->setWordWrap(true);
        note->setText(tr("<b>Needed:</b> %1. With the switch the model runs with a newer workflow version, "
                         "so results may differ from the old model (e.g. via merging, port and mesh "
                         "changes). Compare the results after the next run.")
                          .arg(wf.value(QStringLiteral("switch_reason")).toString().toHtmlEscaped()));
        layout->addWidget(note);
        layout->addWidget(switchBox);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dlg);
    QPushButton *convert = buttons->addButton(tr("Convert"), QDialogButtonBox::AcceptRole);
    convert->setEnabled(!needsSwitch);
    if (switchBox)
        connect(switchBox, &QCheckBox::toggled, convert, &QPushButton::setEnabled);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return false;
    *switchImports = switchBox && switchBox->isChecked();
    return true;
#endif
}

/*!*******************************************************************************************************************
 * \brief Writes the converted model: converter → temp file, verified backup, replace, reload.
 *
 * \param model         Model file.
 * \param switchImports Whether the user allowed switching the imports to gds2openEMS.
 **********************************************************************************************************************/
void MainWindow::finishLooseConversion(const QString &model, bool switchImports)
{
    auto tmp = std::make_shared<QTemporaryDir>();
    if (!tmp->isValid()) {
        m_looseConversionRunning = false;
        error(tr("Conversion failed: no temporary folder."), false);
        emit looseConversionFinished(false, tr("no temporary folder"));
        return;
    }
    const QString out = tmp->filePath(QFileInfo(model).fileName());
    QStringList args{QStringLiteral("--write"), model, QStringLiteral("--out"), out};
    if (switchImports)
        args << QStringLiteral("--switch-imports");

    runLooseConverter(args, [this, tmp, model, out](const QJsonObject &report, const QString &err) {
        m_looseConversionRunning = false;
        auto fail = [this](const QString &msg) {
            error(tr("Conversion failed, the model was not changed: %1").arg(msg), false);
            emit looseConversionFinished(false, msg);
        };
        if (!err.isEmpty())
            return fail(err);
        if (!report.value(QStringLiteral("ok")).toBool())
            return fail(report.value(QStringLiteral("reason")).toString());

        bool ok = false;
        const QByteArray converted = readAll(out, &ok);
        if (!ok || converted.isEmpty())
            return fail(tr("the converter wrote no file"));
        const QByteArray original = readAll(model, &ok);
        if (!ok)
            return fail(tr("can't read %1").arg(model));

        // Backup next to the model; it must be a byte-exact copy before the model is touched.
        const QFileInfo fi(model);
        QString backup;
        for (int i = 0; backup.isEmpty() || QFileInfo::exists(backup); ++i) {
            backup = fi.dir().filePath(QStringLiteral("%1_backup_%2%3.py")
                                           .arg(fi.completeBaseName(),
                                                QDateTime::currentDateTime().toString(
                                                    QStringLiteral("yyyyMMdd_HHmmss")),
                                                i ? QStringLiteral("_%1").arg(i) : QString()));
        }
        if (!QFile::copy(model, backup))
            return fail(tr("can't write the backup %1").arg(backup));
        const QByteArray backupData = readAll(backup, &ok);
        if (!ok || backupData != original)
            return fail(tr("the backup %1 doesn't match the model").arg(backup));

        QSaveFile save(model);
        if (!save.open(QIODevice::WriteOnly) || save.write(converted) != converted.size() || !save.commit())
            return fail(tr("can't write %1 (the backup is %2)").arg(model, backup));

        loadPythonModel(model);
        info(tr("Converted to settings[...]: %1 variables. Backup of the original: %2. "
                "The model re-simulates once on the next run (the workflow detects the changed script).")
                 .arg(report.value(QStringLiteral("converted")).toArray().size())
                 .arg(QDir::toNativeSeparators(backup)), false);
        m_lastConversionBackup = backup;
        emit looseConversionFinished(true, backup);
    });
}
