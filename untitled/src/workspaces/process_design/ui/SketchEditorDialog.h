#pragma once

#include <QDialog>
#include <QHash>
#include "../../../core/SketchDocument.h"

class QGraphicsView;
class QGraphicsScene;
class QTableWidget;
class QLineEdit;
class QLabel;
class QPushButton;
class QComboBox;
class QListWidget;

class SketchEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SketchEditorDialog(const SketchDocument &document, QWidget *parent = nullptr);
    SketchDocument document() const { return m_document; }

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void startDrawing(const QString &kind);
    void refresh(bool rebuildTable = true);
    void readCoordinates();
    void fitDrawing();
    void transformSelected(const QString &operation);
    void createPrimitive(const QString &kind);
    void editMeasurements();
    void trimSelected(bool extend);
    void offsetSelected();
    void filletSelected();
    void rememberChange();
    void undoChange();
    SketchContour selectedContour() const;
    struct UndoState { SketchDocument document; int selected; QHash<QString,bool> rectangleDirections; };
    QVector<UndoState> m_history;
    int m_selected = 0;
    QComboBox *m_contours;
    QPushButton *m_undoEdit;
    QPushButton *m_measurements;
    QPushButton *m_offset;
    QPushButton *m_fillet;
    QListWidget *m_annotationList;
    QVector<QPushButton *> m_transforms;
    QVector<QPushButton *> m_trimButtons;
    QVector<QPushButton *> m_primitives;
    QWidget *m_curveProperties;
    QLineEdit *m_curveRadius;
    QLineEdit *m_curveStart;
    QLineEdit *m_curveSweep;
    SketchDocument m_document;
    QGraphicsView *m_view;
    QGraphicsScene *m_scene;
    QTableWidget *m_points;
    QLineEdit *m_planeZ;
    QLabel *m_status;
    QPushButton *m_save;
    QPushButton *m_finish;
    QPushButton *m_undo;
    QString m_drawingKind;
    QVector<QPointF> m_pending;
    bool m_coordinatesValid = true;
    QHash<QString,bool> m_rectangleDirections;
};
