#include "ProcessTemplateLibraryDialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

ProcessTemplateLibraryDialog::ProcessTemplateLibraryDialog(
    const ProcessTemplateLibrary &library,
    QWidget *parent)
    : QDialog(parent)
    , library_(library)
    , templateList_(new QListWidget(this))
    , planList_(new QListWidget(this))
    , templateId_(new QLineEdit(this))
    , templateVersion_(new QLineEdit(this))
    , templateSource_(new QComboBox(this))
    , parameterId_(new QLineEdit(this))
    , parameterValue_(new QLineEdit(this))
    , planId_(new QLineEdit(this))
    , planVersion_(new QLineEdit(this))
    , strategyId_(new QLineEdit(this))
    , toolId_(new QSpinBox(this))
    , startExpression_(new QLineEdit(this))
    , depthExpression_(new QLineEdit(this))
    , templateRefs_(new QLineEdit(this))
    , errorLabel_(new QLabel(this))
    , addTemplateButton_(new QPushButton(this))
    , addPlanButton_(new QPushButton(this))
    , saveButton_(new QPushButton(this))
    , cancelButton_(new QPushButton(this))
{
    setObjectName(QStringLiteral("processTemplateLibraryDialog"));
    setModal(true);
    resize(840, 540);
    setStyleSheet(QStringLiteral(
        "#processTemplateLibraryDialog { background: #ffffff; }"
        "#processTemplateLibraryDialog QGroupBox { font-weight: 600; margin-top: 10px; }"
        "#processTemplateLibraryDialog QLineEdit, #processTemplateLibraryDialog QComboBox, #processTemplateLibraryDialog QSpinBox { min-height: 27px; border: 1px solid #c6d0df; border-radius: 5px; padding: 2px 7px; }"
        "#processTemplateLibraryDialog QListWidget { border: 1px solid #dbe3ef; }"
        "#processTemplateLibraryDialog QPushButton { min-height: 28px; border: 1px solid #c6d0df; border-radius: 5px; padding: 3px 10px; background: #ffffff; color: #27364d; }"
        "#processTemplateLibraryDialog QPushButton:hover { background: #f3f6fb; border-color: #9fb0c8; }"
        "#processTemplateLibraryDialog QPushButton#saveButton { background: #2f6fec; color: #ffffff; border-color: #2f6fec; }"));

    templateSource_->addItem(QStringLiteral("Common"),
                             int(ProcessParameterSource::SystemDefault));
    templateSource_->addItem(QStringLiteral("Material"),
                             int(ProcessParameterSource::MaterialTemplate));
    templateSource_->addItem(QStringLiteral("Tool type"),
                             int(ProcessParameterSource::ToolTypeTemplate));
    templateSource_->addItem(QStringLiteral("Module rule"),
                             int(ProcessParameterSource::ModuleRule));
    toolId_->setRange(1, 9999);
    startExpression_->setText(QStringLiteral("AT"));
    depthExpression_->setText(QStringLiteral("H1-AT"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);
    auto *immutabilityHint = new QLabel(this);
    immutabilityHint->setObjectName(QStringLiteral("immutabilityHint"));
    immutabilityHint->setWordWrap(true);
    layout->addWidget(immutabilityHint);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    auto *templateGroup = new QGroupBox(this);
    templateGroup->setObjectName(QStringLiteral("templateVersionGroup"));
    auto *templateLayout = new QVBoxLayout(templateGroup);
    templateLayout->addWidget(templateList_, 1);
    auto *templateForm = new QFormLayout;
    templateForm->addRow(QStringLiteral("ID"), templateId_);
    templateForm->addRow(QStringLiteral("Version"), templateVersion_);
    templateForm->addRow(QStringLiteral("Source"), templateSource_);
    templateForm->addRow(QStringLiteral("Parameter"), parameterId_);
    templateForm->addRow(QStringLiteral("Value"), parameterValue_);
    templateLayout->addLayout(templateForm);
    templateLayout->addWidget(addTemplateButton_);
    splitter->addWidget(templateGroup);

    auto *planGroup = new QGroupBox(this);
    planGroup->setObjectName(QStringLiteral("machiningPlanGroup"));
    auto *planLayout = new QVBoxLayout(planGroup);
    planLayout->addWidget(planList_, 1);
    auto *planForm = new QFormLayout;
    planForm->addRow(QStringLiteral("ID"), planId_);
    planForm->addRow(QStringLiteral("Version"), planVersion_);
    planForm->addRow(QStringLiteral("Strategy"), strategyId_);
    planForm->addRow(QStringLiteral("Tool ID"), toolId_);
    planForm->addRow(QStringLiteral("Start"), startExpression_);
    planForm->addRow(QStringLiteral("Depth"), depthExpression_);
    planForm->addRow(QStringLiteral("Templates"), templateRefs_);
    planLayout->addLayout(planForm);
    planLayout->addWidget(addPlanButton_);
    splitter->addWidget(planGroup);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    errorLabel_->setStyleSheet(QStringLiteral("color: #b3261e;"));
    errorLabel_->setWordWrap(true);
    layout->addWidget(errorLabel_);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    saveButton_->setObjectName(QStringLiteral("saveButton"));
    buttons->addWidget(saveButton_);
    buttons->addWidget(cancelButton_);
    layout->addLayout(buttons);

    connect(addTemplateButton_, &QPushButton::clicked, this,
            [this]() { addTemplateVersion(); });
    connect(addPlanButton_, &QPushButton::clicked, this,
            [this]() { addPlanVersion(); });
    connect(saveButton_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);

    retranslateUi();
    refreshLists();
}

void ProcessTemplateLibraryDialog::setChineseUi(bool chinese)
{
    chinese_ = chinese;
    retranslateUi();
}

void ProcessTemplateLibraryDialog::addTemplateVersion()
{
    bool valueOk = false;
    const double value = parameterValue_->text().trimmed().toDouble(&valueOk);
    if (!valueOk) {
        showError(chinese_ ? QStringLiteral("参数值必须是数字。")
                           : QStringLiteral("Parameter value must be numeric."));
        return;
    }
    ProcessTemplateDefinition definition;
    definition.id = templateId_->text().trimmed();
    definition.version = templateVersion_->text().trimmed();
    definition.name = definition.id;
    definition.source = ProcessParameterSource(
        templateSource_->currentData().toInt());
    definition.values.set(parameterId_->text().trimmed(), value);
    QString error;
    if (!library_.add(definition, &error)) {
        showError(error);
        return;
    }
    showError(QString());
    refreshLists();
}

void ProcessTemplateLibraryDialog::addPlanVersion()
{
    MachiningPlanDefinition plan;
    plan.id = planId_->text().trimmed();
    plan.version = planVersion_->text().trimmed();
    plan.name = plan.id;
    plan.strategyId = strategyId_->text().trimmed();
    plan.toolId = toolId_->value();
    plan.startExpression = startExpression_->text().trimmed();
    plan.depthExpression = depthExpression_->text().trimmed();
    for (const QString &ref : templateRefs_->text().split(
             QLatin1Char(','), Qt::SkipEmptyParts)) {
        plan.templateRefs.append(ref.trimmed());
    }
    QString error;
    if (!library_.addPlan(plan, &error)) {
        showError(error);
        return;
    }
    showError(QString());
    refreshLists();
}

void ProcessTemplateLibraryDialog::refreshLists()
{
    templateList_->clear();
    for (const ProcessTemplateDefinition &definition : library_.definitions()) {
        templateList_->addItem(QStringLiteral("%1 · %2")
                                   .arg(definition.ref(),
                                        ProcessParameterSourceName(definition.source)));
    }
    planList_->clear();
    for (const MachiningPlanDefinition &plan : library_.plans()) {
        planList_->addItem(QStringLiteral("%1 · T%2 · %3")
                               .arg(plan.ref())
                               .arg(plan.toolId)
                               .arg(plan.strategyId));
    }
}

void ProcessTemplateLibraryDialog::retranslateUi()
{
    setWindowTitle(chinese_ ? QStringLiteral("工艺资料与规则")
                            : QStringLiteral("Process Data and Rules"));
    if (QLabel *hint = findChild<QLabel*>(QStringLiteral("immutabilityHint"))) {
        hint->setText(chinese_
            ? QStringLiteral("模板和方案按版本追加，版本不可覆盖。修改资料不会静默改变已确认工序；需要显式重新应用。")
            : QStringLiteral("Template and plan versions are append-only. Data changes never silently update confirmed operations."));
    }
    if (QGroupBox *group = findChild<QGroupBox*>(QStringLiteral("templateVersionGroup"))) {
        group->setTitle(chinese_ ? QStringLiteral("参数模板")
                                 : QStringLiteral("Parameter templates"));
    }
    if (QGroupBox *group = findChild<QGroupBox*>(QStringLiteral("machiningPlanGroup"))) {
        group->setTitle(chinese_ ? QStringLiteral("自动孔加工方案")
                                 : QStringLiteral("Automatic-hole plans"));
    }
    addTemplateButton_->setText(chinese_ ? QStringLiteral("新增模板版本")
                                         : QStringLiteral("Add template version"));
    addPlanButton_->setText(chinese_ ? QStringLiteral("新增方案版本")
                                     : QStringLiteral("Add plan version"));
    saveButton_->setText(chinese_ ? QStringLiteral("保存资料")
                                  : QStringLiteral("Save data"));
    cancelButton_->setText(chinese_ ? QStringLiteral("取消")
                                    : QStringLiteral("Cancel"));
}

void ProcessTemplateLibraryDialog::showError(const QString &message)
{
    errorLabel_->setText(message);
    errorLabel_->setVisible(!message.isEmpty());
}
