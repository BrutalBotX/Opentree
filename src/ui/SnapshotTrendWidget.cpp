#include "ui/SnapshotTrendWidget.h"

#include <QPaintEvent>
#include <QPainter>

#include <algorithm>

#include "utils/SizeFormatter.h"

namespace opentree {

SnapshotTrendWidget::SnapshotTrendWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
}

void SnapshotTrendWidget::setSnapshots(const QVector<SnapshotSummary> &snapshots)
{
    m_snapshots = snapshots;
    update();
}

void SnapshotTrendWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(18, 21, 30));

    if (m_snapshots.isEmpty()) {
        painter.setPen(QColor(150, 160, 180));
        painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter, "No snapshot trend yet");
        return;
    }

    const QRectF plot = rect().adjusted(14, 14, -14, -18);
    qint64 minSize = m_snapshots.first().totalSize;
    qint64 maxSize = m_snapshots.first().totalSize;
    for (const SnapshotSummary &snapshot : m_snapshots) {
        minSize = std::min(minSize, snapshot.totalSize);
        maxSize = std::max(maxSize, snapshot.totalSize);
    }

    painter.setPen(QPen(QColor(48, 58, 82), 1));
    painter.drawRoundedRect(plot, 8, 8);

    QPolygonF line;
    for (int index = 0; index < m_snapshots.size(); ++index) {
        const double x = m_snapshots.size() == 1 ? plot.center().x() : plot.left() + (plot.width() * index) / (m_snapshots.size() - 1);
        const double ratio = maxSize == minSize ? 0.5 : double(m_snapshots[index].totalSize - minSize) / double(maxSize - minSize);
        const double y = plot.bottom() - ratio * plot.height();
        line << QPointF(x, y);
    }

    painter.setPen(QPen(QColor(94, 168, 255), 2));
    painter.drawPolyline(line);
    painter.setBrush(QColor(242, 142, 43));
    painter.setPen(Qt::NoPen);
    for (const QPointF &point : line) {
        painter.drawEllipse(point, 3.5, 3.5);
    }

    painter.setPen(QColor(190, 200, 220));
    painter.drawText(QRectF(plot.left(), 0, plot.width(), 14), Qt::AlignLeft | Qt::AlignVCenter, SizeFormatter::formatBytes(maxSize));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 2, plot.width(), 14), Qt::AlignLeft | Qt::AlignVCenter, SizeFormatter::formatBytes(minSize));
}

}
