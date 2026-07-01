#pragma once

#include <QWidget>

#include "services/SnapshotService.h"
#include "domain/ScanTypes.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QTableWidget)

namespace opentree {

class HeatmapPanel : public QWidget {
    Q_OBJECT

public:
    explicit HeatmapPanel(QWidget *parent = nullptr);

    void setHeatmapData(const QVector<TreeEntry> &entries, const QVector<SnapshotCompareRow> &compareRows);
    void setViewMetric(ViewMetric metric);

private:
    QLabel *m_summaryLabel;
    QTableWidget *m_table;
    ViewMetric m_viewMetric = ViewMetric::Percentage;
};

}
