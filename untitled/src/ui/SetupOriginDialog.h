#pragma once

#include <QDialog>

#include "../core/SetupOrigin.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;

class SetupOriginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SetupOriginDialog(bool chinese, QWidget *parent = nullptr);

    void setModelBounds(const QVector3D &minimum, const QVector3D &maximum);
    void setOrigin(const SetupOrigin &origin);
    SetupOrigin origin() const;
    void setWorkOffset(const QString &workOffset);
    QString workOffset() const;

protected:
    void accept() override;

private:
    void updateResolvedPoint();
    QDoubleSpinBox *createCoordinateSpinBox();

    QComboBox *m_workOffsetCombo = nullptr;
    QComboBox *m_anchorCombo = nullptr;
    QDoubleSpinBox *m_customX = nullptr;
    QDoubleSpinBox *m_customY = nullptr;
    QDoubleSpinBox *m_customZ = nullptr;
    QDoubleSpinBox *m_offsetX = nullptr;
    QDoubleSpinBox *m_offsetY = nullptr;
    QDoubleSpinBox *m_offsetZ = nullptr;
    QLabel *m_resolvedLabel = nullptr;
    QVector3D m_bbMin;
    QVector3D m_bbMax;
    bool m_chinese = true;
};
