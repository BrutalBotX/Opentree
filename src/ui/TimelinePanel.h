#pragma once

#include <QWidget>

#include "services/SnapshotService.h"

QT_FORWARD_DECLARE_CLASS(QListWidget)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QTableWidget)
QT_FORWARD_DECLARE_CLASS(QWidget)

namespace opentree {

class SnapshotTimelineWidget;
class SnapshotTrendWidget;

class TimelinePanel : public QWidget {
    Q_OBJECT

public:
    struct DiagnosticsState {
        bool scheduleEnabled = false;
        QString cadence;
        QString runAt;
        int whitelistCount = 0;
        BackgroundRunSummary latestRun;
    };

    explicit TimelinePanel(QWidget *parent = nullptr);

    void setCurrentRootPath(const QString &rootPath);
    void setSnapshots(const QVector<SnapshotSummary> &snapshots);
    int selectedSnapshotId() const;
    int visibleSnapshotCount() const;
    void setDiagnosticsState(const DiagnosticsState &state);
    void setCompareResult(const SnapshotCompareResult &summary, const QVector<SnapshotCompareRow> &rows, const QVector<SnapshotFileEvent> &events);
    void selectSnapshotId(int snapshotId);

signals:
    void compareSnapshotRequested(int snapshotId);

private:
    QString m_currentRootPath;
    QVector<SnapshotSummary> m_visibleSnapshots;
    QLabel *m_scopeLabel;
    QLabel *m_emptyStateLabel;
    QLabel *m_diagnosticsLabel;
    QLabel *m_summaryCardsLabel;
    SnapshotTimelineWidget *m_timelineWidget;
    SnapshotTrendWidget *m_trendWidget;
    QListWidget *m_snapshotList;
    QPushButton *m_compareButton;
    QLabel *m_compareSummaryLabel;
    QLabel *m_topChangesLabel;
    QTableWidget *m_topChangesTable;
    QTableWidget *m_compareTable;
    QLabel *m_eventSummaryLabel;
    QListWidget *m_eventList;
};

}
