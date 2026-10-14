#pragma once
#include "../../../import/StepImporter.h"
#include "../../../strategies/hole/HoleStrategyUtils.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QDataStream>
#include <QCryptographicHash>
#include <QMap>

inline QString autoHoleFeatureFingerprint(const QVector<MachiningFeature> &features)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_14);
    for (const auto &hole : features)
        stream << int(hole.kind) << hole.subType << hole.radius << hole.depth
               << hole.secondaryRadius << hole.pitch << hole.coordinatePrecisionVersion
               << hole.center.x() << hole.center.y() << hole.center.z()
               << hole.axis << int(hole.region) << hole.faceIndices;
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

struct AutoHoleGroup {
    QVector<HoleFeature> holes;
    QString reason;
};

inline QVector<AutoHoleGroup> autoHoleGroups(const QVector<MachiningFeature> &features)
{
    QVector<AutoHoleGroup> groups;
    QMap<QString, int> positions;
    for (const auto &hole : features) {
        if (hole.kind != FeatureKind::Hole && hole.kind != FeatureKind::Thread) continue;
        QString reason = holeAxisOutputError(hole, hole.depth);
        if (reason.isEmpty() && (!std::isfinite(hole.radius) || hole.radius <= 0
            || !std::isfinite(hole.depth) || hole.depth <= 0 || hole.faceIndices.isEmpty()
            || !std::isfinite(hole.center.x()) || !std::isfinite(hole.center.y())
            || !std::isfinite(hole.center.z()))) reason = QStringLiteral("缺少有效孔尺寸、中心或源面证据");
        if (reason.isEmpty() && (hole.kind != FeatureKind::Hole
            || (hole.subType != "through_hole" && hole.subType != "blind_hole")))
            reason = QStringLiteral("此孔型需单独选择工艺，不纳入基础钻孔建议");
        const QString key = hole.subType + ":" + QString::number(hole.radius, 'g', 17)
            + ":" + QString::number(hole.depth, 'g', 17) + ":" + reason;
        if (!positions.contains(key)) {
            positions.insert(key, groups.size());
            groups.append({{}, reason});
        }
        groups[positions.value(key)].holes.append(hole);
    }
    return groups;
}

class AutoHoleDialog : public QDialog {
public:
    explicit AutoHoleDialog(const QVector<MachiningFeature> &features, QWidget *parent = nullptr)
        : QDialog(parent), m_groups(autoHoleGroups(features))
    {
        setObjectName("autoHoleDialog");
        setWindowTitle(QStringLiteral("自动孔 · 分组预览"));
        resize(820, 480);
        auto *layout = new QVBoxLayout(this);
        auto *intro = new QLabel(QStringLiteral("当前模型全部识别孔，按孔径、深度及孔型分组。选一组进入参数页，确认后才创建工序。"), this);
        intro->setWordWrap(true);layout->addWidget(intro);
        m_table = new QTableWidget(m_groups.size(), 5, this);
        m_table->setObjectName("autoHoleGroups");
        m_table->setHorizontalHeaderLabels({QStringLiteral("孔型"), QStringLiteral("孔径 (mm)"),
            QStringLiteral("深度 (mm)"), QStringLiteral("数量"), QStringLiteral("建议 / 原因")});
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->verticalHeader()->hide();
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
        for (int row = 0; row < m_groups.size(); ++row) {
            const auto &group = m_groups[row];const auto &hole = group.holes.first();
            const QString type = hole.subType == "through_hole" ? QStringLiteral("通孔")
                : hole.subType == "blind_hole" ? QStringLiteral("盲孔") : hole.subType;
            const QStringList values = {type, QString::number(2 * hole.radius, 'g', 17),
                QString::number(hole.depth, 'g', 17), QString::number(group.holes.size()),
                group.reason.isEmpty() ? QStringLiteral("基础钻孔 G81 · 参数待确认") : group.reason};
            for (int col = 0; col < values.size(); ++col) {
                auto *item = new QTableWidgetItem(values[col]);item->setToolTip(values[col]);
                m_table->setItem(row, col, item);
            }
        }
        layout->addWidget(m_table, 1);
        auto *note = new QLabel(m_groups.isEmpty() ? QStringLiteral("当前模型没有识别孔，请先导入并识别模型。")
            : QStringLiteral("不自动选刀或推断材料切削值。参数页可切换适用孔工艺；请核对刀具、深度、转速、进给和安全高度。"), this);
        note->setWordWrap(true);layout->addWidget(note);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        auto *next = buttons->addButton(QStringLiteral("进入此组参数"), QDialogButtonBox::AcceptRole);
        next->setObjectName("autoHoleContinue");next->setEnabled(false);
        connect(m_table, &QTableWidget::itemSelectionChanged, this, [this, next] {
            const int row = m_table->currentRow();
            next->setEnabled(row >= 0 && row < m_groups.size() && m_groups[row].reason.isEmpty());
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(next, &QPushButton::clicked, this, [this] {
            const int row = m_table->currentRow();
            if (row >= 0 && row < m_groups.size() && m_groups[row].reason.isEmpty()) {
                m_selected = m_groups[row].holes;accept();
            }
        });
        layout->addWidget(buttons);
    }
    QVector<HoleFeature> selectedHoles() const { return m_selected; }
private:
    QVector<AutoHoleGroup> m_groups;
    QVector<HoleFeature> m_selected;
    QTableWidget *m_table = nullptr;
};
