#include "../src/ui/ParameterEditorDialog.h"

#include <QApplication>
#include <QMetaObject>
#include <QTableWidget>

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

int FindRow(const QTableWidget *table, const QString &key)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem *item = table->item(row, 0);
        if (item && item->data(Qt::UserRole).toString() == key) {
            return row;
        }
    }
    return -1;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    const ProcessParameterSchema schema = ProcessParameterSchema::CommonOperation();
    StrategyParams template_values = schema.defaultParams();
    template_values.set(QStringLiteral("feedRate"), 280.0);
    StrategyParams edited = template_values;
    edited.set(QStringLiteral("feedRate"), 640.0);

    QMap<QString, ProcessParameterSource> sources;
    for (const ProcessParameterDefinition &definition : schema.definitions()) {
        sources.insert(definition.id, ProcessParameterSource::SystemDefault);
    }
    sources.insert(QStringLiteral("feedRate"), ProcessParameterSource::ManualOverride);

    ParameterEditorDialog dialog;
    dialog.setChineseUi(true);
    dialog.setParameterSchema(schema);
    dialog.setParameterSources(sources);
    dialog.setTemplateParams(template_values,
                             QStringLiteral("aluminum_endmill"),
                             QStringLiteral("3"));
    dialog.setParams(edited);

    QTableWidget *table = dialog.findChild<QTableWidget *>(QStringLiteral("parameterTable"));
    const int feed_row = table ? FindRow(table, QStringLiteral("feedRate")) : -1;
    if (!expect(table != nullptr, "the schema editor table should be discoverable") ||
        !expect(table->columnCount() == 4,
                "schema editor should show parameter, value, unit and source") ||
        !expect(feed_row >= 0, "schema fields should be rendered") ||
        !expect(table->item(feed_row, 2)->text() == QStringLiteral("mm/min"),
                "feed rate should display its schema unit") ||
        !expect(table->item(feed_row, 3)->text().contains(QStringLiteral("人工")),
                "manual parameter provenance should be visible")) {
        return 1;
    }

    QMetaObject::invokeMethod(&dialog, "onRestoreDefaults", Qt::DirectConnection);
    table = dialog.findChild<QTableWidget *>(QStringLiteral("parameterTable"));
    const int restored_feed_row = FindRow(table, QStringLiteral("feedRate"));
    if (!expect(table->item(restored_feed_row, 1)->text().toDouble() == 280.0,
                "restore should use the applied template snapshot") ||
        !expect(dialog.templateDescription().contains(QStringLiteral("aluminum_endmill")) &&
                    dialog.templateDescription().contains(QStringLiteral("3")),
                "the applied template identity should remain traceable")) {
        return 1;
    }

    return 0;
}
