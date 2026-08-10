#include "MachineProfileDialog.h"

#include "../core/MachineProfileValidator.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

MachineProfileDialog::MachineProfileDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Machine Profile"));
    resize(520, 420);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_idEdit = new QLineEdit(this);
    m_versionEdit = new QLineEdit(this);
    m_controllerEdit = new QLineEdit(this);
    form->addRow(tr("Profile ID"), m_idEdit);
    form->addRow(tr("Version"), m_versionEdit);
    form->addRow(tr("Controller"), m_controllerEdit);
    layout->addLayout(form);

    layout->addWidget(new QLabel(
        tr("Safe-start blocks (one output block per line). G90 and G54-G59 are resolved safely when generating."),
        this));
    m_safeStartEdit = new QPlainTextEdit(this);
    layout->addWidget(m_safeStartEdit, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &MachineProfileDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void MachineProfileDialog::setProfile(const MachineProfile &profile)
{
    m_profile = profile;
    m_idEdit->setText(profile.id);
    m_versionEdit->setText(profile.version);
    m_controllerEdit->setText(profile.controllerId);
    m_safeStartEdit->setPlainText(profile.safeStartBlocks.join(QLatin1Char('\n')));
}

void MachineProfileDialog::accept()
{
    MachineProfile candidate;
    candidate.id = m_idEdit->text().trimmed();
    candidate.version = m_versionEdit->text().trimmed();
    candidate.controllerId = m_controllerEdit->text().trimmed();
    candidate.safeStartBlocks =
        m_safeStartEdit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString &block : candidate.safeStartBlocks) {
        block = block.trimmed();
    }

    const MachineProfileValidationResult validation =
        MachineProfileValidator::validate(candidate);
    if (!validation.ok) {
        QMessageBox::warning(this,
                             tr("Invalid Machine Profile"),
                             validation.errors.join(QLatin1Char('\n')));
        return;
    }

    m_profile = candidate;
    QDialog::accept();
}
