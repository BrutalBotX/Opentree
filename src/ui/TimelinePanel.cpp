#include "ui/TimelinePanel.h"

#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <cstdlib>

#include "ui/SnapshotTrendWidget.h"
#include "utils/SizeFormatter.h"

namespace opentree {

TimelinePanel::TimelinePanel(QWidget *parent)
    : QWidget(parent)
    , m_overviewCard(new QWidget(this))
    , m_historyCard(new QWidget(this))
    , m_bodySplitter(new QSplitter(Qt::Horizontal, this))
    , m_trendWidget(new SnapshotTrendWidget(this))
    , m_compareCard(new QGroupBox(QStringLiteral("Snapshot Compare"), this))
    , m_scopeLabel(new QLabel(this))
    , m_emptyStateLabel(new QLabel(this))
    , m_diagnosticsLabel(new QLabel(this))
    , m_summaryCardsLabel(new QLabel(this))
    , m_snapshotList(new QListWidget(this))
    , m_compareButton(new QPushButton("Compare with Current Scan", this))
    , m_compareSummaryLabel(new QLabel(this))
    , m_compareTable(new QTableWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto makeCard = [this](QWidget *card, const QString &title) {
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 14, 14, 14);
        cardLayout->setSpacing(8);
        auto *heading = new QLabel(title, card);
        heading->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 600;"));
        cardLayout->addWidget(heading);
        return cardLayout;
    };

    auto *overviewLayout = makeCard(m_overviewCard, QStringLiteral("Overview"));
    m_scopeLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 600;"));
    overviewLayout->addWidget(m_scopeLabel);
    m_diagnosticsLabel->setWordWrap(true);
    overviewLayout->addWidget(m_diagnosticsLabel);
    m_summaryCardsLabel->setWordWrap(true);
    overviewLayout->addWidget(m_summaryCardsLabel);

    m_bodySplitter->setChildrenCollapsible(false);

    auto *historyLayout = makeCard(m_historyCard, QStringLiteral("Snapshot History"));
    m_trendWidget->setMinimumHeight(120);
    historyLayout->addWidget(m_trendWidget);
    m_snapshotList->setSelectionMode(QAbstractItemView::SingleSelection);
    historyLayout->addWidget(m_snapshotList, 1);
    m_emptyStateLabel->setWordWrap(true);
    historyLayout->addWidget(m_emptyStateLabel);
    historyLayout->addWidget(m_compareButton);

    auto *leftStack = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftStack);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);
    leftLayout->addWidget(m_overviewCard);
    leftLayout->addWidget(m_historyCard, 1);
    m_bodySplitter->addWidget(leftStack);

    auto *compareLayout = new QVBoxLayout(m_compareCard);
    compareLayout->setContentsMargins(12, 12, 12, 12);
    compareLayout->setSpacing(6);
    m_compareSummaryLabel->setWordWrap(true);
    m_compareSummaryLabel->setTextFormat(Qt::RichText);
    compareLayout->addWidget(m_compareSummaryLabel);

    m_compareTable->setColumnCount(2);
    m_compareTable->setHorizontalHeaderLabels({"Name", "Delta"});
    m_compareTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_compareTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_compareTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_compareTable->horizontalHeader()->setStretchLastSection(true);
    m_compareTable->setMinimumHeight(120);
    compareLayout->addWidget(m_compareTable);
    m_bodySplitter->addWidget(m_compareCard);
    m_bodySplitter->setStretchFactor(0, 3);
    m_bodySplitter->setStretchFactor(1, 2);
    layout->addWidget(m_bodySplitter, 1);
    m_compareCard->setVisible(true);

    setCurrentRootPath(QString());
    setDiagnosticsState({});
    resetCompareState();

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
        emit snapshotSelected(m_visibleSnapshots[row].id);
    });

    m_snapshotList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_snapshotList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        QListWidgetItem *item = m_snapshotList->itemAt(position);
        if (!item) {
            return;
        }
        QMenu menu(this);
        QAction *compareAction = menu.addAction("Compare with Current Scan");
        QAction *copyAction = menu.addAction("Copy Snapshot Text");
        QAction *selected = menu.exec(m_snapshotList->viewport()->mapToGlobal(position));
        if (selected == compareAction) {
            const int snapshotId = item->data(Qt::UserRole).toInt();
            if (snapshotId != 0) {
                emit compareSnapshotRequested(snapshotId);
            }
        } else if (selected == copyAction) {
            QGuiApplication::clipboard()->setText(item->text());
        }
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
    resetCompareState();
    m_visibleSnapshots.clear();
    m_snapshotList->clear();
    for (const SnapshotSummary &snapshot : snapshots) {
        if (!m_currentRootPath.isEmpty() && snapshot.rootPath.compare(m_currentRootPath, Qt::CaseInsensitive) != 0) {
            continue;
        }
        m_visibleSnapshots.push_back(snapshot);
    }

    std::reverse(m_visibleSnapshots.begin(), m_visibleSnapshots.end());

    for (int index = 0; index < m_visibleSnapshots.size(); ++index) {
        const SnapshotSummary &snapshot = m_visibleSnapshots[index];
        const QString createdAt = snapshot.createdAt.contains('T') ? QString(snapshot.createdAt).replace('T', ' ') : snapshot.createdAt;
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  |  %2 changed folders  |  %3 files  |  %4")
                .arg(createdAt)
                .arg(snapshot.itemCount)
                .arg(snapshot.fileCount)
                .arg(snapshot.rootPath),
            m_snapshotList);
        item->setData(Qt::UserRole, snapshot.id);
        if (snapshot.id == previouslySelectedId || (previouslySelectedId == 0 && index == 0)) {
            m_snapshotList->setCurrentRow(index);
        }
    }

    m_trendWidget->setSnapshots(m_visibleSnapshots);

    const bool hasSnapshots = !m_visibleSnapshots.isEmpty();
    m_emptyStateLabel->setVisible(!hasSnapshots);
    m_snapshotList->setVisible(hasSnapshots);
    m_trendWidget->setVisible(hasSnapshots);
    m_historyCard->setVisible(true);
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
            return;
        }
    }
}

void TimelinePanel::setCompareEnabled(bool enabled)
{
    m_compareButton->setEnabled(enabled && !m_visibleSnapshots.isEmpty());
}

void TimelinePanel::resetCompareState()
{
    m_compareSummaryLabel->setText(QStringLiteral("Select a snapshot to compare with the current scan."));
    m_compareTable->clearContents();
    m_compareTable->setRowCount(0);
}

int TimelinePanel::selectedSnapshotId() const
{
    auto *item = m_snapshotList->currentItem();
    if (item) {
        return item->data(Qt::UserRole).toInt();
    }
    return 0;
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
        m_compareSummaryLabel->setText(QStringLiteral("Select a snapshot to compare with the current scan."));
        m_compareTable->setRowCount(0);
        return;
    }

    const QString deltaText = summary.totalDeltaBytes >= 0
        ? QStringLiteral("+%1").arg(SizeFormatter::formatAdaptiveBytes(summary.totalDeltaBytes))
        : QStringLiteral("-%1").arg(SizeFormatter::formatAdaptiveBytes(std::llabs(summary.totalDeltaBytes)));
    const QString color = summary.totalDeltaBytes >= 0 ? QStringLiteral("#4cd964") : QStringLiteral("#ff6b6b");
    m_compareSummaryLabel->setText(
        QStringLiteral("<span style='font-weight:600;'>Compared snapshot:</span> %1<br>"
                       "<span style='font-weight:600;'>Size change:</span> <span style='color:%5;'>%2</span><br>"
                       "<span style='font-weight:600;'>Folders changed:</span> %3 | "
                       "<span style='font-weight:600;'>Files/events changed:</span> %4")
            .arg(summary.snapshotCreatedAt)
            .arg(deltaText)
            .arg(summary.changedFolderCount)
            .arg(summary.fileEventCount)
            .arg(color));

    QVector<SnapshotCompareRow> sortedRows = rows;
    std::sort(sortedRows.begin(), sortedRows.end(), [](const SnapshotCompareRow &left, const SnapshotCompareRow &right) {
        return std::abs(left.deltaBytes) > std::abs(right.deltaBytes);
    });
    const qsizetype rowCount = std::min<qsizetype>(12, sortedRows.size());
    m_compareTable->setRowCount(rowCount);
    for (int i = 0; i < rowCount; ++i) {
        const SnapshotCompareRow &row = sortedRows[i];
        const QString name = QFileInfo(row.path).fileName().isEmpty() ? row.path : QFileInfo(row.path).fileName();
        auto *nameItem = new QTableWidgetItem(name);
        nameItem->setToolTip(row.path);
        auto *deltaItem = new QTableWidgetItem(QStringLiteral("%1%2")
            .arg(row.deltaBytes >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
            .arg(SizeFormatter::formatAdaptiveBytes(std::llabs(row.deltaBytes))));
        deltaItem->setToolTip(row.path);
        if (row.deltaBytes > 0) {
            deltaItem->setForeground(QBrush(QColor(76, 217, 100)));
        } else if (row.deltaBytes < 0) {
            deltaItem->setForeground(QBrush(QColor(255, 107, 107)));
        }
        m_compareTable->setItem(i, 0, nameItem);
        m_compareTable->setItem(i, 1, deltaItem);
    }
}

}
