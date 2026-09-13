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
#include "resultscalculator.h"
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
#include <QMenu>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QGestureEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPinchGesture>
#include <QWheelEvent>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolButton>
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
    // Zoom: rubber-band drag, wheel / trackpad pinch toward cursor; F resets.
    class ZoomableChartView : public QChartView
    {
    public:
        explicit ZoomableChartView(QChart *c, QWidget *parent = nullptr)
            : QChartView(c, parent)
        {
            setRubberBand(QChartView::RectangleRubberBand);
            setRenderHint(QPainter::Antialiasing);
            setMinimumHeight(160);
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            setFocusPolicy(Qt::ClickFocus);
            grabGesture(Qt::PinchGesture);
            setToolTip(QObject::tr("Drag to zoom · wheel / pinch toward cursor · F to reset"));
        }

    protected:
        void mousePressEvent(QMouseEvent *event) override
        {
            setFocus(Qt::MouseFocusReason);
            QChartView::mousePressEvent(event);
        }

        void keyPressEvent(QKeyEvent *event) override
        {
            if (event->key() == Qt::Key_F && chart()) {
                chart()->zoomReset();
                event->accept();
                return;
            }
            QChartView::keyPressEvent(event);
        }

        void wheelEvent(QWheelEvent *event) override
        {
            qreal dy = event->angleDelta().y();
            if (dy == 0.0)
                dy = event->pixelDelta().y();
            if (!chart() || dy == 0.0) {
                QChartView::wheelEvent(event);
                return;
            }
            const qreal factor = (dy > 0) ? 1.15 : (1.0 / 1.15);
            zoomToward(event->position().toPoint(), factor);
            event->accept();
        }

        bool viewportEvent(QEvent *event) override
        {
            if (event->type() == QEvent::Gesture) {
                auto *ge = static_cast<QGestureEvent *>(event);
                if (QPinchGesture *pinch = static_cast<QPinchGesture *>(ge->gesture(Qt::PinchGesture))) {
                    if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged) {
                        // centerPoint is in the receiving widget's coordinates.
                        zoomToward(pinch->centerPoint(), pinch->scaleFactor());
                        return true;
                    }
                }
            }
            if (event->type() == QEvent::NativeGesture) {
                auto *ne = static_cast<QNativeGestureEvent *>(event);
                if (ne->gestureType() == Qt::ZoomNativeGesture && chart()) {
                    // Trackpad pinch (Windows / macOS): value is a magnification delta.
                    const qreal factor = 1.0 + ne->value();
                    if (!qFuzzyIsNull(ne->value()) && factor > 0.0) {
                        zoomToward(ne->localPos(), factor);
                        return true;
                    }
                }
            }
            return QChartView::viewportEvent(event);
        }

    private:
        void zoomToward(const QPointF &viewPos, qreal factor)
        {
            if (!chart() || factor <= 0.0 || qFuzzyCompare(factor, 1.0))
                return;
            const QPointF chartPos = chart()->mapFromScene(mapToScene(viewPos.toPoint()));
            const QPointF valueUnderCursor = chart()->mapToValue(chartPos);
            chart()->zoom(factor);
            const QPointF newChartPos = chart()->mapToPosition(valueUnderCursor);
            const QPointF delta = newChartPos - chartPos;
            chart()->scroll(delta.x(), delta.y());
        }
    };

    return new ZoomableChartView(chart);
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
                      const QString &path,
                      bool selected,
                      bool singlePoint,
                      ResultsViewer *owner)
{
    auto *series = new QLineSeries();
    series->setName(name);
    series->setProperty("tracePath", path);
    QPen pen(color);
    pen.setStyle(style);
    pen.setWidthF(selected ? 3.4 : 1.8);
    series->setPen(pen);
    series->setPointsVisible(true);
    if (singlePoint || selected) {
        pen.setWidthF(selected ? 3.4 : 3.0);
        series->setPen(pen);
    }
    for (const QPointF &pt : pts)
        series->append(pt);
    chart->addSeries(series);
    if (owner && !path.isEmpty()) {
        QObject::connect(series, &QLineSeries::clicked, owner,
                         [owner, path](const QPointF &) { owner->onCalcTraceClicked(path); });
    }
}

} // namespace

ResultsViewer::ResultsViewer(QWidget *parent)
    : QWidget(parent)
{
    loadResultsSettings();
    buildUi();
}

void ResultsViewer::loadResultsSettings()
{
    QSettings settings(QStringLiteral("EMStudio"), QStringLiteral("EMStudioApp"));
    settings.beginGroup(QStringLiteral("Results"));

    m_preferredParams.clear();
    const QRegularExpression re(QStringLiteral("^S?(\\d+)[,xX]?(\\d+)$"),
                                QRegularExpression::CaseInsensitiveOption);
    const QStringList tokens = settings.value(QStringLiteral("sParameters")).toStringList();
    for (QString tok : tokens) {
        tok = tok.trimmed();
        if (tok.isEmpty())
            continue;
        const QRegularExpressionMatch match = re.match(tok);
        if (!match.hasMatch())
            continue;
        const int m = match.captured(1).toInt();
        const int k = match.captured(2).toInt();
        if (m >= 1 && k >= 1)
            m_preferredParams.insert(qMakePair(m, k));
    }
    if (m_preferredParams.isEmpty())
        m_preferredParams.insert(qMakePair(1, 1));

    m_calcVisiblePref = settings.value(QStringLiteral("calculatorVisible"), false).toBool();
    settings.endGroup();
}

void ResultsViewer::saveResultsSettings() const
{
    QSettings settings(QStringLiteral("EMStudio"), QStringLiteral("EMStudioApp"));
    settings.beginGroup(QStringLiteral("Results"));

    QStringList tokens;
    QList<QPair<int, int>> sorted = m_preferredParams.values();
    std::sort(sorted.begin(), sorted.end());
    for (const auto &pk : sorted)
        tokens << QStringLiteral("S%1%2").arg(pk.first).arg(pk.second);
    settings.setValue(QStringLiteral("sParameters"), tokens);

    const bool calcOn = m_calcToggleBtn && m_calcToggleBtn->isChecked();
    settings.setValue(QStringLiteral("calculatorVisible"), calcOn);
    settings.endGroup();
}

QSet<QPair<int, int>> ResultsViewer::preferredParamsForPorts(int n) const
{
    QSet<QPair<int, int>> out;
    if (n < 1)
        return out;
    for (const auto &pk : m_preferredParams) {
        if (pk.first >= 1 && pk.second >= 1 && pk.first <= n && pk.second <= n)
            out.insert(pk);
    }
    return out;
}

void ResultsViewer::setTargetDirectory(const QString &dir)
{
    const QString cleaned = dir.isEmpty() ? QString() : QDir::cleanPath(dir);
    const bool changed = (cleaned != m_targetDir);
    m_targetDir = cleaned;
    if (m_pathEdit && m_pathEdit->text() != m_targetDir)
        m_pathEdit->setText(m_targetDir);

    // Drop primary-file selection/cache when switching models/folders so old
    // Touchstone overlays don't linger. Compare overlays are kept intentionally.
    if (changed) {
        QSet<QString> keep;
        for (const QString &p : m_extraComparePaths) {
            if (m_checkedPaths.contains(p))
                keep.insert(p);
        }
        m_checkedPaths = keep;
        m_networkCache.clear();
        m_calcSelectedPaths.clear();
        m_lastN = -1;
    }
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
    m_convertBtn->setToolTip(
        tr("Manual fallback: run combine_extend_snp.py when only Palace CSV is present.\n"
           "Usually unnecessary after a normal EMStudio Run (Touchstone is created already)."));
    m_convertBtn->setToolTip(tr("Run combine_extend_snp.py on this folder "
                                "(Palace port-S.csv → .sNp). Needed before plotting."));
    connect(m_convertBtn, &QPushButton::clicked, this, &ResultsViewer::convertPalaceCsv);
    fileBtnRow->addWidget(m_convertBtn);
    m_modelFitBtn = new QPushButton(tr("Model Fit…"), filesGroup);
    m_modelFitBtn->setToolTip(tr("Open snp2le on the selected Touchstone file "
                                 "(lumped-element netlist extraction)."));
    connect(m_modelFitBtn, &QPushButton::clicked, this, &ResultsViewer::launchModelFit);
    fileBtnRow->addWidget(m_modelFitBtn);

    m_compareBtn = new QPushButton(tr("Compare…"), filesGroup);
    m_compareBtn->setToolTip(tr("Overlay Touchstone from another file or run folder on the same charts."));
    auto *compareMenu = new QMenu(m_compareBtn);
    compareMenu->addAction(tr("Touchstone file…"), this, &ResultsViewer::compareFile);
    compareMenu->addAction(tr("Run folder…"), this, &ResultsViewer::compareFolder);
    m_compareBtn->setMenu(compareMenu);
    fileBtnRow->addWidget(m_compareBtn);

    m_clearCompareBtn = new QPushButton(tr("Clear compare"), filesGroup);
    m_clearCompareBtn->setToolTip(tr("Remove all Compare overlays."));
    m_clearCompareBtn->setEnabled(false);
    connect(m_clearCompareBtn, &QPushButton::clicked, this, &ResultsViewer::clearCompare);
    fileBtnRow->addWidget(m_clearCompareBtn);

    fileBtnRow->addStretch();
    filesLayout->addLayout(fileBtnRow);

    // --- S-Parameters ---
    auto *paramGroup = new QGroupBox(tr("S-Parameters"), hSplit);
    m_paramGrid = new QGridLayout(paramGroup);

    // --- Display ---
    auto *displayGroup = new QGroupBox(tr("Display"), hSplit);
    auto *displayLayout = new QVBoxLayout(displayGroup);
    m_dbRadio = new QRadioButton(tr("dB"), displayGroup);
    m_phaseRadio = new QRadioButton(tr("Phase"), displayGroup);
    m_smithRadio = new QRadioButton(tr("Smith chart"), displayGroup);
    m_zoomRadio = new QRadioButton(tr("Smith chart (zoomed)"), displayGroup);
    m_dbRadio->setChecked(true);
    auto *modeGroup = new QButtonGroup(this);
    modeGroup->addButton(m_dbRadio);
    modeGroup->addButton(m_phaseRadio);
    modeGroup->addButton(m_smithRadio);
    modeGroup->addButton(m_zoomRadio);
    displayLayout->addWidget(m_dbRadio);
    displayLayout->addWidget(m_phaseRadio);
    displayLayout->addWidget(m_smithRadio);
    displayLayout->addWidget(m_zoomRadio);
    connect(m_dbRadio, &QRadioButton::toggled, this, &ResultsViewer::onModeChanged);
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
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setWidget(m_plotHost);
    scroll->setFrameShape(QFrame::NoFrame);

    m_plotCalcSplit = new QSplitter(Qt::Horizontal, vSplit);
    m_plotCalcSplit->setChildrenCollapsible(false);
    m_plotCalcSplit->addWidget(scroll);

    m_calcPanel = new ResultsCalculatorPanel(m_plotCalcSplit);
    m_calcPanel->setVisible(false);
    m_plotCalcSplit->addWidget(m_calcPanel);
    m_plotCalcSplit->setStretchFactor(0, 5);
    m_plotCalcSplit->setStretchFactor(1, 1);
    m_plotCalcSplit->setSizes({700, 260});

    vSplit->addWidget(hSplit);
    vSplit->addWidget(m_plotCalcSplit);
    vSplit->setStretchFactor(0, 0);
    vSplit->setStretchFactor(1, 1);
    vSplit->setChildrenCollapsible(false);
    vSplit->setSizes({220, 480});

    mainLayout->addWidget(vSplit, 1);

    auto *bottomRow = new QHBoxLayout();
    m_legendLabel = new QLabel(this);
    m_legendLabel->setWordWrap(true);
    bottomRow->addWidget(m_legendLabel, 1);

    m_calcToggleBtn = new QToolButton(this);
    m_calcToggleBtn->setIcon(resultsCalculatorIcon(22));
    m_calcToggleBtn->setIconSize(QSize(22, 22));
    m_calcToggleBtn->setCheckable(true);
    m_calcToggleBtn->setChecked(false);
    m_calcToggleBtn->setToolTip(tr("Show / hide RF calculator panel"));
    m_calcToggleBtn->setAutoRaise(true);
    connect(m_calcToggleBtn, &QToolButton::toggled, this, &ResultsViewer::toggleCalculator);
    bottomRow->addWidget(m_calcToggleBtn, 0, Qt::AlignRight | Qt::AlignVCenter);
    mainLayout->addLayout(bottomRow);

    if (m_calcVisiblePref)
        m_calcToggleBtn->setChecked(true);

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
    if (isComparePath(path)) {
        const QString name = QFileInfo(path).fileName();
        const QString label = QStringLiteral("cmp: %1").arg(name);
        if (label.size() <= 28)
            return label;
        return QStringLiteral("cmp: %1..%2").arg(name.left(8), name.right(12));
    }
    QString rel = relPathFor(path);
    if (rel.size() <= 17)
        return rel;
    return rel.left(10) + QLatin1String("..") + rel.right(20);
}

bool ResultsViewer::isComparePath(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    for (const QString &p : m_extraComparePaths) {
        if (QDir::cleanPath(p) == clean)
            return true;
    }
    return false;
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
        QSet<QString> keep;
        for (const QString &p : m_extraComparePaths) {
            if (m_checkedPaths.contains(p))
                keep.insert(p);
        }
        m_checkedPaths = keep;
    } else if (!QDir(m_targetDir).exists()) {
        m_masterFiles.clear();
        auto *item = new QTreeWidgetItem(QStringList{
            tr("Target Directory does not exist: %1").arg(m_targetDir)});
        item->setFlags(Qt::NoItemFlags);
        m_fileList->addTopLevelItem(item);
        QSet<QString> keep;
        for (const QString &p : m_extraComparePaths) {
            if (m_checkedPaths.contains(p))
                keep.insert(p);
        }
        m_checkedPaths = keep;
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
            // Keep Compare checks only; drop stale primary paths
            QSet<QString> keep;
            for (const QString &p : m_extraComparePaths) {
                if (m_checkedPaths.contains(p))
                    keep.insert(p);
            }
            m_checkedPaths = keep;
        } else {
            // Drop stale checks (keep Compare overlays)
            QSet<QString> still;
            for (const QString &p : m_masterFiles) {
                if (m_checkedPaths.contains(p))
                    still.insert(p);
            }
            for (const QString &p : m_extraComparePaths) {
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

    rebuildCompareTreeGroup();

    m_fileList->blockSignals(false);
    if (m_convertBtn)
        m_convertBtn->setEnabled(!m_targetDir.isEmpty() && QDir(m_targetDir).exists());
    if (m_modelFitBtn)
        m_modelFitBtn->setEnabled(!m_targetDir.isEmpty() && QDir(m_targetDir).exists());
    if (m_clearCompareBtn)
        m_clearCompareBtn->setEnabled(!m_extraComparePaths.isEmpty());
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

bool ResultsViewer::hasTouchstoneFiles() const
{
    if (m_targetDir.isEmpty() || !QDir(m_targetDir).exists())
        return false;
    return !findTouchstoneFiles(m_targetDir).isEmpty();
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

void ResultsViewer::setPreferredPythonPreferenceKey(const QString &prefKey)
{
    m_preferredPythonPrefKey = prefKey.trimmed();
}

QString ResultsViewer::preferredPythonPreferenceKey() const
{
    return m_preferredPythonPrefKey;
}

QStringList ResultsViewer::hostPythonCandidates() const
{
    QSettings settings(QStringLiteral("EMStudio"), QStringLiteral("EMStudioApp"));
    settings.beginGroup(QStringLiteral("Preferences"));
    const QString openemsPy = settings.value(QStringLiteral("OPENEMS_PYTHON")).toString().trimmed();
    const QString elmerPy = settings.value(QStringLiteral("ELMER_PYTHON")).toString().trimmed();
    const QString palacePy = settings.value(QStringLiteral("PALACE_PYTHON")).toString().trimmed();
    settings.endGroup();

#ifdef Q_OS_WIN
    // On Windows, ignore WSL-style absolute paths (Palace often points at /home/...).
    auto usable = [](const QString &p) {
        return !p.isEmpty() && QFileInfo::exists(p) && !p.startsWith(QLatin1Char('/'));
    };
#else
    auto usable = [](const QString &p) {
        return !p.isEmpty() && QFileInfo::exists(p);
    };
#endif

    auto appendUnique = [](QStringList &out, const QString &p) {
        if (p.isEmpty())
            return;
        if (!out.contains(p))
            out.append(p);
    };

    QStringList orderedKeys;
    if (!m_preferredPythonPrefKey.isEmpty())
        orderedKeys << m_preferredPythonPrefKey;
    // Fallbacks: keep tool-specific venvs before PATH lookup.
    orderedKeys << QStringLiteral("PALACE_PYTHON")
                << QStringLiteral("ELMER_PYTHON")
                << QStringLiteral("OPENEMS_PYTHON");

    QStringList out;
    for (const QString &key : orderedKeys) {
        QString path;
        if (key == QLatin1String("OPENEMS_PYTHON"))
            path = openemsPy;
        else if (key == QLatin1String("ELMER_PYTHON"))
            path = elmerPy;
        else if (key == QLatin1String("PALACE_PYTHON"))
            path = palacePy;
        if (usable(path))
            appendUnique(out, path);
    }

    const QString py3 = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (usable(py3))
        appendUnique(out, py3);

    const QString py = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (usable(py))
        appendUnique(out, py);

#ifdef Q_OS_WIN
    const QString pyLauncher = QStandardPaths::findExecutable(QStringLiteral("py"));
    if (usable(pyLauncher))
        appendUnique(out, pyLauncher);
#endif

    if (out.isEmpty()) {
#ifdef Q_OS_WIN
        out << QStringLiteral("python");
#else
        out << QStringLiteral("python3");
#endif
    }
    return out;
}

QString ResultsViewer::resolveHostPython() const
{
    const QStringList cands = hostPythonCandidates();
    return cands.isEmpty() ? QStringLiteral("python3") : cands.first();
}

QString ResultsViewer::resolvePythonWithSnp2le(QString *detailOut) const
{
    QString lastDetail;
    for (const QString &python : hostPythonCandidates()) {
        QString detail;
        if (snp2leImportOk(python, &detail)) {
            if (detailOut)
                *detailOut = detail;
            return python;
        }
        if (!detail.isEmpty())
            lastDetail = detail;
    }
    if (detailOut)
        *detailOut = lastDetail;
    return {};
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

bool ResultsViewer::snp2leImportOk(const QString &python, QString *detailOut) const
{
    if (python.isEmpty()) {
        if (detailOut)
            *detailOut = tr("No Python interpreter configured.");
        return false;
    }

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

    QString detail;
    const QString python = resolvePythonWithSnp2le(&detail);
    if (python.isEmpty()) {
        const QString hintPy = resolveHostPython();
        const QString installCmd = QStringLiteral("%1 -m pip install -U snp2le")
                                       .arg(QDir::toNativeSeparators(hintPy));
        const QString reqCmd = QStringLiteral("%1 -m pip install -U -r requirements-python.txt")
                                   .arg(QDir::toNativeSeparators(hintPy));
        QMessageBox::warning(
            this,
            tr("Model Fit — snp2le not installed"),
            tr("snp2le is not available for the configured Python interpreters "
               "(tried preferred tool venv first).\n"
               "Suggested install target:\n%1\n\n"
               "Install it in that same Python (native host, not WSL), then try again:\n\n"
               "  %2\n\n"
               "Or install the full EMStudio Python set from the repo root:\n\n"
               "  %3\n\n"
               "snp2le needs Python ≥ 3.10 (PySide6 GUI).\n"
               "Project: https://github.com/iic-jku/snp2le\n\n"
               "Check output:\n%4")
                .arg(QDir::toNativeSeparators(hintPy),
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

void ResultsViewer::appendComparePaths(const QStringList &paths)
{
    bool added = false;
    for (const QString &raw : paths) {
        const QString path = QDir::cleanPath(raw);
        if (path.isEmpty() || !QFileInfo::exists(path))
            continue;
        if (m_extraComparePaths.contains(path))
            continue;
        m_extraComparePaths.append(path);
        m_checkedPaths.insert(path);
        added = true;
    }
    if (!added)
        return;
    rescanFiles();
}

void ResultsViewer::rebuildCompareTreeGroup()
{
    if (m_extraComparePaths.isEmpty())
        return;

    auto *groupItem = new QTreeWidgetItem(QStringList{tr("Compare")});
    groupItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    groupItem->setCheckState(0, Qt::Unchecked);
    m_fileList->addTopLevelItem(groupItem);
    for (const QString &path : m_extraComparePaths)
        groupItem->addChild(makeFileItem(path));
    refreshGroupCheckState(groupItem);
    groupItem->setExpanded(true);
}

void ResultsViewer::compareFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Compare Touchstone file"),
        m_targetDir.isEmpty() ? QDir::homePath() : m_targetDir,
        tr("Touchstone (*.s*p *.S*P);;All files (*)"));
    if (path.isEmpty())
        return;
    if (!kTouchstoneRe.match(QFileInfo(path).fileName()).hasMatch()) {
        QMessageBox::warning(this, tr("Compare"),
                             tr("Selected file does not look like a Touchstone .sNp name."));
        return;
    }
    appendComparePaths({path});
    emit logMessage(tr("\n[Compare] Added file: %1\n").arg(QDir::toNativeSeparators(path)));
}

void ResultsViewer::compareFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Compare run folder"),
        m_targetDir.isEmpty() ? QDir::homePath() : m_targetDir);
    if (dir.isEmpty())
        return;

    const QStringList found = filteredFiles(findTouchstoneFiles(dir));
    if (found.isEmpty()) {
        QMessageBox::warning(this, tr("Compare"),
                             tr("No Touchstone (.sNp) files found under:\n%1").arg(dir));
        return;
    }
    appendComparePaths(found);
    emit logMessage(tr("\n[Compare] Added %1 file(s) from %2\n")
                        .arg(found.size())
                        .arg(QDir::toNativeSeparators(dir)));
}

void ResultsViewer::clearCompare()
{
    if (m_extraComparePaths.isEmpty())
        return;
    for (const QString &p : m_extraComparePaths)
        m_checkedPaths.remove(p);
    m_extraComparePaths.clear();
    rescanFiles();
    emit logMessage(tr("\n[Compare] Cleared overlay files.\n"));
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
    for (const QString &path : m_extraComparePaths) {
        if (m_checkedPaths.contains(path) && !checkedInOrder.contains(path))
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
        t.path = path;
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

    // Apply last preferred S-params that exist for this port count; else default S11.
    // Do not rewrite preferences when falling back — user may reopen a larger network later.
    m_checkedParams = preferredParamsForPorts(n);
    if (n >= 1 && m_checkedParams.isEmpty())
        m_checkedParams.insert(qMakePair(1, 1));

    if (n == 0) {
        m_paramGrid->addWidget(new QLabel(tr("Check a file to choose S-parameters")), 0, 0);
    } else {
        for (int m = 1; m <= n; ++m) {
            for (int k = 1; k <= n; ++k) {
                auto *btn = new QPushButton(QStringLiteral("S%1%2").arg(m).arg(k));
                btn->setCheckable(true);
                btn->setFixedWidth(50);
                btn->setProperty("s_m", m);
                btn->setProperty("s_k", k);
                connect(btn, &QPushButton::toggled, this, &ResultsViewer::onParamToggled);
                btn->blockSignals(true);
                btn->setChecked(m_checkedParams.contains(qMakePair(m, k)));
                btn->blockSignals(false);
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
    // Only persist an explicit non-empty choice. Fallback S11 when a preferred
    // param is missing from the current run must not overwrite that preference.
    if (!m_checkedParams.isEmpty()) {
        m_preferredParams = m_checkedParams;
        saveResultsSettings();
    }
    redrawPlot();
}

void ResultsViewer::onModeChanged(bool checked)
{
    if (!checked)
        return;
    if (m_dbRadio->isChecked())
        m_mode = DisplayMode::Db;
    else if (m_phaseRadio->isChecked())
        m_mode = DisplayMode::Phase;
    else if (m_smithRadio->isChecked())
        m_mode = DisplayMode::Smith;
    else if (m_zoomRadio->isChecked())
        m_mode = DisplayMode::Zoom;
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
        const bool sel = m_calcSelectedPaths.contains(t.path);
        const QString mark = sel ? QStringLiteral("●") : QStringLiteral("■");
        parts.append(QStringLiteral("<span style='color:%1;'>%2</span> %3%4")
                         .arg(t.color.name(),
                              mark,
                              sel ? QStringLiteral("<b>") : QString(),
                              t.label.toHtmlEscaped())
                     + (sel ? QStringLiteral("</b>") : QString()));
    }
    if (m_calcPanel && m_calcPanel->isVisible()) {
        parts.prepend(tr("<i>Click a curve for Calculator · drag/wheel/pinch to zoom · F reset</i>&nbsp;&nbsp;"));
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
        drawDbPhase(plotted, params,
                    m_mode == DisplayMode::Db,
                    m_mode == DisplayMode::Phase);
    }
    setLegend(plotted);
    syncCalculatorTraces();
}

void ResultsViewer::toggleCalculator(bool on)
{
    if (!m_calcPanel)
        return;
    m_calcPanel->setVisible(on);
    if (on) {
        syncCalculatorTraces();
        if (m_plotCalcSplit) {
            QList<int> sizes = m_plotCalcSplit->sizes();
            if (sizes.size() >= 2 && sizes.at(1) < 120) {
                const int total = sizes.at(0) + sizes.at(1);
                m_plotCalcSplit->setSizes({qMax(200, total - 260), 260});
            }
        }
    }
    saveResultsSettings();
}

void ResultsViewer::onCalcTraceClicked(const QString &path)
{
    if (path.isEmpty())
        return;
    const int idx = m_calcSelectedPaths.indexOf(path);
    if (idx >= 0)
        m_calcSelectedPaths.removeAt(idx);
    else
        m_calcSelectedPaths.append(path);

    if (m_calcToggleBtn && !m_calcToggleBtn->isChecked())
        m_calcToggleBtn->setChecked(true);

    redrawPlot();
}

void ResultsViewer::syncCalculatorTraces()
{
    if (!m_calcPanel || !m_calcPanel->isVisible())
        return;

    const QVector<PlottedTrace> plotted = getCheckedPlotted();

    // Keep only still-plotted selections
    {
        QSet<QString> alive;
        for (const PlottedTrace &t : plotted)
            alive.insert(t.path);
        QStringList kept;
        for (const QString &p : m_calcSelectedPaths) {
            if (alive.contains(p))
                kept.append(p);
        }
        m_calcSelectedPaths = kept;
    }

    QVector<ResultsCalculatorPanel::TraceRef> refs;
    // Prefer click-selected order; if none selected yet, use all plotted (with hint in UI)
    QStringList order = m_calcSelectedPaths;
    if (order.isEmpty()) {
        for (const PlottedTrace &t : plotted)
            order.append(t.path);
    }
    for (const QString &path : order) {
        for (const PlottedTrace &t : plotted) {
            if (t.path != path)
                continue;
            ResultsCalculatorPanel::TraceRef r;
            r.label = t.label;
            r.path = t.path;
            r.network = t.network;
            r.selected = m_calcSelectedPaths.contains(t.path);
            refs.append(r);
            break;
        }
    }
    m_calcPanel->setTraces(refs, !m_calcSelectedPaths.isEmpty());
}

void ResultsViewer::drawDbPhase(const QVector<PlottedTrace> &plotted,
                                const QVector<std::pair<int, int>> &params,
                                bool showDb,
                                bool showPhase)
{
    clearPlotArea();
    if (!showDb && !showPhase)
        return;

    auto *row = new QWidget(m_plotHost);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    for (const auto &pk : params) {
        const int m = pk.first;
        const int n = pk.second;

        auto *col = new QWidget(row);
        auto *colLayout = new QVBoxLayout(col);
        colLayout->setContentsMargins(2, 2, 2, 2);

        QChart *dbChart = nullptr;
        QChart *phChart = nullptr;
        if (showDb) {
            dbChart = new QChart();
            dbChart->legend()->hide();
            dbChart->setTitle(QStringLiteral("dB S%1%2").arg(m).arg(n));
        }
        if (showPhase) {
            phChart = new QChart();
            phChart->legend()->hide();
            phChart->setTitle(QStringLiteral("phase S%1%2").arg(m).arg(n));
        }

        for (const PlottedTrace &t : plotted) {
            const auto svals = t.network->sParam(m - 1, n - 1);
            const auto freqs = t.network->frequencyHz();
            QVector<QPointF> dbPts;
            QVector<QPointF> phPts;
            if (showDb)
                dbPts.reserve(freqs.size());
            if (showPhase)
                phPts.reserve(freqs.size());
            for (int i = 0; i < freqs.size(); ++i) {
                const double ghz = freqs.at(i) / 1e9;
                if (showDb)
                    dbPts.append(QPointF(ghz, toDb(svals.at(i))));
                if (showPhase)
                    phPts.append(QPointF(ghz, toPhaseDeg(svals.at(i))));
            }
            const bool single = freqs.size() == 1;
            const bool selected = m_calcSelectedPaths.contains(t.path);
            if (dbChart)
                addSeriesToChart(dbChart, dbPts, t.color, t.style, t.label, t.path,
                                 selected, single, this);
            if (phChart)
                addSeriesToChart(phChart, phPts, t.color, t.style, t.label, t.path,
                                 selected, single, this);
        }

        if (dbChart) {
            dbChart->createDefaultAxes();
            if (auto *axX = qobject_cast<QValueAxis *>(dbChart->axes(Qt::Horizontal).value(0)))
                configureFreqAxis(axX);
            if (auto *axY = qobject_cast<QValueAxis *>(dbChart->axes(Qt::Vertical).value(0)))
                axY->setTitleText(QStringLiteral("dB"));
            colLayout->addWidget(makeChartView(dbChart), 1);
        }
        if (phChart) {
            phChart->createDefaultAxes();
            if (auto *axX = qobject_cast<QValueAxis *>(phChart->axes(Qt::Horizontal).value(0)))
                configureFreqAxis(axX);
            if (auto *axY = qobject_cast<QValueAxis *>(phChart->axes(Qt::Vertical).value(0))) {
                axY->setTitleText(QStringLiteral("°"));
                axY->setRange(-180.0, 180.0);
                axY->setTickCount(5); // -180,-90,0,90,180 — readable in narrow panes
                axY->setLabelFormat(QStringLiteral("%.0f"));
            }
            colLayout->addWidget(makeChartView(phChart), 1);
        }
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
