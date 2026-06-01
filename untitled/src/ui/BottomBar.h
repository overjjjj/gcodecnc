#pragma once
#include <QWidget>
#include <QLabel>
#include <QProgressBar>

class QComboBox;

class BottomBar : public QWidget
{
    Q_OBJECT
public:
    explicit BottomBar(QWidget *parent = nullptr);

    void setStatus(const QString &msg);
    void setProgress(double fraction);   // 0.0 – 1.0
    void showProgress(bool visible);

signals:
    void simulationSpeedChanged(double multiplier);

private:
    QLabel       *m_label;
    QProgressBar *m_progress;
    QLabel       *m_speedLabel;
    QComboBox    *m_speedCombo;
};
