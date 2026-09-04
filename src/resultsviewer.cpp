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

#include "resultsviewer.h"
#include "smithchartwidget.h"

#include <QtCharts>

#include <QButtonGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <QtMath>
#include <algorithm>
#include <cmath>

QT_CHARTS_USE_NAMESPACE

namespace {

const QVector<QColor> kColors = {
    QColor(220, 20, 60),   // red
    QColor(30, 90, 200),   // blue
    QColor(200, 0, 200),   // magenta
    QColor(0, 180, 180),   // cyan
    QColor(0, 150, 40),    // green
    QColor(200, 180, 0),   // yellow
    QColor(20, 20, 20),    // black
};

const QVector<Qt::PenStyle> kStyles = {
    Qt::SolidLine,
    Qt::DashLine,
    Qt::DashDotLine,
    Qt::DotLine,
};

const QRegularExpression kTouchstoneRe(
    QStringLiteral(R"(\.s(\d+)p$)"),
    QRegularExpression::CaseInsensitiveOption);

double toDb(const std::complex<double> &v)
{
    const double mag = std::abs(v);
    if (mag <= 0.0)
        return -200.0;
    return 20.0 * std::log10(mag);
}

double toPhaseDeg(const std::complex<double> &v)
{
    return qRadiansToDegrees(std::arg(v));
}

QChartView *makeChartView(QChart *chart)
{
    auto *view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->setMinimumHeight(160);
    view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return view;
}

void configureFreqAxis(QValueAxis *axis)
{
    axis->setTitleText(QStringLiteral("Frequency (GHz)"));
    axis->setLabelFormat(QStringLiteral("%.3g"));
}

void addSeriesToChart(QChart *chart,
                      const QVector<QPointF> &pts,
                      const QColor &color,
                      Qt::PenStyle style,
                      const QString &name,
                      bool singlePoint)
{
    auto *series = new QLineSeries();
    series->setName(name);
    QPen pen(color);
    pen.setStyle(style);
    pen.setWidthF(1.8);
    series->setPen(pen);
    if (singlePoint) {
        series->setPointsVisible(true);
        pen.setWidthF(3.0);
        series->setPen(pen);
    }
    for (const QPointF &pt : pts)
        series->append(pt);
    chart->addSeries(series);
}

} // namespace

ResultsViewer::ResultsViewer(QWidget *parent)
    : QWidget(parent)
{
    m_checkedParams.insert(qMakePair(1, 1));
    buildUi();
}

void ResultsViewer::setTargetDirectory(const QString &dir)
{
    m_targetDir = QDir::cleanPath(dir);
    if (m_pathEdit && m_pathEdit->text() != m_targetDir)
        m_pathEdit->setText(m_targetDir);
    rescanFiles();
}

QString ResultsViewer::targetDirectory() const
{
    return m_targetDir;
}

void ResultsViewer::rescan()
{
    rescanFiles();
}

void ResultsViewer::refresh()
{
    rescanFiles();
}

void ResultsViewer::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    auto *pathRow = new QHBoxLayout();
    pathRow->addWidget(new QLabel(tr("Results folder:"), this));
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText(tr("Folder containing Touchstone .sNp (or Palace port-S.csv)"));
    pathRow->addWidget(m_pathEdit, 1);
    auto *browseBtn = new QPushButton(tr("Browse…"), this);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString start = m_targetDir.isEmpty() ? QDir::homePath() : m_targetDir;
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Select results folder"), start);
        if (!dir.isEmpty())
            setTargetDirectory(dir);
    });
    pathRow->addWidget(browseBtn);
    connect(m_pathEdit, &QLineEdit::editingFinished, this, [this]() {
        const QString dir = m_pathEdit->text().trimmed();
        if (dir != m_targetDir)
            setTargetDirectory(dir);
    });
    mainLayout->addLayout(pathRow);

    auto *vSplit = new QSplitter(Qt::Vertical, this);

    auto *hSplit = new QSplitter(Qt::Horizontal, vSplit);

    // --- Files ---
    auto *filesGroup = new QGroupBox(tr("Files"), hSplit);
    auto *filesLayout = new QVBoxLayout(filesGroup);
    auto *filterRow = new QHBoxLayout();
    m_includeDcCb = new QCheckBox(tr("Include _dc files"), filesGroup);
    m_includeDeembeddedCb = new QCheckBox(tr("Include _deembedded files"), filesGroup);
    connect(m_includeDcCb, &QCheckBox::toggled, this, &ResultsViewer::onFilterChanged);
    connect(m_includeDeembeddedCb, &QCheckBox::toggled, this, &ResultsViewer::onFilterChanged);
    filterRow->addWidget(m_includeDcCb);
    filterRow->addWidget(m_includeDeembeddedCb);
    filterRow->addStretch();
    filesLayout->addLayout(filterRow);

    m_fileList = new QTreeWidget(filesGroup);
    m_fileList->setHeaderHidden(true);
    m_fileList->setMinimumHeight(80);
    connect(m_fileList, &QTreeWidget::itemChanged, this, &ResultsViewer::onFileItemChanged);
    filesLayout->addWidget(m_fileList, 1);

    auto *fileBtnRow = new QHBoxLayout();
    auto *refreshBtn = new QPushButton(tr("Refresh"), filesGroup);
    connect(refreshBtn, &QPushButton::clicked, this, &ResultsViewer::refresh);
    fileBtnRow->addWidget(refreshBtn);
    m_convertBtn = new QPushButton(tr("CSV → Touchstone"), filesGroup);
    m_convertBtn->setToolTip(tr("Run combine_extend_snp.py on this folder "
                                "(Palace port-S.csv → .sNp). Needed before plotting."));
    connect(m_convertBtn, &QPushButton::clicked, this, &ResultsViewer::convertPalaceCsv);
    fileBtnRow->addWidget(m_convertBtn);
    m_modelFitBtn = new QPushButton(tr("Model Fit…"), filesGroup);
    m_modelFitBtn->setToolTip(tr("Open snp2le on the selected Touchstone file "
                                 "(lumped-element netlist extraction)."));
    connect(m_modelFitBtn, &QPushButton::clicked, this, &ResultsViewer::launchModelFit);
    fileBtnRow->addWidget(m_modelFitBtn);
    fileBtnRow->addStretch();
    filesLayout->addLayout(fileBtnRow);

    // --- S-Parameters ---
    auto *paramGroup = new QGroupBox(tr("S-Parameters"), hSplit);
    m_paramGrid = new QGridLayout(paramGroup);

    // --- Display ---
    auto *displayGroup = new QGroupBox(tr("Display"), hSplit);
    auto *displayLayout = new QVBoxLayout(displayGroup);
    m_phaseRadio = new QRadioButton(tr("dB + Phase"), displayGroup);
    m_smithRadio = new QRadioButton(tr("Smith chart"), displayGroup);
    m_zoomRadio = new QRadioButton(tr("Smith chart (zoomed)"), displayGroup);
    m_phaseRadio->setChecked(true);
    auto *modeGroup = new QButtonGroup(this);
    modeGroup->addButton(m_phaseRadio);
    modeGroup->addButton(m_smithRadio);
    modeGroup->addButton(m_zoomRadio);
    displayLayout->addWidget(m_phaseRadio);
    displayLayout->addWidget(m_smithRadio);
    displayLayout->addWidget(m_zoomRadio);
    connect(m_phaseRadio, &QRadioButton::toggled, this, &ResultsViewer::onModeChanged);
    connect(m_smithRadio, &QRadioButton::toggled, this, &ResultsViewer::onModeChanged);
    connect(m_zoomRadio, &QRadioButton::toggled, this, &ResultsViewer::onModeChanged);

    m_warningLabel = new QLabel(displayGroup);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setStyleSheet(QStringLiteral("color: #b00000;"));
    displayLayout->addWidget(m_warningLabel);
    displayLayout->addStretch();

    hSplit->addWidget(filesGroup);
    hSplit->addWidget(paramGroup);
    hSplit->addWidget(displayGroup);
    hSplit->setStretchFactor(0, 3);
    hSplit->setStretchFactor(1, 2);
    hSplit->setStretchFactor(2, 2);
    hSplit->setChildrenCollapsible(false);

    m_plotHost = new QWidget(this);
    m_plotHostLayout = new QVBoxLayout(m_plotHost);
    m_plotHostLayout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(vSplit);
    scroll->setWidgetResizable(true);
    scroll->setWidget(m_plotHost);
    scroll->setFrameShape(QFrame::NoFrame);

    vSplit->addWidget(hSplit);
    vSplit->addWidget(scroll);
    vSplit->setStretchFactor(0, 0);
    vSplit->setStretchFactor(1, 1);
    vSplit->setChildrenCollapsible(false);
    vSplit->setSizes({220, 480});

    mainLayout->addWidget(vSplit, 1);

    m_legendLabel = new QLabel(this);
    m_legendLabel->setWordWrap(true);
    mainLayout->addWidget(m_legendLabel);

    showEmptyMessage(tr("Check a file and at least one S-parameter to plot"));
}

QStringList ResultsViewer::findTouchstoneFiles(const QString &targetDir) const
{
    QStringList matches;
    if (targetDir.isEmpty() || !QDir(targetDir).exists())
        return matches;

    QDirIterator it(targetDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi(path);
        // Skip hidden path segments (Volker filters dirnames starting with '.')
        const QString rel = QDir::fromNativeSeparators(QDir(targetDir).relativeFilePath(path));
        bool skip = false;
        for (const QString &part : rel.split(QLatin1Char('/'))) {
            if (part.startsWith(QLatin1Char('.'))) {
                skip = true;
                break;
            }
        }
        if (skip)
            continue;
        if (kTouchstoneRe.match(fi.fileName()).hasMatch())
            matches.append(QDir::cleanPath(path));
    }
    matches.sort(Qt::CaseInsensitive);
    return matches;
}

QStringList ResultsViewer::filteredFiles(const QStringList &files) const
{
    const bool includeDc = m_includeDcCb && m_includeDcCb->isChecked();
    const bool includeDeemb = m_includeDeembeddedCb && m_includeDeembeddedCb->isChecked();
    QStringList out;
    for (const QString &path : files) {
        const QString name = QFileInfo(path).fileName();
        if (!includeDc && name.contains(QLatin1String("_dc")))
            continue;
        if (!includeDeemb && name.contains(QLatin1String("_deembedded")))
            continue;
        out.append(path);
    }
    return out;
}

QString ResultsViewer::relPathFor(const QString &path) const
{
    if (m_targetDir.isEmpty())
        return path;
    return QDir::fromNativeSeparators(QDir(m_targetDir).relativeFilePath(path));
}

QString ResultsViewer::legendLabelFor(const QString &path) const
{
    QString rel = relPathFor(path);
    if (rel.size() <= 17)
        return rel;
    return rel.left(10) + QLatin1String("..") + rel.right(20);
}

void ResultsViewer::onFilterChanged()
{
    rescanFiles();
}

void ResultsViewer::rescanFiles()
{
    m_fileList->blockSignals(true);
    m_fileList->clear();

    if (m_targetDir.isEmpty()) {
        m_masterFiles.clear();
        auto *item = new QTreeWidgetItem(QStringList{
            tr("No Target Directory set.")});
        item->setFlags(Qt::NoItemFlags);
        m_fileList->addTopLevelItem(item);
    } else if (!QDir(m_targetDir).exists()) {
        m_masterFiles.clear();
        auto *item = new QTreeWidgetItem(QStringList{
            tr("Target Directory does not exist: %1").arg(m_targetDir)});
        item->setFlags(Qt::NoItemFlags);
        m_fileList->addTopLevelItem(item);
    } else {
        const QStringList allFiles = findTouchstoneFiles(m_targetDir);
        m_masterFiles = filteredFiles(allFiles);
        if (m_masterFiles.isEmpty()) {
            QString message;
            if (!allFiles.isEmpty()) {
                message = tr("No files match the current _dc/_deembedded filters under %1")
                              .arg(m_targetDir);
            } else if (hasPalaceCsv()) {
                message = tr("Found Palace/Elmer CSV under %1, but no Touchstone (.sNp) yet.\n"
                             "Click “CSV → Touchstone” (runs combine_extend_snp.py), then Refresh.")
                              .arg(m_targetDir);
            } else {
                message = tr("No Touchstone (.sNp) files found under %1").arg(m_targetDir);
            }
            auto *item = new QTreeWidgetItem(QStringList{message});
            item->setFlags(Qt::NoItemFlags);
            m_fileList->addTopLevelItem(item);
        } else {
            // Drop stale checks
            QSet<QString> still;
            for (const QString &p : m_masterFiles) {
                if (m_checkedPaths.contains(p))
                    still.insert(p);
            }
            m_checkedPaths = still;

            // Auto-check newest when nothing is selected (first open / filters cleared all)
            if (m_checkedPaths.isEmpty()) {
                QString newest = m_masterFiles.first();
                qint64 newestMtime = QFileInfo(newest).lastModified().toMSecsSinceEpoch();
                for (const QString &p : m_masterFiles) {
                    const qint64 mt = QFileInfo(p).lastModified().toMSecsSinceEpoch();
                    if (mt >= newestMtime) {
                        newestMtime = mt;
                        newest = p;
                    }
                }
                m_checkedPaths.insert(newest);
            }

            QMap<QString, QStringList> groups;
            QStringList rootFiles;
            for (const QString &path : m_masterFiles) {
                const QString parent = QFileInfo(relPathFor(path)).path();
                if (parent.isEmpty() || parent == QLatin1String("."))
                    rootFiles.append(path);
                else
                    groups[parent].append(path);
            }

            for (const QString &path : rootFiles)
                m_fileList->addTopLevelItem(makeFileItem(path));

            for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
                auto *groupItem = new QTreeWidgetItem(QStringList{it.key()});
                groupItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                groupItem->setCheckState(0, Qt::Unchecked);
                m_fileList->addTopLevelItem(groupItem);
                for (const QString &path : it.value())
                    groupItem->addChild(makeFileItem(path));
                refreshGroupCheckState(groupItem);
            }
            m_fileList->expandAll();
        }
    }

    m_fileList->blockSignals(false);
    if (m_convertBtn)
        m_convertBtn->setEnabled(!m_targetDir.isEmpty() && QDir(m_targetDir).exists());
    if (m_modelFitBtn)
        m_modelFitBtn->setEnabled(!m_targetDir.isEmpty() && QDir(m_targetDir).exists());
    onControlChanged();
}

bool ResultsViewer::hasPalaceCsv() const
{
    if (m_targetDir.isEmpty() || !QDir(m_targetDir).exists())
        return false;

    QDirIterator it(m_targetDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString name = QFileInfo(path).fileName();
        if (name.compare(QLatin1String("port-S.csv"), Qt::CaseInsensitive) == 0)
            return true;
        if (name.compare(QLatin1String("scalar_results.names"), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString ResultsViewer::resolveCombineScript() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString nearExe = QDir(appDir).filePath(QStringLiteral("scripts/combine_extend_snp.py"));
    if (QFileInfo::exists(nearExe))
        return nearExe;

    QDir d(appDir);
    for (int i = 0; i < 6; ++i) {
        const QString p = d.filePath(QStringLiteral("scripts/combine_extend_snp.py"));
        if (QFileInfo::exists(p))
            return p;
        if (!d.cdUp())
            break;
    }
    return nearExe;
}

QString ResultsViewer::resolveHostPython() const
{
    QSettings settings(QStringLiteral("EMStudio"), QStringLiteral("EMStudioApp"));
    settings.beginGroup(QStringLiteral("Preferences"));
    const QString openemsPy = settings.value(QStringLiteral("OPENEMS_PYTHON")).toString().trimmed();
    const QString elmerPy = settings.value(QStringLiteral("ELMER_PYTHON")).toString().trimmed();
    settings.endGroup();

    auto usableWin = [](const QString &p) {
        return !p.isEmpty() && QFileInfo::exists(p) && !p.startsWith(QLatin1Char('/'));
    };

    // Prefer a native Windows interpreter with scikit-rf (not WSL PALACE_PYTHON).
    if (usableWin(openemsPy))
        return openemsPy;
    if (usableWin(elmerPy))
        return elmerPy;

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (usableWin(fromPath))
        return fromPath;

    const QString pyLauncher = QStandardPaths::findExecutable(QStringLiteral("py"));
    if (usableWin(pyLauncher))
        return pyLauncher;

    return QStringLiteral("python");
}

QStringList ResultsViewer::hostPythonArgs(const QString &python) const
{
    QStringList args;
    const QString base = QFileInfo(python).fileName();
    if (base.compare(QLatin1String("py"), Qt::CaseInsensitive) == 0
        || base.compare(QLatin1String("py.exe"), Qt::CaseInsensitive) == 0) {
        args << QStringLiteral("-3");
    }
    return args;
}

bool ResultsViewer::isRawTouchstoneName(const QString &fileName) const
{
    return !fileName.contains(QLatin1String("_dc"))
        && !fileName.contains(QLatin1String("_deembedded"));
}

QString ResultsViewer::pickModelFitFile() const
{
    auto newestOf = [](const QStringList &paths) -> QString {
        if (paths.isEmpty())
            return {};
        QString best = paths.first();
        qint64 bestMt = QFileInfo(best).lastModified().toMSecsSinceEpoch();
        for (const QString &p : paths) {
            const qint64 mt = QFileInfo(p).lastModified().toMSecsSinceEpoch();
            if (mt >= bestMt) {
                bestMt = mt;
                best = p;
            }
        }
        return best;
    };

    QStringList checkedRaw;
    for (const QString &path : m_checkedPaths) {
        if (isRawTouchstoneName(QFileInfo(path).fileName()))
            checkedRaw.append(path);
    }
    if (!checkedRaw.isEmpty())
        return newestOf(checkedRaw);

    QStringList allRaw;
    for (const QString &path : m_masterFiles) {
        if (isRawTouchstoneName(QFileInfo(path).fileName()))
            allRaw.append(path);
    }
    if (!allRaw.isEmpty())
        return newestOf(allRaw);

    // Last resort: any checked / listed Touchstone (including _dc / _deembedded)
    if (!m_checkedPaths.isEmpty())
        return newestOf(m_checkedPaths.values());
    return newestOf(m_masterFiles);
}

bool ResultsViewer::snp2leImportOk(QString *detailOut) const
{
    const QString python = resolveHostPython();
    QStringList args = hostPythonArgs(python);
    args << QStringLiteral("-c")
         << QStringLiteral("import snp2le; print(getattr(snp2le, '__version__', 'ok'))");

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(python, args);
    if (!proc.waitForStarted(8000)) {
        if (detailOut)
            *detailOut = tr("Failed to start Python:\n%1").arg(python);
        return false;
    }
    if (!proc.waitForFinished(20000)) {
        proc.kill();
        if (detailOut)
            *detailOut = tr("Timed out checking for snp2le.");
        return false;
    }

    const QString output = QString::fromUtf8(proc.readAll()).trimmed();
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (detailOut)
            *detailOut = output;
        return false;
    }
    if (detailOut)
        *detailOut = output;
    return true;
}

void ResultsViewer::launchModelFit()
{
    const QString snpPath = pickModelFitFile();
    if (snpPath.isEmpty() || !QFileInfo::exists(snpPath)) {
        QMessageBox::warning(this, tr("Model Fit"),
                             tr("No Touchstone (.sNp) file available.\n"
                                "Run a simulation (and CSV → Touchstone if needed), "
                                "then check a file in the list."));
        return;
    }

    const QString python = resolveHostPython();
    QString detail;
    if (!snp2leImportOk(&detail)) {
        const QString installCmd = QStringLiteral("%1 -m pip install -U snp2le")
                                       .arg(QDir::toNativeSeparators(python));
        const QString reqCmd = QStringLiteral("%1 -m pip install -U -r requirements-python.txt")
                                   .arg(QDir::toNativeSeparators(python));
        QMessageBox::warning(
            this,
            tr("Model Fit — snp2le not installed"),
            tr("snp2le is not available for:\n%1\n\n"
               "Install it in that same Python (native host, not WSL), then try again:\n\n"
               "  %2\n\n"
               "Or install the full EMStudio Python set from the repo root:\n\n"
               "  %3\n\n"
               "snp2le needs Python ≥ 3.10 (PySide6 GUI).\n"
               "Project: https://github.com/iic-jku/snp2le\n\n"
               "Check output:\n%4")
                .arg(QDir::toNativeSeparators(python),
                     installCmd,
                     reqCmd,
                     detail.isEmpty() ? tr("(no details)") : detail));
        return;
    }

    QStringList args = hostPythonArgs(python);
    args << QStringLiteral("-m") << QStringLiteral("snp2le") << snpPath;

    qint64 pid = 0;
    if (!QProcess::startDetached(python, args, QFileInfo(snpPath).absolutePath(), &pid)) {
        QMessageBox::critical(this, tr("Model Fit"),
                              tr("Failed to launch:\n%1 -m snp2le\n%2")
                                  .arg(python, snpPath));
        return;
    }

    emit logMessage(tr("\n[Model Fit] Started snp2le (pid %1) on:\n%2\n"
                       "[Model Fit] Using Python: %3\n")
                        .arg(pid)
                        .arg(QDir::toNativeSeparators(snpPath),
                             QDir::toNativeSeparators(python)));
}

void ResultsViewer::convertPalaceCsv()
{
    QString log;
    if (!tryConvertPalaceCsv(&log)) {
        if (m_targetDir.isEmpty() || !QDir(m_targetDir).exists()) {
            QMessageBox::warning(this, tr("CSV → Touchstone"),
                                 tr("Set a valid Results folder first."));
            return;
        }
        emit logMessage(tr("\n[CSV → Touchstone failed]\n%1\n")
                            .arg(log.isEmpty() ? tr("(no details)") : log));
        QMessageBox::critical(this, tr("CSV → Touchstone"),
                              tr("Conversion failed. See Simulation Log for details."));
        return;
    }

    rescanFiles();
    emit logMessage(tr("\n[CSV → Touchstone] Touchstone files created.\n%1\n")
                        .arg(log.isEmpty() ? tr("(ok)") : log));
}

bool ResultsViewer::tryConvertPalaceCsv(QString *logOut)
{
    auto setLog = [&](const QString &msg) {
        if (logOut)
            *logOut = msg;
    };

    if (m_targetDir.isEmpty() || !QDir(m_targetDir).exists()) {
        setLog(tr("Results folder is missing or invalid."));
        return false;
    }

    const QString script = resolveCombineScript();
    if (!QFileInfo::exists(script)) {
        setLog(tr("combine_extend_snp.py not found:\n%1").arg(script));
        return false;
    }

    if (!hasPalaceCsv()) {
        setLog(tr("No port-S.csv / scalar_results.names under %1").arg(m_targetDir));
        return false;
    }

    // Already have Touchstone — still re-run so deembedded/DC stay in sync.
    QString python = resolveHostPython();
    QStringList args;
    if (QFileInfo(python).fileName().compare(QStringLiteral("py"), Qt::CaseInsensitive) == 0
        || QFileInfo(python).fileName().compare(QStringLiteral("py.exe"), Qt::CaseInsensitive) == 0) {
        args << QStringLiteral("-3");
    }
    args << script;

    QProcess proc;
    proc.setWorkingDirectory(m_targetDir);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(python, args);
    if (!proc.waitForStarted(8000)) {
        setLog(tr("Failed to start Python: %1").arg(python));
        return false;
    }
    if (!proc.waitForFinished(180000)) {
        proc.kill();
        setLog(tr("Conversion timed out."));
        return false;
    }

    const QString output = QString::fromUtf8(proc.readAll()).trimmed();
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        setLog(tr("combine_extend_snp.py failed (exit %1).\n%2")
                   .arg(proc.exitCode())
                   .arg(output.isEmpty() ? tr("(no output)") : output));
        return false;
    }

    setLog(output);
    return true;
}

QTreeWidgetItem *ResultsViewer::makeFileItem(const QString &path)
{
    auto *item = new QTreeWidgetItem(QStringList{QFileInfo(path).fileName()});
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setData(0, Qt::UserRole, path);
    item->setCheckState(0, m_checkedPaths.contains(path) ? Qt::Checked : Qt::Unchecked);
    return item;
}

void ResultsViewer::onFileItemChanged(QTreeWidgetItem *item, int /*column*/)
{
    if (m_updatingChecks || !item)
        return;

    const QVariant pathData = item->data(0, Qt::UserRole);
    const bool isGroup = !pathData.isValid() || pathData.toString().isEmpty();

    m_updatingChecks = true;
    if (isGroup) {
        const Qt::CheckState state = item->checkState(0);
        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem *child = item->child(i);
            child->setCheckState(0, state);
            const QString childPath = child->data(0, Qt::UserRole).toString();
            if (state == Qt::Checked)
                m_checkedPaths.insert(childPath);
            else
                m_checkedPaths.remove(childPath);
        }
    } else {
        const QString path = pathData.toString();
        if (item->checkState(0) == Qt::Checked)
            m_checkedPaths.insert(path);
        else
            m_checkedPaths.remove(path);
        if (item->parent())
            refreshGroupCheckState(item->parent());
    }
    m_updatingChecks = false;
    onControlChanged();
}

void ResultsViewer::refreshGroupCheckState(QTreeWidgetItem *groupItem)
{
    if (!groupItem || groupItem->childCount() == 0)
        return;
    int checked = 0;
    int unchecked = 0;
    for (int i = 0; i < groupItem->childCount(); ++i) {
        const Qt::CheckState s = groupItem->child(i)->checkState(0);
        if (s == Qt::Checked)
            ++checked;
        else
            ++unchecked;
    }
    if (unchecked == 0)
        groupItem->setCheckState(0, Qt::Checked);
    else if (checked == 0)
        groupItem->setCheckState(0, Qt::Unchecked);
    else
        groupItem->setCheckState(0, Qt::PartiallyChecked);
}

const TouchstoneNetwork *ResultsViewer::loadNetworkCached(const QString &path)
{
    QFileInfo fi(path);
    if (!fi.exists())
        return nullptr;

    const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
    auto it = m_networkCache.find(path);
    if (it != m_networkCache.end() && it->mtimeMs == mtime)
        return it->ok ? &it->network : nullptr;

    CachedNetwork entry;
    entry.mtimeMs = mtime;
    QString err;
    entry.ok = entry.network.load(path, &err);
    m_networkCache.insert(path, entry);
    it = m_networkCache.find(path);
    return it->ok ? &it->network : nullptr;
}

QVector<ResultsViewer::PlottedTrace> ResultsViewer::getCheckedPlotted()
{
    QStringList checkedInOrder;
    for (const QString &path : m_masterFiles) {
        if (m_checkedPaths.contains(path))
            checkedInOrder.append(path);
    }

    // Warm the cache first so pointers taken below stay valid (no rehash mid-loop).
    for (const QString &path : checkedInOrder)
        loadNetworkCached(path);

    QVector<PlottedTrace> out;
    QStringList warnings;
    int colorIdx = 0;
    for (const QString &path : checkedInOrder) {
        const QString label = legendLabelFor(path);
        const auto it = m_networkCache.constFind(path);
        if (it == m_networkCache.cend() || !it->ok) {
            warnings.append(label);
            continue;
        }
        PlottedTrace t;
        t.network = &it->network;
        t.color = kColors.at(colorIdx % kColors.size());
        t.style = kStyles.at(colorIdx % kStyles.size());
        t.label = label;
        out.append(t);
        ++colorIdx;
    }
    m_warningLabel->setText(warnings.isEmpty()
                                ? QString()
                                : tr("Failed to load: %1").arg(warnings.join(QLatin1String(", "))));
    return out;
}

int ResultsViewer::currentCommonNports()
{
    const auto plotted = getCheckedPlotted();
    if (plotted.isEmpty())
        return 0;
    int n = plotted.first().network->nports();
    for (const PlottedTrace &t : plotted)
        n = qMin(n, t.network->nports());
    return n;
}

void ResultsViewer::rebuildParameterGrid(int n)
{
    while (QLayoutItem *child = m_paramGrid->takeAt(0)) {
        if (QWidget *w = child->widget())
            w->deleteLater();
        delete child;
    }

    QSet<QPair<int, int>> kept;
    for (const auto &pk : m_checkedParams) {
        if (pk.first <= n && pk.second <= n)
            kept.insert(pk);
    }
    m_checkedParams = kept;
    if (n >= 1 && m_checkedParams.isEmpty())
        m_checkedParams.insert(qMakePair(1, 1));

    if (n == 0) {
        m_paramGrid->addWidget(new QLabel(tr("Check a file to choose S-parameters")), 0, 0);
    } else {
        for (int m = 1; m <= n; ++m) {
            for (int k = 1; k <= n; ++k) {
                auto *btn = new QPushButton(QStringLiteral("S%1%2").arg(m).arg(k));
                btn->setCheckable(true);
                btn->setChecked(m_checkedParams.contains(qMakePair(m, k)));
                btn->setFixedWidth(50);
                btn->setProperty("s_m", m);
                btn->setProperty("s_k", k);
                connect(btn, &QPushButton::toggled, this, &ResultsViewer::onParamToggled);
                m_paramGrid->addWidget(btn, m - 1, k - 1);
            }
        }
    }
    redrawPlot();
}

void ResultsViewer::onParamToggled(bool checked)
{
    auto *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;
    const int m = btn->property("s_m").toInt();
    const int k = btn->property("s_k").toInt();
    const auto key = qMakePair(m, k);
    if (checked)
        m_checkedParams.insert(key);
    else
        m_checkedParams.remove(key);
    redrawPlot();
}

void ResultsViewer::onModeChanged(bool checked)
{
    if (!checked)
        return;
    if (m_smithRadio->isChecked())
        m_mode = DisplayMode::Smith;
    else if (m_zoomRadio->isChecked())
        m_mode = DisplayMode::Zoom;
    else
        m_mode = DisplayMode::Phase;
    redrawPlot();
}

void ResultsViewer::onControlChanged()
{
    const int n = currentCommonNports();
    if (n != m_lastN) {
        m_lastN = n;
        rebuildParameterGrid(n);
    } else {
        redrawPlot();
    }
}

void ResultsViewer::clearPlotArea()
{
    while (QLayoutItem *child = m_plotHostLayout->takeAt(0)) {
        if (QWidget *w = child->widget())
            w->deleteLater();
        delete child;
    }
}

void ResultsViewer::showEmptyMessage(const QString &text)
{
    clearPlotArea();
    auto *lbl = new QLabel(text, m_plotHost);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setMinimumHeight(200);
    m_plotHostLayout->addWidget(lbl);
    m_legendLabel->clear();
}

void ResultsViewer::setLegend(const QVector<PlottedTrace> &plotted)
{
    if (plotted.isEmpty()) {
        m_legendLabel->clear();
        return;
    }
    QStringList parts;
    for (const PlottedTrace &t : plotted) {
        parts.append(QStringLiteral("<span style='color:%1;'>■</span> %2")
                         .arg(t.color.name(), t.label.toHtmlEscaped()));
    }
    m_legendLabel->setText(parts.join(QLatin1String("&nbsp;&nbsp;&nbsp;")));
}

void ResultsViewer::redrawPlot()
{
    const QVector<PlottedTrace> plotted = getCheckedPlotted();
    QVector<std::pair<int, int>> params;
    {
        QList<QPair<int, int>> sorted = m_checkedParams.values();
        std::sort(sorted.begin(), sorted.end());
        for (const auto &pk : sorted)
            params.append({pk.first, pk.second});
    }

    if (plotted.isEmpty() || params.isEmpty()) {
        showEmptyMessage(tr("Check a file and at least one S-parameter to plot"));
        return;
    }

    if (m_mode == DisplayMode::Smith || m_mode == DisplayMode::Zoom) {
        QVector<std::pair<int, int>> reflection;
        QVector<std::pair<int, int>> excluded;
        for (const auto &pk : params) {
            if (pk.first == pk.second)
                reflection.append(pk);
            else
                excluded.append(pk);
        }
        if (!excluded.isEmpty()) {
            QStringList names;
            for (const auto &pk : excluded)
                names.append(QStringLiteral("S%1%2").arg(pk.first).arg(pk.second));
            const QString note = tr("Not shown in Smith view (not reflection): %1")
                                     .arg(names.join(QLatin1String(", ")));
            const QString current = m_warningLabel->text();
            m_warningLabel->setText(current.isEmpty() ? note : (current + QLatin1String("   ") + note));
        }
        if (reflection.isEmpty()) {
            showEmptyMessage(tr("No reflection (Snn) parameter selected for Smith view"));
            return;
        }
        drawSmith(plotted, reflection, m_mode == DisplayMode::Zoom);
    } else {
        drawDbPhase(plotted, params);
    }
    setLegend(plotted);
}

void ResultsViewer::drawDbPhase(const QVector<PlottedTrace> &plotted,
                                const QVector<std::pair<int, int>> &params)
{
    clearPlotArea();

    auto *row = new QWidget(m_plotHost);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    for (const auto &pk : params) {
        const int m = pk.first;
        const int n = pk.second;

        auto *col = new QWidget(row);
        auto *colLayout = new QVBoxLayout(col);
        colLayout->setContentsMargins(2, 2, 2, 2);

        auto *dbChart = new QChart();
        dbChart->legend()->hide();
        dbChart->setTitle(QStringLiteral("dB S%1%2").arg(m).arg(n));
        auto *phChart = new QChart();
        phChart->legend()->hide();
        phChart->setTitle(QStringLiteral("phase S%1%2").arg(m).arg(n));

        for (const PlottedTrace &t : plotted) {
            const auto svals = t.network->sParam(m - 1, n - 1);
            const auto freqs = t.network->frequencyHz();
            QVector<QPointF> dbPts;
            QVector<QPointF> phPts;
            dbPts.reserve(freqs.size());
            phPts.reserve(freqs.size());
            for (int i = 0; i < freqs.size(); ++i) {
                const double ghz = freqs.at(i) / 1e9;
                dbPts.append(QPointF(ghz, toDb(svals.at(i))));
                phPts.append(QPointF(ghz, toPhaseDeg(svals.at(i))));
            }
            const bool single = freqs.size() == 1;
            addSeriesToChart(dbChart, dbPts, t.color, t.style, t.label, single);
            addSeriesToChart(phChart, phPts, t.color, t.style, t.label, single);
        }

        dbChart->createDefaultAxes();
        phChart->createDefaultAxes();
        if (auto *axX = qobject_cast<QValueAxis *>(dbChart->axes(Qt::Horizontal).value(0)))
            configureFreqAxis(axX);
        if (auto *axX = qobject_cast<QValueAxis *>(phChart->axes(Qt::Horizontal).value(0)))
            configureFreqAxis(axX);
        if (auto *axY = qobject_cast<QValueAxis *>(dbChart->axes(Qt::Vertical).value(0)))
            axY->setTitleText(QStringLiteral("dB"));
        if (auto *axY = qobject_cast<QValueAxis *>(phChart->axes(Qt::Vertical).value(0))) {
            axY->setTitleText(QStringLiteral("phase"));
            axY->setRange(-180.0, 180.0);
            axY->setTickCount(9); // -180..180 every 45
        }

        colLayout->addWidget(makeChartView(dbChart), 1);
        colLayout->addWidget(makeChartView(phChart), 1);
        rowLayout->addWidget(col, 1);
    }

    m_plotHostLayout->addWidget(row, 1);
}

void ResultsViewer::drawSmith(const QVector<PlottedTrace> &plotted,
                              const QVector<std::pair<int, int>> &reflectionParams,
                              bool zoomed)
{
    clearPlotArea();

    auto *row = new QWidget(m_plotHost);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    for (const auto &pk : reflectionParams) {
        auto *smith = new SmithChartWidget(row);
        smith->setZoomed(zoomed);
        smith->setChartTitle(QStringLiteral("S%1%2").arg(pk.first).arg(pk.second));
        for (const PlottedTrace &t : plotted) {
            smith->addTrace(t.network->sParam(pk.first - 1, pk.second - 1),
                            t.color, t.style, t.label);
        }
        rowLayout->addWidget(smith, 1);
    }

    m_plotHostLayout->addWidget(row, 1);
}
