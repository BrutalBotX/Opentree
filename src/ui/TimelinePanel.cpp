#include "ui/TimelinePanel.h"

#include "ui/SnapshotTrendWidget.h"
#include "ui/SnapshotTimelineWidget.h"

#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

#include "utils/SizeFormatter.h"

namespace opentree {

TimelinePanel::TimelinePanel(QWidget *parent)
    : QWidget(parent)
    , m_scopeLabel(new QLabel(this))
    , m_emptyStateLabel(new QLabel(this))
    , m_diagnosticsLabel(new QLabel(this))
    , m_summaryCardsLabel(new QLabel(this))
    , m_timelineWidget(new SnapshotTimelineWidget(this))
    , m_trendWidget(new SnapshotTrendWidget(this))
    , m_snapshotList(new QListWidget(this))
    , m_compareButton(new QPushButton("Compare with Current Scan", this))
    , m_compareSummaryLabel(new QLabel(this))
    , m_topChangesLabel(new QLabel(this))
    , m_topChangesTable(new QTableWidget(this))
    , m_compareTable(new QTableWidget(this))
    , m_eventSummaryLabel(new QLabel(this))
    , m_eventList(new QListWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *label = new QLabel("Snapshots", this);
    layout->addWidget(label);
    layout->addWidget(m_scopeLabel);
    m_diagnosticsLabel->setWordWrap(true);
    layout->addWidget(m_diagnosticsLabel);
    m_summaryCardsLabel->setWordWrap(true);
    layout->addWidget(m_summaryCardsLabel);
    layout->addWidget(m_timelineWidget);
    layout->addWidget(m_trendWidget);

    m_emptyStateLabel->setWordWrap(true);
    layout->addWidget(m_emptyStateLabel);
    m_snapshotList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_snapshotList);
    layout->addWidget(m_compareButton);
    m_compareSummaryLabel->setWordWrap(true);
    layout->addWidget(m_compareSummaryLabel);
    m_topChangesLabel->setWordWrap(true);
    layout->addWidget(m_topChangesLabel);
    m_topChangesTable->setColumnCount(3);
    m_topChangesTable->setHorizontalHeaderLabels({"Path", "Delta", "%"});
    m_topChangesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_topChangesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_topChangesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_topChangesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_topChangesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_topChangesTable);

    m_compareTable->setColumnCount(5);
    m_compareTable->setHorizontalHeaderLabels({"Folder", "Previous", "Current", "Delta", "% Change"});
    m_compareTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_compareTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_compareTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_compareTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_compareTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_compareTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_compareTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_compareTable, 1);

    m_eventSummaryLabel->setWordWrap(true);
    layout->addWidget(m_eventSummaryLabel);
    layout->addWidget(m_eventList, 1);

    setCurrentRootPath(QString());
    setDiagnosticsState({});
    setCompareResult({}, {}, {});

    connect(m_compareButton, &QPushButton::clicked, this, [this]() {
        const int snapshotId = selectedSnapshotId();
        if (snapshotId == 0) {
            return;
        }
        emit compareSnapshotRequested(snapshotId);
    });

    connect(m_snapshotList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= m_visibleSnapshots.size()) {
            return;
        }
        m_timelineWidget->selectSnapshotId(m_visibleSnapshots[row].id);
    });

    connect(m_timelineWidget, &SnapshotTimelineWidget::snapshotSelected, this, [this](int snapshotId) {
        selectSnapshotId(snapshotId);
    });
}

void TimelinePanel::setCurrentRootPath(const QString &rootPath)
{
    m_currentRootPath = rootPath;
    m_scopeLabel->setText(rootPath.isEmpty()
            ? QStringLiteral("Current root: none")
            : QStringLiteral("Current root: %1").arg(rootPath));
}

void TimelinePanel::setSnapshots(const QVector<SnapshotSummary> &snapshots)
{
    const int previouslySelectedId = selectedSnapshotId();
    m_visibleSnapshots.clear();
    m_snapshotList->clear();
    for (const SnapshotSummary &snapshot : snapshots) {
        if (!m_currentRootPath.isEmpty() && snapshot.rootPath.compare(m_currentRootPath, Qt::CaseInsensitive) != 0) {
            continue;
        }
        m_visibleSnapshots.push_back(snapshot);
    }

    for (int index = 0; index < m_visibleSnapshots.size(); ++index) {
        const SnapshotSummary &snapshot = m_visibleSnapshots[index];
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  |  %2 changed folders  |  %3 files  |  %4")
                .arg(snapshot.createdAt)
                .arg(snapshot.itemCount)
                .arg(snapshot.fileCount)
                .arg(snapshot.rootPath),
            m_snapshotList);
        item->setData(Qt::UserRole, snapshot.id);
        if (snapshot.id == previouslySelectedId || (previouslySelectedId == 0 && index == 0)) {
            m_snapshotList->setCurrentRow(index);
        }
    }

    m_timelineWidget->setSnapshots(m_visibleSnapshots);
    m_trendWidget->setSnapshots(m_visibleSnapshots);
    const bool hasSnapshots = !m_visibleSnapshots.isEmpty();
    m_emptyStateLabel->setVisible(!hasSnapshots);
    m_snapshotList->setVisible(hasSnapshots);
    m_timelineWidget->setVisible(hasSnapshots);
    m_trendWidget->setVisible(hasSnapshots);
    if (!m_currentRootPath.isEmpty()) {
        m_scopeLabel->setText(QStringLiteral("Current root: %1 | Snapshots: %2").arg(m_currentRootPath).arg(m_visibleSnapshots.size()));
    }
    if (hasSnapshots) {
        const SnapshotSummary &latest = m_visibleSnapshots.last();
        const SnapshotSummary &first = m_visibleSnapshots.first();
        const qint64 totalGrowth = latest.totalSize - first.totalSize;
        m_summaryCardsLabel->setText(
            QStringLiteral("Latest size: %1 | Snapshots: %2 | Files: %3 | Net growth: %4")
                .arg(SizeFormatter::formatBytes(latest.totalSize))
                .arg(m_visibleSnapshots.size())
                .arg(latest.fileCount)
                .arg(SizeFormatter::formatBytes(totalGrowth)));
    } else {
        m_summaryCardsLabel->setText(QString());
    }
    m_emptyStateLabel->setText(hasSnapshots
            ? QString()
            : (m_currentRootPath.isEmpty()
                    ? QStringLiteral("Scan a folder to see its snapshot history.")
                    : QStringLiteral("No snapshots for the current root yet. Create a baseline snapshot first, or lower the threshold.")));
    m_compareButton->setEnabled(hasSnapshots);
}

void TimelinePanel::selectSnapshotId(int snapshotId)
{
    for (int index = 0; index < m_visibleSnapshots.size(); ++index) {
        if (m_visibleSnapshots[index].id == snapshotId) {
            m_snapshotList->setCurrentRow(index);
            m_timelineWidget->selectSnapshotId(snapshotId);
            return;
        }
    }
}

int TimelinePanel::selectedSnapshotId() const
{
    auto *item = m_snapshotList->currentItem();
    if (item) {
        return item->data(Qt::UserRole).toInt();
    }
    return m_timelineWidget->selectedSnapshotId();
}

int TimelinePanel::visibleSnapshotCount() const
{
    return m_visibleSnapshots.size();
}

void TimelinePanel::setDiagnosticsState(const DiagnosticsState &state)
{
    QString scheduleText = state.scheduleEnabled
        ? QStringLiteral("Enabled | %1 at %2 | Whitelist roots: %3").arg(state.cadence, state.runAt).arg(state.whitelistCount)
        : QStringLiteral("Disabled | Whitelist roots: %1").arg(state.whitelistCount);

    QString runText = QStringLiteral("No background run recorded yet.");
    if (!state.latestRun.startedAt.isEmpty()) {
        runText = QStringLiteral("Last run: %1 | status: %2 | roots: %3 | snapshots: %4 | compacted: %5")
                      .arg(state.latestRun.finishedAt.isEmpty() ? state.latestRun.startedAt : state.latestRun.finishedAt)
                      .arg(state.latestRun.status)
                      .arg(state.latestRun.rootsProcessed)
                      .arg(state.latestRun.snapshotsCreated)
                      .arg(state.latestRun.eventsCompacted);
        if (!state.latestRun.message.isEmpty()) {
            runText += QStringLiteral(" | %1").arg(state.latestRun.message);
        }
    }

    m_diagnosticsLabel->setText(QStringLiteral("Schedule: %1\n%2").arg(scheduleText, runText));
}

void TimelinePanel::setCompareResult(const SnapshotCompareResult &summary, const QVector<SnapshotCompareRow> &rows, const QVector<SnapshotFileEvent> &events)
{
    if (!summary.found) {
        m_compareSummaryLabel->setText(QStringLiteral("Select a snapshot and compare it with the current scan."));
        m_topChangesLabel->setText(QString());
        m_topChangesTable->setRowCount(0);
    } else {
        m_compareSummaryLabel->setText(
            QStringLiteral("Snapshot: %1 | Changed folders: %2 | File events: %3 | Total delta: %4 bytes | Largest growth: %5 (%6 bytes) | Largest shrink: %7 (%8 bytes)")
                .arg(summary.snapshotCreatedAt)
                .arg(summary.changedFolderCount)
                .arg(summary.fileEventCount)
                .arg(summary.totalDeltaBytes)
                .arg(summary.largestGrowthPath.isEmpty() ? QStringLiteral("-") : summary.largestGrowthPath)
                .arg(summary.largestGrowthBytes)
                .arg(summary.largestShrinkPath.isEmpty() ? QStringLiteral("-") : summary.largestShrinkPath)
                .arg(summary.largestShrinkBytes));

        QVector<SnapshotCompareRow> sortedRows = rows;
        std::sort(sortedRows.begin(), sortedRows.end(), [](const SnapshotCompareRow &left, const SnapshotCompareRow &right) {
            return std::llabs(left.deltaBytes) > std::llabs(right.deltaBytes);
        });
        QStringList highlights;
        QVector<SnapshotCompareRow> topRows;
        int growths = 0;
        int shrinks = 0;
        for (const SnapshotCompareRow &row : sortedRows) {
            if (row.deltaBytes > 0 && growths < 3) {
                highlights << QStringLiteral("grow: %1 (%2)").arg(row.path, SizeFormatter::formatBytes(row.deltaBytes));
                topRows.push_back(row);
                ++growths;
            } else if (row.deltaBytes < 0 && shrinks < 3) {
                highlights << QStringLiteral("shrink: %1 (%2)").arg(row.path, SizeFormatter::formatBytes(-row.deltaBytes));
                topRows.push_back(row);
                ++shrinks;
            }
            if (growths >= 3 && shrinks >= 3) {
                break;
            }
        }
        m_topChangesLabel->setText(highlights.isEmpty() ? QString() : QStringLiteral("Top movers: %1").arg(highlights.join(QStringLiteral(" | "))));
        m_topChangesTable->setRowCount(topRows.size());
        for (int rowIndex = 0; rowIndex < topRows.size(); ++rowIndex) {
            const SnapshotCompareRow &row = topRows[rowIndex];
            m_topChangesTable->setItem(rowIndex, 0, new QTableWidgetItem(row.path));
            m_topChangesTable->setItem(rowIndex, 1, new QTableWidgetItem(QString::number(row.deltaBytes)));
            m_topChangesTable->setItem(rowIndex, 2, new QTableWidgetItem(row.percentChangeText));
        }
    }

    m_compareTable->setRowCount(rows.size());
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const SnapshotCompareRow &row = rows[rowIndex];
        m_compareTable->setItem(rowIndex, 0, new QTableWidgetItem(row.path));
        m_compareTable->setItem(rowIndex, 1, new QTableWidgetItem(QString::number(row.previousSize)));
        m_compareTable->setItem(rowIndex, 2, new QTableWidgetItem(QString::number(row.currentSize)));
        m_compareTable->setItem(rowIndex, 3, new QTableWidgetItem(QString::number(row.deltaBytes)));
        m_compareTable->setItem(rowIndex, 4, new QTableWidgetItem(row.percentChangeText));
    }

    m_eventSummaryLabel->setText(QStringLiteral("File Events: %1").arg(events.size()));
    m_eventList->clear();
    for (const SnapshotFileEvent &event : events) {
        m_eventList->addItem(
            QStringLiteral("[%1] %2 | old: %3 | new: %4")
                .arg(event.eventType)
                .arg(event.path)
                .arg(event.oldSize)
                .arg(event.newSize));
    }
}

}
