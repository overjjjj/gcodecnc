#include "HoleSelectionDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

HoleSelectionDialog::HoleSelectionDialog(QWidget *parent)
    : QDialog(parent)
    , m_showSerialCheck(new QCheckBox(this))
    , m_table(new QTableWidget(this))
    , m_sortDiameterButton(new QPushButton(this))
    // 函数说明：m_sortXButton负责当前模块的核心处理。
    , m_sortXButton(new QPushButton(this))
    // 函数说明：m_sortYButton负责当前模块的核心处理。
    , m_sortYButton(new QPushButton(this))
    // 函数说明：m_sortNearestButton负责当前模块的核心处理。
    , m_sortNearestButton(new QPushButton(this))
    // 函数说明：m_undoButton负责当前模块的核心处理。
    , m_undoButton(new QPushButton(this))
    // 函数说明：m_redoButton负责当前模块的核心处理。
    , m_redoButton(new QPushButton(this))
    // 函数说明：m_confirmButton负责当前模块的核心处理。
    , m_confirmButton(new QPushButton(this))
    // 函数说明：m_cancelButton负责当前模块的核心处理。
    , m_cancelButton(new QPushButton(this))
{
    setObjectName(QStringLiteral("holeSelectionDialog"));
    setModal(true);
    resize(520, 390);
    setStyleSheet(QStringLiteral(
        "#holeSelectionDialog { background: #ffffff; }"
        "#holeSelectionDialog QTableWidget { border: 1px solid #dbe3ef; gridline-color: #e8edf5; }"
        "#holeSelectionDialog QPushButton { min-height: 28px; border: 1px solid #c6d0df; border-radius: 5px; padding: 3px 8px; background: #ffffff; color: #27364d; }"
        "#holeSelectionDialog QPushButton:hover { background: #f3f6fb; border-color: #9fb0c8; }"
        "#holeSelectionDialog QPushButton#confirmButton { background: #2f6fec; color: #ffffff; border-color: #2f6fec; }"
        "#holeSelectionDialog QPushButton#confirmButton:hover { background: #245fd0; }"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *hint = new QLabel(this);
    hint->setObjectName(QStringLiteral("selectionHint"));
    hint->setText(QStringLiteral("已选孔位可排序；确认后将按当前顺序生成工序。"));
    layout->addWidget(hint);

    layout->addWidget(m_showSerialCheck);

    m_table->setColumnCount(4);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(m_table, 1);

    auto *sortLayout = new QHBoxLayout;
    sortLayout->setSpacing(6);
    sortLayout->addWidget(m_sortDiameterButton);
    sortLayout->addWidget(m_sortXButton);
    sortLayout->addWidget(m_sortYButton);
    sortLayout->addWidget(m_sortNearestButton);
    layout->addLayout(sortLayout);

    auto *historyLayout = new QHBoxLayout;
    historyLayout->addWidget(m_undoButton);
    historyLayout->addWidget(m_redoButton);
    historyLayout->addStretch(1);
    layout->addLayout(historyLayout);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_confirmButton->setObjectName(QStringLiteral("confirmButton"));
    buttons->addWidget(m_confirmButton);
    buttons->addWidget(m_cancelButton);
    layout->addLayout(buttons);

    // 函数说明：connect负责当前模块的核心处理。
    connect(m_showSerialCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_session.setDisplaySerialNumbers(checked);
        refreshTable();
    });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_sortDiameterButton, &QPushButton::clicked, this,
            [this]() { sortDiameterDescending(); });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_sortXButton, &QPushButton::clicked, this,
            [this]() { if (m_session.sort(HoleSelectionSort::XAscending)) refreshTable(); });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_sortYButton, &QPushButton::clicked, this,
            [this]() { if (m_session.sort(HoleSelectionSort::YAscending)) refreshTable(); });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_sortNearestButton, &QPushButton::clicked, this,
            [this]() { if (m_session.sort(HoleSelectionSort::NearestNeighbor)) refreshTable(); });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_undoButton, &QPushButton::clicked, this, [this]() { undo(); });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_redoButton, &QPushButton::clicked, this, [this]() { redo(); });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_confirmButton, &QPushButton::clicked, this, [this]() { confirm(); });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() { cancel(); });

    retranslateUi();
}

// 函数说明：setChineseUi负责当前模块的核心处理。
void HoleSelectionDialog::setChineseUi(bool chinese)
{
    m_chinese = chinese;
    retranslateUi();
}

// 函数说明：setRecords负责当前模块的核心处理。
void HoleSelectionDialog::setRecords(const QList<HoleSelectionRecord> &records)
{
    m_session = HoleSelectionSession(records);
    refreshTable();
}

// 函数说明：sortDiameterDescending负责当前模块的核心处理。
bool HoleSelectionDialog::sortDiameterDescending()
{
    if (!m_session.sort(HoleSelectionSort::DiameterDescending)) {
        return false;
    }
    refreshTable();
    return true;
}

// 函数说明：undo负责当前模块的核心处理。
bool HoleSelectionDialog::undo()
{
    if (!m_session.undo()) {
        return false;
    }
    refreshTable();
    return true;
}

// 函数说明：redo负责当前模块的核心处理。
bool HoleSelectionDialog::redo()
{
    if (!m_session.redo()) {
        return false;
    }
    refreshTable();
    return true;
}

// 函数说明：confirm负责当前模块的核心处理。
bool HoleSelectionDialog::confirm()
{
    if (m_session.records().isEmpty()) {
        return false;
    }
    m_session.commit();
    accept();
    return true;
}

// 函数说明：refreshTable负责当前模块的核心处理。
void HoleSelectionDialog::refreshTable()
{
    const QSignalBlocker blocker(m_showSerialCheck);
    m_showSerialCheck->setChecked(m_session.displaySerialNumbers());
    m_table->setRowCount(0);
    for (int row = 0; row < m_session.records().size(); ++row) {
        const HoleSelectionRecord &record = m_session.records().at(row);
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(
            m_session.displaySerialNumbers() ? QString::number(row + 1) : QStringLiteral("-")));
        m_table->setItem(row, 1, new QTableWidgetItem(
            QString::number(record.diameter, 'f', 3)));
        m_table->setItem(row, 2, new QTableWidgetItem(
            QString::number(record.center.x(), 'f', 3)));
        m_table->setItem(row, 3, new QTableWidgetItem(
            QString::number(record.center.y(), 'f', 3)));
    }
    m_undoButton->setEnabled(m_session.isDirty());
}

// 函数说明：retranslateUi负责当前模块的核心处理。
void HoleSelectionDialog::retranslateUi()
{
    setWindowTitle(m_chinese ? QStringLiteral("孔位选择与排序")
                             : QStringLiteral("Hole Selection and Order"));
    m_showSerialCheck->setText(m_chinese ? QStringLiteral("显示孔序号")
                                         : QStringLiteral("Show hole sequence"));
    m_sortDiameterButton->setText(m_chinese ? QStringLiteral("按直径降序")
                                            : QStringLiteral("Diameter desc."));
    m_sortXButton->setText(m_chinese ? QStringLiteral("按 X 排序") : QStringLiteral("Sort X"));
    m_sortYButton->setText(m_chinese ? QStringLiteral("按 Y 排序") : QStringLiteral("Sort Y"));
    m_sortNearestButton->setText(m_chinese ? QStringLiteral("点到点排序")
                                           : QStringLiteral("Nearest neighbor"));
    m_undoButton->setText(m_chinese ? QStringLiteral("撤销") : QStringLiteral("Undo"));
    m_redoButton->setText(m_chinese ? QStringLiteral("恢复") : QStringLiteral("Redo"));
    m_confirmButton->setText(m_chinese ? QStringLiteral("确定") : QStringLiteral("Confirm"));
    m_cancelButton->setText(m_chinese ? QStringLiteral("取消") : QStringLiteral("Cancel"));
    m_table->setHorizontalHeaderLabels({
        m_chinese ? QStringLiteral("序号") : QStringLiteral("#"),
        m_chinese ? QStringLiteral("直径") : QStringLiteral("Diameter"),
        QStringLiteral("X"),
        QStringLiteral("Y")
    });
    refreshTable();
}

// 函数说明：cancel负责当前模块的核心处理。
void HoleSelectionDialog::cancel()
{
    m_session.cancel();
    reject();
}
