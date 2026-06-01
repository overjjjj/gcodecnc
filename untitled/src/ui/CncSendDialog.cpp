#include "CncSendDialog.h"
#include "../communication/CncCommInterface.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

#ifdef CNEXT_ENABLE_SERIALPORT
#include <QSerialPortInfo>
#endif

CncSendDialog::CncSendDialog(const QString &gcode, QWidget *parent)
    : QDialog(parent)
    , m_gcode(gcode)
{
    setWindowTitle(tr("发送到机床"));
    setMinimumWidth(480);

    m_comm = new CncCommInterface(this);
    connect(m_comm, &CncCommInterface::connected,        this, &CncSendDialog::onConnected);
    connect(m_comm, &CncCommInterface::disconnected,     this, &CncSendDialog::onDisconnected);
    connect(m_comm, &CncCommInterface::responseReceived, this, &CncSendDialog::onResponse);
    connect(m_comm, &CncCommInterface::errorOccurred,    this, &CncSendDialog::onCommError);

    // -- Port / baud row --
    m_portCombo = new QComboBox(this);
#ifdef CNEXT_ENABLE_SERIALPORT
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        m_portCombo->addItem(info.portName());
    }
#endif
    if (m_portCombo->count() == 0) {
        m_portCombo->addItem(QStringLiteral("COM1"));
    }

    m_baudSpin = new QSpinBox(this);
    m_baudSpin->setRange(1200, 921600);
    m_baudSpin->setValue(9600);
    m_baudSpin->setSingleStep(9600);

    auto *formLayout = new QFormLayout;
    formLayout->addRow(tr("端口："), m_portCombo);
    formLayout->addRow(tr("波特率："), m_baudSpin);

    // -- Connect button --
    m_btnConnect = new QPushButton(tr("连接"), this);
    connect(m_btnConnect, &QPushButton::clicked, this, &CncSendDialog::onConnectToggle);

    auto *topRow = new QHBoxLayout;
    topRow->addLayout(formLayout, 1);
    topRow->addWidget(m_btnConnect, 0, Qt::AlignBottom);

    // -- Status label --
    m_statusLabel = new QLabel(tr("未连接"), this);

    // -- Log area --
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setFixedHeight(160);
    m_log->setPlaceholderText(tr("机床响应将显示在此处…"));

    // -- Send / Close buttons --
    m_btnSend = new QPushButton(tr("发送 G 代码"), this);
    m_btnSend->setEnabled(false);
    connect(m_btnSend, &QPushButton::clicked, this, &CncSendDialog::onSend);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->addWidget(m_btnSend);
    bottomRow->addStretch();
    bottomRow->addWidget(btnBox);

    auto *root = new QVBoxLayout(this);
    root->addLayout(topRow);
    root->addWidget(m_statusLabel);
    root->addWidget(m_log);
    root->addLayout(bottomRow);
}

CncSendDialog::~CncSendDialog()
{
    if (m_comm && m_comm->isConnected()) {
        m_comm->disconnect();
    }
}

void CncSendDialog::onConnectToggle()
{
    if (m_connected) {
        m_comm->disconnect();
    } else {
        const QString port = m_portCombo->currentText();
        const int baud     = m_baudSpin->value();
        m_comm->connect(port, baud);
    }
}

void CncSendDialog::onSend()
{
    if (!m_gcode.isEmpty()) {
        m_comm->sendGCode(m_gcode);
        m_log->append(tr("[发送] %1 字节").arg(m_gcode.toLocal8Bit().size()));
    }
}

void CncSendDialog::onResponse(const QString &data)
{
    m_log->append(tr("[响应] ") + data.trimmed());
}

void CncSendDialog::onCommError(const QString &msg)
{
    m_log->append(tr("[错误] ") + msg);
}

void CncSendDialog::onConnected()
{
    m_connected = true;
    updateUi();
    m_log->append(tr("[已连接] %1 @ %2").arg(m_portCombo->currentText()).arg(m_baudSpin->value()));
}

void CncSendDialog::onDisconnected()
{
    m_connected = false;
    updateUi();
    m_log->append(tr("[已断开]"));
}

void CncSendDialog::updateUi()
{
    m_btnConnect->setText(m_connected ? tr("断开") : tr("连接"));
    m_portCombo->setEnabled(!m_connected);
    m_baudSpin->setEnabled(!m_connected);
    m_btnSend->setEnabled(m_connected);
    m_statusLabel->setText(m_connected ? tr("已连接") : tr("未连接"));
}
