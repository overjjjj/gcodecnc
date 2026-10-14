#pragma once
#include <QDialog>

class QComboBox;
class QSpinBox;
class QPushButton;
class QTextEdit;
class QLabel;
class CncCommInterface;

class CncSendDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CncSendDialog(const QString &gcode, QWidget *parent = nullptr);
    ~CncSendDialog() override;

private slots:
    void onConnectToggle();
    void onSend();
    void onResponse(const QString &data);
    void onCommError(const QString &msg);
    void onConnected();
    void onDisconnected();

private:
    void updateUi();

    QComboBox      *m_portCombo   = nullptr;
    QSpinBox       *m_baudSpin    = nullptr;
    QPushButton    *m_btnConnect  = nullptr;
    QPushButton    *m_btnSend     = nullptr;
    QTextEdit      *m_log         = nullptr;
    QLabel         *m_statusLabel = nullptr;

    CncCommInterface *m_comm = nullptr;
    QString           m_gcode;
    bool              m_connected = false;
};
