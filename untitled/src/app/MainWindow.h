#pragma once
#include <QMainWindow>
#include <QPointer>
#include <QDockWidget>
#include <QAction>
#include <QComboBox>
#include <QMenu>
#include <QStringList>
#include <QToolBar>
#include <QTranslator>
#include "../import/StepImporter.h"
#include "../workspaces/program_verification/simulation/SimulationController.h"
#include "../workspaces/process_design/operations/OperationProposal.h"

class QDialog;
class QCloseEvent;
class ViewportWidget;
class FeatureListPanel;
class StrategyPanel;
class ToolLibraryPanel;
class OperationListPanel;
class GCodeEditor;
class BottomBar;
class AppWorkspaceRouter;
class ProjectCommandController;
class SimulationController;
class QListWidget;
class QStackedWidget;
class QLabel;
class QGroupBox;
class QToolButton;
class QPlainTextEdit;
struct ProgramEntry;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

signals:
    void activeRegionChanged(FaceRegion region);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onImportStep();
    void onEditSketch();
    void onImportGCode();
    void onSaveProject();
    void onSaveProjectAs();
    void onOpenProject();
    void onExportGCode();
    void onAbout();
    void onLanguageChinese();
    void onLanguageEnglish();
    void onResetCamera();
    void onSendToMachine();

    void onStepImported(const QString &filePath);
    void onGCodeReady(const QString &gcode);
    void onStatusMessage(const QString &msg);
    void onErrorOccurred(const QString &msg);

    void onSimPlay();
    void onSimPause();
    void onSimStop();

    void showSetupOverview();
    void onSetFrontFace(bool checked);
    void onEditSetupOrigin();
    void onSetOriginFromSelectedHole();
    void onEditStockDefinition();
    void onEditMachineProfile();

private:
    bool confirmProjectReplacement();
    void resetSourceViews();
    bool saveCurrentProject(bool saveAs = false);
    void beginOperationWorkflow(const QString &strategyId);
    void previewAutomaticHoles();
    void previewSmartChamfer();
    void showOperationParameters();
    void createMenus();
    void createToolBar();
    void createPages();
    void createStatusBar();
    void connectSignals();
    void retranslateUi();
    void switchLanguage(const QString &lang);
    bool isChineseUi() const;
    QString currentWorkOffset() const;
    void jumpToGeneratedOperation(int operationNumber);
    void syncProgramList();
    void syncCurrentProgramSnapshot();
    QString programSourceTooltip(const ProgramEntry &program) const;
    QString findProgramIdForOperation(const QString &operationId) const;
    int findOperationLine(const QString &gcode, const QString &operationId, int operationNumber) const;
    void loadProgramById(const QString &programId, bool syncSelection = true);
    bool loadExternalGCodeFile(const QString &filePath, QString *error);
    bool ensureSimulationLoaded();
    QString currentInternalVerificationText(Coordinate3D *origin) const;
    void selectExternalProgram(int index);
    bool validateCurrentGCodeForOutput(const QString &actionName);
    bool validateSetupForProposals(const QList<OperationProposal> &proposals);
    void updateProgramActionAvailability();
    void updateProgramReviewSummary();
    void setProgramReviewBadge(QLabel *badge,
                               const QString &text,
                               const QString &state);
    void updateDesignWorkflowSummary();
    void setDesignWorkflowStage(QToolButton *stage,
                                const QString &text,
                                const QString &state,
                                bool enabled);
    QString appendProgramSnapshot(const QString &baseName,
                                  const QString &gcode,
                                  const QString &sourceSummary = QString(),
                                  const QStringList &sourceOperationIds = QStringList());

    QPointer<QDialog> m_setupOverviewDialog;
    QPointer<QDialog> m_geometryDialog;
    QStackedWidget *m_designSideStack = nullptr;
    QWidget *m_selectionPanelHost = nullptr;
    QDialog *m_parameterDialog = nullptr;
    void refreshOperationPathVisibility();
    QPointer<QDialog> m_operationEditDialog;
    QString m_workflowStrategy;
    QString m_workflowSource, m_workflowSetup;
    QString m_autoHoleFeatureFingerprint;
    StrategyParams m_workflowDraft;
    int m_workflowTool = -1;
    int m_workflowFeatureIndex = -1;
    bool m_reselecting = false;
    QVector<HoleFeature> m_workflowHoles;
    SelectionChain m_workflowSelection;
    QList<SelectionChain> m_workflowSelections;
    QVector<int> m_workflowFeatureIndices;
    QVector<QVector<QVector3D>> m_workflowRegionPoints;
    QVector<QVector3D> m_workflowPoints;

    // Central
    ViewportWidget    *m_viewport;
    ViewportWidget    *m_simViewport;
    GCodeEditor       *m_gcodeEditor;
    QPlainTextEdit    *m_macroLibraryEditor = nullptr;
    QDockWidget       *m_featureDock = nullptr;
    QDockWidget       *m_strategyDock = nullptr;
    QDockWidget       *m_toolDock = nullptr;
    QDockWidget       *m_gcodeDock = nullptr;
    QDockWidget       *m_operationDock = nullptr;
    FeatureListPanel  *m_featurePanel;
    StrategyPanel     *m_strategyPanel;
    ToolLibraryPanel  *m_toolPanel;
    OperationListPanel *m_operationPanel;

    QListWidget      *m_pageNav = nullptr;
    AppWorkspaceRouter *m_workspaceRouter = nullptr;
    ProjectCommandController *m_projectCommandController = nullptr;
    QStackedWidget   *m_pageStack = nullptr;
    QLabel           *m_productTitleLabel = nullptr;
    QLabel           *m_workspaceTitleLabel = nullptr;
    QLabel           *m_cq8ConnectionBadge = nullptr;
    QLabel           *m_machineModeBadge = nullptr;
    QLabel           *m_activeTaskBadge = nullptr;
    QLabel           *m_safetyStateBadge = nullptr;
    QWidget          *m_designPage = nullptr;
    QWidget          *m_designWorkflowStrip = nullptr;
    QToolButton      *m_designModelStage = nullptr;
    QToolButton      *m_designSetupStage = nullptr;
    QToolButton      *m_designFeatureStage = nullptr;
    QToolButton      *m_designOperationStage = nullptr;
    QToolButton      *m_designProgramStage = nullptr;
    QWidget          *m_machiningPage = nullptr;
    QWidget          *m_machineControlPage = nullptr;
    QGroupBox        *m_machiningActionsGroup = nullptr;
    QListWidget      *m_programList = nullptr;
    QLabel           *m_machiningHintLabel = nullptr;
    QLabel           *m_programEmptyLabel = nullptr;
    QWidget          *m_programValidationStrip = nullptr;
    QLabel           *m_programSnapshotBadge = nullptr;
    QLabel           *m_programSafetyBadge = nullptr;
    QLabel           *m_simulationStateBadge = nullptr;
    QLabel           *m_outputReadinessBadge = nullptr;
    QGroupBox        *m_simulationPanel = nullptr;
    QGroupBox        *m_finalProgramPanel = nullptr;
    QGroupBox        *m_machineStatusGroup = nullptr;
    QGroupBox        *m_machineAxesGroup = nullptr;
    QGroupBox        *m_machineRunGroup = nullptr;
    QGroupBox        *m_machineLogGroup = nullptr;
    QLabel           *m_machineControlHintLabel = nullptr;

    BottomBar         *m_bottomBar;

    SimulationController *m_simCtrl = nullptr;

    QMenu             *m_fileMenu = nullptr;
    QMenu             *m_viewMenu = nullptr;
    QMenu             *m_langMenu = nullptr;
    QMenu             *m_helpMenu = nullptr;
    QToolBar          *m_mainToolBar = nullptr;

    QTranslator        m_qtTranslator;
    QTranslator        m_appTranslator;

    // Actions
    QAction *m_actImportStep = nullptr;
    QAction *m_actImportGCode = nullptr;
    QAction *m_actOpenProject = nullptr;
    QAction *m_actSaveProject = nullptr;
    QAction *m_actSaveProjectAs = nullptr;
    QAction *m_actExportGCode = nullptr;
    QAction *m_actExit = nullptr;
    QAction *m_actResetCamera = nullptr;
    QAction *m_actLangZh = nullptr;
    QAction *m_actLangEn = nullptr;
    QAction *m_actAbout = nullptr;
    QAction *m_actSendToMachine = nullptr;

    QAction *m_actSimPlay  = nullptr;
    QAction *m_actSimPause = nullptr;
    QAction *m_actSimStop  = nullptr;

    QAction *m_actSetFrontFace = nullptr;
    QAction *m_actSetupOrigin = nullptr;
    QAction *m_actOriginFromHole = nullptr;
    QAction *m_actStockDefinition = nullptr;
    QAction *m_actMachineProfile = nullptr;
    enum class SimulationReviewState {
        Unavailable,
        Ready,
        Running,
        Paused,
        Completed,
        Stopped
    };
    SimulationReviewState m_simulationReviewState = SimulationReviewState::Unavailable;
    bool     m_settingFrontFace = false;
    int      m_pendingFrontFaceIndex = -1;  // 待确认的面索引，-1 表示无
    QVector3D m_pendingFrontFaceNormal;

    FaceRegion m_activeRegion = FaceRegion::Unknown;
    QString   m_projectFilePath;
    QString   m_currentProgramId;
    QString   m_externalGCodePath;
    QString   m_externalGCodeText;
    QVector<ExternalProgramSection> m_externalPrograms;
    QComboBox *m_externalProgramCombo = nullptr;
    bool      m_updatingProgramList = false;
    bool      m_syncingOperationsFromPanel = false;
    QComboBox *m_ppCombo = nullptr;
    QComboBox *m_wcsCombo = nullptr;
};
