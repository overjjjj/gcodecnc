#include "src/ui/ContourMachiningChoice.h"

#include <QCoreApplication>
#include <QTextStream>

#include <cstdlib>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << Qt::endl;
        std::exit(1);
    }
}

ContourFeature pointContour()
{
    ContourFeature feature;
    feature.subType = QStringLiteral("closed_contour");
    feature.points = {
        QVector3D(0.0f, 0.0f, 0.0f),
        QVector3D(10.0f, 0.0f, 0.0f),
        QVector3D(10.0f, 5.0f, 0.0f),
        QVector3D(0.0f, 5.0f, 0.0f)
    };
    return feature;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    require(isManualContourChoiceStrategy(QStringLiteral("mill_closed_contour")),
            "closed contour should require manual machining choice");
    require(isManualContourChoiceStrategy(QStringLiteral("mill_open_contour")),
            "open contour should require manual machining choice");
    require(!isManualContourChoiceStrategy(QStringLiteral("mill_blind_slot")),
            "slot strategies should not use the contour-side dialog");

    ContourFeature contour = pointContour();
    require(canReverseContourDirection(contour),
            "a point contour should support direction reversal");
    StrategyParams params;
    ContourMachiningChoice choice;
    choice.direction = ContourTraversalDirection::Reverse;
    choice.compensation = ContourCompensationChoice::RightG42;
    choice.closedContour = true;
    choice.startPointIndex = 2;
    applyContourMachiningChoice(contour, params, choice);

    require(contour.points.first() == QVector3D(10.0f, 5.0f, 0.0f),
            "closed contour should begin at the selected start point");
    require(contour.points.at(1) == QVector3D(10.0f, 0.0f, 0.0f),
            "reverse choice should continue through the previous contour point");
    require(params.get(QStringLiteral("compensation"), 0.0) == -1.0,
            "right compensation should map to G42");

    choice.direction = ContourTraversalDirection::Forward;
    choice.startPointIndex = 0;
    choice.compensation = ContourCompensationChoice::LeftG41;
    applyContourMachiningChoice(contour, params, choice);
    require(params.get(QStringLiteral("compensation"), 0.0) == 1.0,
            "left compensation should map to G41");

    choice.compensation = ContourCompensationChoice::CamOffsetG40;
    applyContourMachiningChoice(contour, params, choice);
    require(params.get(QStringLiteral("compensation"), 1.0) == 0.0,
            "CAM-side compensation should map to G40 strategy mode");

    ContourFeature fallback;
    fallback.radius = 10.0;
    require(!canReverseContourDirection(fallback),
            "center/radius fallback geometry must not claim reversible point order");
    require(contourPreviewPoints(fallback).size() == 4,
            "fallback closed geometry should still provide a truthful preview outline");

    ContourFeature openContour;
    openContour.subType = QStringLiteral("open_contour");
    openContour.points = {
        QVector3D(0, 0, 0), QVector3D(5, 0, 0), QVector3D(10, 0, 0)
    };
    choice.closedContour = false;
    choice.direction = ContourTraversalDirection::Reverse;
    choice.startPointIndex = 2;
    applyContourMachiningChoice(openContour, params, choice);
    require(openContour.points.first() == QVector3D(10, 0, 0)
            && openContour.points.last() == QVector3D(0, 0, 0),
            "open contour reverse should start at the opposite endpoint");

    QTextStream(stdout) << "PASS contour_machining_choice_test" << Qt::endl;
    return 0;
}
