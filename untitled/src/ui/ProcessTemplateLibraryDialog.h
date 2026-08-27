#pragma once

#include "../core/ProcessTemplateLibrary.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

class ProcessTemplateLibraryDialog : public QDialog
{
public:
    explicit ProcessTemplateLibraryDialog(
        const ProcessTemplateLibrary &library,
        QWidget *parent = nullptr);

    void setChineseUi(bool chinese);
    const ProcessTemplateLibrary &library() const { return library_; }

private:
    void addTemplateVersion();
    void addPlanVersion();
    void refreshLists();
    void retranslateUi();
    void showError(const QString &message);

    ProcessTemplateLibrary library_;
    bool chinese_ = true;
    QListWidget *templateList_ = nullptr;
    QListWidget *planList_ = nullptr;
    QLineEdit *templateId_ = nullptr;
    QLineEdit *templateVersion_ = nullptr;
    QComboBox *templateSource_ = nullptr;
    QLineEdit *parameterId_ = nullptr;
    QLineEdit *parameterValue_ = nullptr;
    QLineEdit *planId_ = nullptr;
    QLineEdit *planVersion_ = nullptr;
    QLineEdit *strategyId_ = nullptr;
    QSpinBox *toolId_ = nullptr;
    QLineEdit *startExpression_ = nullptr;
    QLineEdit *depthExpression_ = nullptr;
    QLineEdit *templateRefs_ = nullptr;
    QLabel *errorLabel_ = nullptr;
    QPushButton *addTemplateButton_ = nullptr;
    QPushButton *addPlanButton_ = nullptr;
    QPushButton *saveButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
};
