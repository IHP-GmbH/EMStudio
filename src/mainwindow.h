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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMap>
#include <QSet>
#include <QPair>
#include <QVariant>
#include <QMainWindow>

#include "pythonparser.h"

class QProcess;
class QLineEdit;
class QComboBox;
class QtProperty;
class QListWidgetItem;
class QtVariantProperty;
class QtTreePropertyBrowser;
class QtVariantEditorFactory;
class QtVariantPropertyManager;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

/*!*******************************************************************************************************************
 * \class MainWindow
 * \brief The main application window for the simulation and substrate visualization tool.
 *
 * Provides a GUI for loading GDS files, editing simulation parameters, running Python-based simulations,
 * and visualizing substrate structures. It integrates QtPropertyBrowser for simulation settings,
 * manages user preferences and run configurations, and handles various file inputs including GDS and XML.
 *
 * Responsibilities include:
 * - Managing tabbed UI sections for workflow navigation.
 * - Handling user interactions for file selection and run execution.
 * - Displaying simulation progress and messages.
 * - Saving/loading configuration states.
 **********************************************************************************************************************/
class MainWindow : public QMainWindow
{
    Q_OBJECT

    enum class ModelType { Palace, OpenEMS, Unknown };
    enum class PalacePhase { None, PythonModel, PalaceSolver };
    enum class RequiredFolderDecision { ChooseAnotherDir, SaveAnyway, Cancel };

    /*!*******************************************************************************************************************
    * \brief PortInfo representation of a simulation_port(...) call extracted from a Python script.
    **********************************************************************************************************************/
    struct PortInfo
    {
        int         portnumber  = 0;
        double      voltage     = 0.0;
        double      z0          = 50.0;
        QString     sourceLayer;
        bool        sourceIsNumber = true;
        QString     fromLayer;
        QString     toLayer;
        QString     direction;
    };

    struct PalacePropInfo
    {
        int         propType = QVariant::String;
        QVariant    value;
        int         decimals = 12;
        double      step = 0.0;
    };

    struct PalaceRunContext
    {
        QString simKeyLower;

        QString modelWin;
        QString launcherWin;
        int     runMode = 0;

        QString baseName;
        QString runDirGuessWin;

        QString palaceRoot;

#ifdef Q_OS_WIN
        QString distro;
#endif

        QString pythonCmd;

        QString palaceExeLinux;
        QString modelDirLinux;
        QString modelLinux;

        QString detectedRunDirWin;
        QString searchDirWin;

        QString configPathWin;
        QString configLinux;
    };

    struct CoreCountResult
    {
        QString cores;
        QString source;  // "physical (lscpu)" / "logical (nproc)"
    };


public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void                            setTopCell(const QString &cellName);
    void                            setGdsFile(const QString &filePath);
    void                            tryAutoLoadRecentPythonForTopCell();
    void                            loadPythonModel(const QString &fileName);

private slots:
    void                            closeEvent(QCloseEvent *event);
    void                            on_lstRunControl_itemClicked(QListWidgetItem *item);
    void                            onSimulationSettingChanged(QtProperty *property, const QVariant &value);

    void                            on_actionExit_triggered();
    void                            on_actionSave_triggered();
    void                            on_actionSave_As_triggered();
    void                            on_btnGdsFile_clicked();
    void                            on_txtGdsFile_textEdited(const QString &arg1);

    void                            on_txtGdsFile_textChanged(const QString &arg1);
    void                            on_btnAddPort_clicked();

    void                            on_btnReomovePort_clicked();
    void                            on_btnRemovePorts_clicked();

    void                            on_btnSubstrate_clicked();
    void                            on_txtSubstrate_textEdited(const QString &arg1);
    void                            on_txtSubstrate_textChanged(const QString &arg1);

    void                            on_actionPrefernces_triggered();

    void                            on_btnRunPythonScript_clicked();
    void                            on_txtRunPythonScript_textEdited(const QString &arg1);
    void                            on_txtRunPythonScript_textChanged(const QString &arg1);

    void                            on_btnRun_clicked();
    void                            on_btnStop_clicked();

    void                            on_cbSubLayerNames_stateChanged(int arg1);
    void                            on_btnGenDefaultPython_clicked();
    void                            on_cbxSimTool_currentIndexChanged(int index);
    void                            on_actionOpen_Python_Model_triggered();
    void                            onOpenRecentPythonModel();

private:
    void                            saveSettings();
    void                            loadSettings();
    void                            setStateSaved();
    void                            setStateChanged();
    void                            setupTabMapping();
    void                            updateGdsUserInfo();
    void                            setupSettingsPanel();
    void                            showTab(int indexToShow);
    void                            updateSimulationSettings();
    void                            importPortsFromEditor();
    void                            hookPortCombo(QComboBox* box);
    void                            drawSubstrate(const QString &filePath);

    void                            initRecentMenu();
    void                            updateRecentMenu();
    void                            addRecentPythonModel(const QString& filePath);
    QStringList                     recentPythonModels() const;
    void                            setRecentPythonModels(const QStringList& list);

    QStringList                     extractGdsCellNames(const QString &filePath);
    QSet<QPair<int, int>>           extractGdsLayerNumbers(const QString &filePath);

    QStringList                     readSubstrateLayers(const QString &xmlFilePath);
    QHash<int, QString>             readSubstrateLayerMap(const QString &xmlFilePath);

    void                            info(const QString &msg, bool clear = false);
    void                            error(const QString &msg, bool clear = false);

    void                            loadPythonScriptToEditor(const QString &filePath);
    void                            setLineEditPalette(QLineEdit* lineEdit, const QString& path);
    void                            applySimSettingsToScript(QString &script, const QString &simKeyLower);
    bool                            variantToPythonLiteral(const QVariant &v, QString *outLiteral);
    bool                            keyIsExcludedForEm(const QString &key);
    void                            applyOpenEmsSettings(QString &script);
    void                            applyPalaceSettings(QString &script);
    void                            applyBoundaries(QString &script, bool alsoTopLevelAssignment);
    void                            applyGdsAndXmlPaths(QString &script, const QString &simKeyLower);
    QString                         makeScriptPathForPython(QString nativePath, const QString &simKeyLower) const;
    void                            ensurePortsTableInitializedFromScript(const QString &script);
    QString                         buildPortCodeFromGuiTable() const;
    QVector<QPair<int,int>>         findPortBlocks(const QString &script);
    void                            replaceOrInsertPortSection(QString &script, const QString &portCode);
    void                            setEditorScriptPreservingState(const QString &script);

    void                            applyOneSettingToScript(QString &script,
                                                            const QString &key,
                                                            const QVariant &val,
                                                            const QString &simKeyLower);

    QString                         loadOrReusePythonScriptText(const QString &filePath);

    bool                            readTextFileUtf8(const QString &fileName, QString &outText);

    void                            updateSubLayerNamesCheckboxState();
    void                            rebuildLayerMapping();

    bool                            applyPythonScriptFromEditor();
    void                            applySubLayerNamesToPorts(bool toNames);
    bool                            ensurePythonScriptPathBySaveAs(bool forceDialog);

    QString                         currentPythonScriptPath() const;
    void                            initSaveAsSuggestion(const QString &currentPath,
                                                         QString &startDir,
                                                         QString &suggestedName) const;
    QString                         bestDefaultModelDirectory(const QString &prefPath) const;
    QString                         bestTopCellName() const;
    QString                         showPythonModelSaveAsDialog(const QString &startDir,
                                                                const QString &suggestedName) const;
    QString                         ensurePySuffix(QString path) const;
    QString                         requiredFolderForSim(const QString &simKeyLower) const;
    bool                            validateRequiredFolderForSim(const QString &dirPath,
                                                                 const QString &simKeyLower,
                                                                 QString *missingName) const;
    RequiredFolderDecision          askMissingFolderDecision(const QString &simKeyLower,
                                                             const QString &missingFolder) const;
    void                            commitChosenPythonModelPath(const QString &path);


    void                            updateSubLayerNamesAutoCheck();
    void                            rebuildSimulationSettingsFromPalace(const QMap<QString, QVariant>& settings,
                                                                        const QMap<QString, QString>& tips,
                                                                        const QMap<QString, QVariant>& topLevelVars);

    void                            clearSimSettingsGroup();
    QString                         findBoundariesKeyCaseInsensitive(const QMap<QString, QVariant> &settings) const;
    QStringList                     parseBoundariesItems(const QVariant &v) const;
    void                            applyBoundariesToUiAndSettings(const QStringList &items, const QString &simTool);
    bool                            shouldSkipPalaceSettingKey(const QString &key) const;
    PalacePropInfo                  inferPalacePropertyInfo(const QString &key, const QVariant &val) const;
    bool                            shouldSkipStringSelfReference(const QString &key, const PalacePropInfo &info) const;
    void                            applyTipIfAny(QtVariantProperty *prop, const QString &key, const QMap<QString, QString> &tips) const;
    void                            setupDoubleAttributes(QtVariantProperty *prop, const PalacePropInfo &info) const;

    bool                            isStateChanged() const;

    QString                         currentSimToolKey() const;
    QString                         toWslPath(const QString &winPath) const;
    QString                         fromWslPath(const QString &wslPath) const;

    QString                         createDefaultOpenemsScript();
    QString                         createDefaultPalaceScript();

    void                            runOpenEMS();
    void                            runPalace();

    bool                            buildPalaceRunContext(PalaceRunContext &ctx, QString &outError);
    void                            logPalaceStartupInfo(const PalaceRunContext &ctx);

    void                            startPalacePythonStage(const PalaceRunContext &ctx);
    void                            startPalaceSolverStage(PalaceRunContext &ctx);

    void                            failPalaceSolver(const QString &message, bool showDialog);

    bool                            startPalaceLauncherStage(PalaceRunContext &ctx);
    bool                            preparePalaceSolverLaunch(PalaceRunContext &ctx,
                                                              QString &outWorkDirLinux,
                                                              QString &outCmd,
                                                              QString &outCores);

    bool                            runPalaceSolverWindows(const PalaceRunContext &ctx, const QString &cmd);
    bool                            runPalaceSolverLinux(const PalaceRunContext &ctx,
                                                         const QString &workDirLinux,
                                                         const QString &cmd);

    QString                         queryWslCpuCores(const QString &distro) const;
    CoreCountResult                 detectMpiCoreCount() const;

    void                            connectPalaceProcessIo();
    void                            onPalaceProcessFinished(int exitCode);
    QString                         detectPhysicalCoreCountLinux() const;

    void                            appendToSimulationLog(const QByteArray &data);
    QString                         detectRunDirFromLog() const;

    QString                         guessDefaultPalaceRunDir(const QString &modelFile, const QString &baseName) const;
    QString                         chooseSearchDir(const QString &detectedRunDir, const QString &defaultRunDir) const;

    QString                         findPalaceConfigJson(const QString &runDir) const;

#ifdef Q_OS_WIN
    bool                            ensureWslAvailable(QString &outError) const;
    QString                         wslToWinPath(const QString &p) const;
#endif

    void                            setupWindowMenuDocks();

    void                            refreshSimToolOptions();
    bool                            pathLooksValid(const QString &path, const QString &relativeExe = QString()) const;
    bool                            fileLooksValid(const QString &path) const;

    QVector<PortInfo>               parsePortsFromScript(const QString& script);
    void                            appendParsedPortsToTable(const QVector<PortInfo>& ports);

private:
    Ui::MainWindow                  *m_ui;

    QList<QWidget*>                 m_tabWidgets;
    QStringList                     m_tabTitles;
    QMap<QString, int>              m_tabMap;

    QString                         m_modelGdsKey;
    QString                         m_modelXmlKey;
    QString                         m_palacePythonOutput;

    QStringList                     m_cells;
    QSet<QPair<int, int>>           m_layers;
    QStringList                     m_subLayers;

    QHash<int, QString>             m_gdsToSubName;
    QHash<QString, int>             m_subNameToGds;

    QMap<QString, QVariant>         m_preferences;
    QMap<QString, QVariant>         m_simSettings;
    QMap<QString, QVariant>         m_sysSettings;

    bool                            m_blockPortChanges;

    QProcess                        *m_simProcess = nullptr;

    QtVariantPropertyManager        *m_variantManager = nullptr;
    QtTreePropertyBrowser           *m_propertyBrowser = nullptr;
    QtVariantProperty               *m_simSettingsGroup = nullptr;

    static constexpr int            kMaxRecentPythonModels = 5;

    QMenu*                          m_menuRecent = nullptr;
    QVector<QAction*>               m_recentModelActions;

    PythonParser::Result            m_curPythonData;

    PalacePhase                     m_palacePhase = PalacePhase::None;

};

#endif // MAINWINDOW_H
