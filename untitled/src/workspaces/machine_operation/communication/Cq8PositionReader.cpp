#include "Cq8PositionReader.h"
#include <QRegularExpression>
#include <limits>

Cq8PositionReader::Cq8PositionReader(QObject *parent, int timeoutMs) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(qMax(1, timeoutMs));
    QObject::connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_pending) fail(tr("CQ8 位置查询超时，请重新核对反馈地址。"));
    });
}

void Cq8PositionReader::invalidate()
{
    const bool hadPosition = !m_positions.isEmpty();
    m_positions.clear();
    if (hadPosition) emit positionsInvalidated();
}

void Cq8PositionReader::stop()
{
    m_timer.stop();
    m_pending = false;
    m_verified = false;
    m_response.clear();
    invalidate();
}

void Cq8PositionReader::fail(const QString &message)
{
    stop();
    emit errorOccurred(message);
}

void Cq8PositionReader::issue(int variable, const QString &command)
{
    m_variable = variable;
    m_response.clear();
    m_pending = true;
    const quint64 request = ++m_requestId;
    m_timer.start();
    invalidate();
    if (m_pending && request == m_requestId) emit readRequested(request, command);
}

bool Cq8PositionReader::start()
{
    if (m_pending) return false;
    m_verified = false;
    issue(103, QStringLiteral("I103"));
    return true;
}

bool Cq8PositionReader::requestPositions()
{
    if (!m_verified || m_pending) return false;
    issue(6920, QStringLiteral("I6920..6922"));
    return true;
}

void Cq8PositionReader::appendResponse(quint64 requestId, const QByteArray &bytes)
{
    if (!m_pending || requestId != m_requestId) return;
    if (bytes.size() > 4096 - m_response.size()) {
        fail(tr("CQ8 位置响应过长。"));
        return;
    }
    m_response.append(bytes);
}

void Cq8PositionReader::finishResponse(quint64 requestId)
{
    if (!m_pending || requestId != m_requestId) return;
    m_timer.stop();
    m_pending = false;
    QString text = QString::fromLatin1(m_response).trimmed();
    m_response.clear();
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace('\r', '\n');
    const QStringList lines = text.split('\n');
    const int count = m_variable == 6920 ? 3 : 1;
    QVector<quint32> values;
    const QRegularExpression valuePattern(QStringLiteral(
        "^(?:I([0-9]+)\\s*=\\s*)?(\\$[0-9A-Fa-f]{1,8}|[0-9]{1,10})$"));
    if (lines.size() != count) {
        fail(tr("CQ8 位置响应数量不符。"));
        return;
    }
    for (int index = 0; index < count; ++index) {
        const auto match = valuePattern.match(lines[index].trimmed());
        bool labelOk = true;
        const int label = match.captured(1).isEmpty() ? m_variable + index
            : match.captured(1).toInt(&labelOk);
        QString number = match.captured(2);
        const bool hex = number.startsWith('$');
        if (hex) number.remove(0, 1);
        bool numberOk = false;
        const quint64 value = number.toULongLong(&numberOk, hex ? 16 : 10);
        if (!match.hasMatch() || !labelOk || label != m_variable + index ||
            !numberOk || value > std::numeric_limits<quint32>::max()) {
            fail(tr("CQ8 位置响应格式或变量编号不符。"));
            return;
        }
        values.append(quint32(value));
    }
    if (m_variable != 6920) {
        const quint32 expected = 0x2000f4a0u + quint32((m_variable - 103) / 100) * 8u;
        if (values.first() != expected) {
            fail(tr("CQ8 I%1 反馈地址不匹配，未启用位置读取。期望地址：$%2")
                 .arg(m_variable).arg(expected, 8, 16, QLatin1Char('0')));
            return;
        }
        if (m_variable < 303) {
            const int next = m_variable + 100;
            issue(next, QStringLiteral("I%1").arg(next));
        } else {
            m_verified = true;
            requestPositions();
        }
        return;
    }
    m_positions = values;
    emit positionsUpdated(values[0], values[1], values[2]);
}
