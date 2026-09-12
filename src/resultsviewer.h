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

#ifndef RESULTSVIEWER_H
#define RESULTSVIEWER_H

#include "touchstone.h"

#include <QHash>
#include <QSet>
#include <QWidget>

#include <utility>

class QCheckBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

/*!*******************************************************************************************************************
 * \class ResultsViewer
 * \brief Embeddable S-parameter results viewer (Touchstone .sNp overlay plots).
 *
 * Mirrors setupEM's Result Viewer: file tree under a target directory, dynamic
 * S-parameter button grid, and dB+Phase / Smith / zoomed-Smith display modes.
 **********************************************************************************************************************/
class ResultsViewer : public QWidget
{
    Q_OBJECT

public:
    explicit ResultsViewer(QWidget *parent = nullptr);

    void setTargetDirectory(const QString &dir);
    QString targetDirectory() const;
    void rescan();
    bool tryConvertPalaceCsv(QString *logOut = nullptr);
    bool hasTouchstoneFiles() const;

    void setPreferredPythonPreferenceKey(const QString &prefKey);
    QString preferredPythonPreferenceKey() const;

#ifdef EMSTUDIO_TESTING
    QString testResolveHostPython() const { return resolveHostPython(); }
    QStringList testHostPythonCandidates() const { return hostPythonCandidates(); }
#endif

public slots:
    void refresh();
    void convertPalaceCsv();
    void launchModelFit();
    void compareFile();
    void compareFolder();
    void clearCompare();

signals:
    void logMessage(const QString &text);

private slots:
    void onFileItemChanged(QTreeWidgetItem *item, int column);
    void onParamToggled(bool checked);
    void onModeChanged(bool checked);
    void onFilterChanged();

private:
    enum class DisplayMode { Db, Phase, Smith, Zoom };

    struct CachedNetwork {
        qint64 mtimeMs = -1;
        TouchstoneNetwork network;
        bool ok = false;
    };

    struct PlottedTrace {
        const TouchstoneNetwork *network = nullptr;
        QColor color;
        Qt::PenStyle style;
        QString label;
    };

    void buildUi();
    void rescanFiles();
    QStringList findTouchstoneFiles(const QString &targetDir) const;
    QStringList filteredFiles(const QStringList &files) const;
    QString relPathFor(const QString &path) const;
    QString legendLabelFor(const QString &path) const;
    bool isComparePath(const QString &path) const;
    void appendComparePaths(const QStringList &paths);
    void rebuildCompareTreeGroup();
    QTreeWidgetItem *makeFileItem(const QString &path);
    void refreshGroupCheckState(QTreeWidgetItem *groupItem);
    const TouchstoneNetwork *loadNetworkCached(const QString &path);
    QVector<PlottedTrace> getCheckedPlotted();
    int currentCommonNports();
    void rebuildParameterGrid(int n);
    void onControlChanged();
    void redrawPlot();
    void clearPlotArea();
    void showEmptyMessage(const QString &text);
    void drawDbPhase(const QVector<PlottedTrace> &plotted,
                     const QVector<std::pair<int, int>> &params,
                     bool showDb,
                     bool showPhase);
    void drawSmith(const QVector<PlottedTrace> &plotted,
                   const QVector<std::pair<int, int>> &reflectionParams,
                   bool zoomed);
    void setLegend(const QVector<PlottedTrace> &plotted);
    bool hasPalaceCsv() const;
    QString resolveCombineScript() const;
    QStringList hostPythonCandidates() const;
    QString resolveHostPython() const;
    QString resolvePythonWithSnp2le(QString *detailOut = nullptr) const;
    QStringList hostPythonArgs(const QString &python) const;
    QString pickModelFitFile() const;
    bool isRawTouchstoneName(const QString &fileName) const;
    bool snp2leImportOk(const QString &python, QString *detailOut = nullptr) const;

    QString m_targetDir;
    QString m_preferredPythonPrefKey;
    QStringList m_masterFiles;
    QStringList m_extraComparePaths; // absolute paths outside / in addition to target dir
    QSet<QString> m_checkedPaths;
    QSet<QPair<int, int>> m_checkedParams; // 1-based (m,k) like Volker
    QHash<QString, CachedNetwork> m_networkCache;
    int m_lastN = -1;
    DisplayMode m_mode = DisplayMode::Db;
    bool m_updatingChecks = false;

    QLineEdit *m_pathEdit = nullptr;
    QCheckBox *m_includeDcCb = nullptr;
    QCheckBox *m_includeDeembeddedCb = nullptr;
    QTreeWidget *m_fileList = nullptr;
    QGridLayout *m_paramGrid = nullptr;
    QRadioButton *m_dbRadio = nullptr;
    QRadioButton *m_phaseRadio = nullptr;
    QRadioButton *m_smithRadio = nullptr;
    QRadioButton *m_zoomRadio = nullptr;
    QLabel *m_warningLabel = nullptr;
    QLabel *m_legendLabel = nullptr;
    QPushButton *m_convertBtn = nullptr;
    QPushButton *m_modelFitBtn = nullptr;
    QPushButton *m_compareBtn = nullptr;
    QPushButton *m_clearCompareBtn = nullptr;
    QWidget *m_plotHost = nullptr;
    QVBoxLayout *m_plotHostLayout = nullptr;
};

#endif // RESULTSVIEWER_H
