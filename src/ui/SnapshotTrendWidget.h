#pragma once

#include <QWidget>

#include "services/SnapshotService.h"

namespace opentree {

class SnapshotTrendWidget : public QWidget {
    Q_OBJECT

public:
    explicit SnapshotTrendWidget(QWidget *parent = nullptr);

    void setSnapshots(const QVector<SnapshotSummary> &snapshots);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<SnapshotSummary> m_snapshots;
};

}
