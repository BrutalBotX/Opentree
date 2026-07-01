#pragma once

#include <QWidget>

#include "services/SnapshotService.h"

namespace opentree {

class SnapshotTimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit SnapshotTimelineWidget(QWidget *parent = nullptr);

    void setSnapshots(const QVector<SnapshotSummary> &snapshots);
    void selectSnapshotId(int snapshotId);
    int selectedSnapshotId() const;

signals:
    void snapshotSelected(int snapshotId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int snapshotIndexAtPosition(const QPoint &position) const;
    QRect pointRectForIndex(int index) const;

    QVector<SnapshotSummary> m_snapshots;
    int m_selectedIndex = -1;
    int m_hoveredIndex = -1;
};

}
