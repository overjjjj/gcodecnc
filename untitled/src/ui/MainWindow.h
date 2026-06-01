#pragma once
#include <QMainWindow>
#include <QDockWidget>
#include <QAction>
#include <QComboBox>
#include <QMenu>
#include <QToolBar>
#include <QTranslator>

class ViewportWidget;
class FeatureListPanel;
class StrategyPanel;
class ToolLibraryPanel;
class OperationListPanel;
class GCodeEditor;
class BottomBar;
class SimulationController;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onImportStep();
    void onSaveProject();
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

    void onSetFrontFace();

private:
    void createMenus();
    void createToolBar();
    void createDocks();
    void createStatusBar();
    void connectSignals();
    void retranslateUi();
    void switchLanguage(const QString &lang);
    bool isChineseUi() const;
    QString currentWorkOffset() const;
    void jumpToGeneratedOperation(int operationNumber);

    // Central
    ViewportWidget    *m_viewport;
    GCodeEditor       *m_gcodeEditor;

    // Docks
    QDockWidget       *m_featureDock = nullptr;
    QDockWidget       *m_strategyDock = nullptr;
    QDockWidget       *m_toolDock = nullptr;
    QDockWidget       *m_gcodeDock = nullptr;
    QDockWidget       *m_operationDock = nullptr;
    FeatureListPanel  *m_featurePanel;
    StrategyPanel     *m_strategyPanel;
    ToolLibraryPanel  *m_toolPanel;
    OperationListPanel *m_operationPanel;

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
    QAction *m_actOpenProject = nullptr;
    QAction *m_actSaveProject = nullptr;
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
    bool     m_settingFrontFace = false;
    int      m_pendingFrontFaceIndex = -1;  // 待确认的面索引，-1 表示无

    QComboBox *m_ppCombo = nullptr;
    QComboBox *m_wcsCombo = nullptr;
};
