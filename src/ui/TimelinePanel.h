#pragma once

#include <QWidget>

#include "services/SnapshotService.h"

QT_FORWARD_DECLARE_CLASS(QListWidget)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QWidget)
QT_FORWARD_DECLARE_CLASS(QGroupBox)
QT_FORWARD_DECLARE_CLASS(QSplitter)
QT_FORWARD_DECLARE_CLASS(QTableWidget)

namespace opentree {

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
    void setCompareEnabled(bool enabled);
    void resetCompareState();

signals:
    void compareSnapshotRequested(int snapshotId);
    void snapshotSelected(int snapshotId);

private:
    QString m_currentRootPath;
    QVector<SnapshotSummary> m_visibleSnapshots;
    QWidget *m_overviewCard;
    QWidget *m_historyCard;
    QSplitter *m_bodySplitter;
    SnapshotTrendWidget *m_trendWidget;
    QGroupBox *m_compareCard;
    QLabel *m_scopeLabel;
    QLabel *m_emptyStateLabel;
    QLabel *m_diagnosticsLabel;
    QLabel *m_summaryCardsLabel;
    QListWidget *m_snapshotList;
    QPushButton *m_compareButton;
    QLabel *m_compareSummaryLabel;
    QTableWidget *m_compareTable;
};

}
