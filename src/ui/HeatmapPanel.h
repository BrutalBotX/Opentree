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
    void setActiveFolderPath(const QString &path);
    void setViewMetric(ViewMetric metric);

signals:
    void entryActivated(const TreeEntry &entry);

private:
    void rebuild();

    QLabel *m_summaryLabel;
    QTableWidget *m_table;
    QVector<TreeEntry> m_entries;
    QVector<SnapshotCompareRow> m_compareRows;
    QString m_activeFolderPath;
    ViewMetric m_viewMetric = ViewMetric::Percentage;
};

}
