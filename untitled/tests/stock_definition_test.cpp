#include "src/core/StockDefinition.h"

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

bool closeTo(const QVector3D &actual, const QVector3D &expected)
{
    return (actual - expected).length() < 1.0e-5f;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QVector3D partMin(-10.0f, 20.0f, -5.0f);
    const QVector3D partMax(30.0f, 60.0f, 15.0f);

    StockDefinition stock;
    stock.minusX = 1.0;
    stock.plusX = 2.0;
    stock.minusY = 3.0;
    stock.plusY = 4.0;
    stock.minusZ = 5.0;
    stock.plusZ = 6.0;
    const StockBounds bounds = stock.resolvedBounds(partMin, partMax);
    require(closeTo(bounds.minimum, QVector3D(-11.0f, 17.0f, -10.0f)),
            "negative-side allowances should expand the minimum bounds");
    require(closeTo(bounds.maximum, QVector3D(32.0f, 64.0f, 21.0f)),
            "positive-side allowances should expand the maximum bounds");
    require(closeTo(bounds.size(), QVector3D(43.0f, 47.0f, 31.0f)),
            "resolved stock size should include all six allowances");

    stock.minusX = -2.0;
    stock.plusZ = -1.0;
    stock.normalize();
    require(stock.minusX == 0.0 && stock.plusZ == 0.0,
            "stock allowances must not become negative");

    require(stock.fingerprint().isEmpty(),
            "unconfirmed stock must not produce a machining-context fingerprint");
    stock.confirmed = true;
    const QString originalFingerprint = stock.fingerprint();
    require(!originalFingerprint.isEmpty(), "confirmed stock should have a stable fingerprint");
    stock.plusX += 0.01;
    require(stock.fingerprint() != originalFingerprint,
            "changing any allowance should invalidate the stock fingerprint");

    QTextStream(stdout) << "PASS stock_definition_test" << Qt::endl;
    return 0;
}
