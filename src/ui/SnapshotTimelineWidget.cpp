#include "ui/SnapshotTimelineWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

namespace opentree {

SnapshotTimelineWidget::SnapshotTimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(64);
    setMouseTracking(true);
}

void SnapshotTimelineWidget::setSnapshots(const QVector<SnapshotSummary> &snapshots)
{
    const int previouslySelectedId = selectedSnapshotId();
    m_snapshots = snapshots;
    m_selectedIndex = -1;
    if (previouslySelectedId != 0) {
        selectSnapshotId(previouslySelectedId);
    } else if (!m_snapshots.isEmpty()) {
        m_selectedIndex = 0;
    }
    update();
}

void SnapshotTimelineWidget::selectSnapshotId(int snapshotId)
{
    m_selectedIndex = -1;
    for (int index = 0; index < m_snapshots.size(); ++index) {
        if (m_snapshots[index].id == snapshotId) {
            m_selectedIndex = index;
            break;
        }
    }
    update();
}

int SnapshotTimelineWidget::selectedSnapshotId() const
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_snapshots.size()) {
        return 0;
    }
    return m_snapshots[m_selectedIndex].id;
}

void SnapshotTimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int left = 24;
    const int right = width() - 24;
    const int centerY = height() / 2;

    painter.setPen(QPen(palette().mid().color(), 2));
    painter.drawLine(left, centerY, right, centerY);

    for (int index = 0; index < m_snapshots.size(); ++index) {
        const QRect rect = pointRectForIndex(index);
        QColor fill = palette().button().color();
        if (index == m_selectedIndex) {
            fill = palette().highlight().color();
        } else if (index == m_hoveredIndex) {
            fill = palette().light().color();
        }

        painter.setBrush(fill);
        painter.setPen(QPen(palette().dark().color(), 1));
        painter.drawEllipse(rect);
    }
}

void SnapshotTimelineWidget::mousePressEvent(QMouseEvent *event)
{
    const int index = snapshotIndexAtPosition(event->pos());
    if (index >= 0 && index < m_snapshots.size()) {
        m_selectedIndex = index;
        emit snapshotSelected(m_snapshots[index].id);
        update();
    }
    QWidget::mousePressEvent(event);
}

void SnapshotTimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int index = snapshotIndexAtPosition(event->pos());
    if (index != m_hoveredIndex) {
        m_hoveredIndex = index;
        update();
    }

    if (index >= 0 && index < m_snapshots.size()) {
        const SnapshotSummary &snapshot = m_snapshots[index];
        QToolTip::showText(
            event->globalPosition().toPoint(),
            QStringLiteral("%1\nChanged folders: %2\nFiles: %3")
                .arg(snapshot.createdAt)
                .arg(snapshot.itemCount)
                .arg(snapshot.fileCount),
            this);
    } else {
        QToolTip::hideText();
    }

    QWidget::mouseMoveEvent(event);
}

void SnapshotTimelineWidget::leaveEvent(QEvent *event)
{
    m_hoveredIndex = -1;
    QToolTip::hideText();
    update();
    QWidget::leaveEvent(event);
}

int SnapshotTimelineWidget::snapshotIndexAtPosition(const QPoint &position) const
{
    for (int index = 0; index < m_snapshots.size(); ++index) {
        if (pointRectForIndex(index).contains(position)) {
            return index;
        }
    }
    return -1;
}

QRect SnapshotTimelineWidget::pointRectForIndex(int index) const
{
    if (m_snapshots.isEmpty()) {
        return {};
    }

    const int left = 24;
    const int right = width() - 24;
    const int centerY = height() / 2;
    const double ratio = m_snapshots.size() == 1 ? 0.0 : static_cast<double>(index) / static_cast<double>(m_snapshots.size() - 1);
    const int x = left + static_cast<int>((right - left) * ratio);
    return QRect(x - 7, centerY - 7, 14, 14);
}

}
