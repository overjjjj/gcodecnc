#pragma once

#include <QDialog>
#include "../core/MachineProfile.h"

class QLineEdit;
class QPlainTextEdit;

class MachineProfileDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MachineProfileDialog(QWidget *parent = nullptr);

    void setProfile(const MachineProfile &profile);
    MachineProfile profile() const { return m_profile; }

protected:
    void accept() override;

private:
    QLineEdit *m_idEdit = nullptr;
    QLineEdit *m_versionEdit = nullptr;
    QLineEdit *m_controllerEdit = nullptr;
    QPlainTextEdit *m_safeStartEdit = nullptr;
    MachineProfile m_profile;
};
