#include "ui/TrashPanel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "services/VirtualTrashService.h"
#include "ui/StagingReviewDialog.h"
#include "ui/TableItems.h"
#include "utils/SizeFormatter.h"

namespace opentree {

TrashPanel::TrashPanel(VirtualTrashService *trashService, QWidget *parent)
    : QWidget(parent)
    , m_trashService(trashService)
    , m_summaryLabel(new QLabel(this))
    , m_statusLabel(new QLabel(this))
    , m_unstageButton(new QPushButton(QStringLiteral("Remove from List"), this))
    , m_clearButton(new QPushButton(QStringLiteral("Clear List"), this))
    , m_recycleButton(new QPushButton(QStringLiteral("Move to Recycle Bin..."), this))
    , m_table(new QTableWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    m_summaryLabel->setWordWrap(true);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setVisible(false);

    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Item"), QStringLiteral("Size"),
                                        QStringLiteral("Kind"), QStringLiteral("Staged"),
                                        QStringLiteral("Reason")});
    configureStandardTable(m_table);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 5; ++column) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    m_table->setColumnWidth(1, 120);
    m_table->setColumnWidth(2, 90);
    m_table->setColumnWidth(3, 160);
    m_table->setColumnWidth(4, 200);
    m_table->sortItems(1, Qt::DescendingOrder); // largest first by default

    m_unstageButton->setToolTip(QStringLiteral("Remove the selected row from the staged list."));
    m_clearButton->setToolTip(QStringLiteral("Empty the staged list. Nothing on disk is affected."));
    m_recycleButton->setToolTip(QStringLiteral("Review the staged items, then move them to the Windows Recycle Bin."));
    m_recycleButton->setObjectName(QStringLiteral("destructiveButton"));

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addWidget(m_unstageButton, 0);
    buttonRow->addWidget(m_clearButton, 0);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_recycleButton, 0);

    layout->addWidget(m_summaryLabel);
    layout->addWidget(m_statusLabel);
    layout->addLayout(buttonRow);
    layout->addWidget(m_table, 1);

    connect(m_unstageButton, &QPushButton::clicked, this, &TrashPanel::unstageSelected);
    connect(m_clearButton, &QPushButton::clicked, this, &TrashPanel::clearStaged);
    connect(m_recycleButton, &QPushButton::clicked, this, &TrashPanel::moveStagedToRecycleBin);

    refresh();
}

void TrashPanel::refresh()
{
    if (!m_trashService) {
        m_summaryLabel->setText(QStringLiteral("Trash: database unavailable."));
        m_table->setRowCount(0);
        return;
    }

    QString error;
    const QVector<TrashItem> items = m_trashService->stagedItems(&error);
    if (!error.isEmpty()) {
        m_summaryLabel->setText(QStringLiteral("Trash: %1").arg(error));
        m_table->setRowCount(0);
        return;
    }

    qint64 totalBytes = 0;
    {
        TableSortGuard guard(m_table);
        m_table->setRowCount(items.size());
        for (int row = 0; row < items.size(); ++row) {
            const TrashItem &item = items[row];
            totalBytes += item.size;

            auto *pathItem = makeTextItem(item.path, item.path);
            pathItem->setData(Qt::UserRole, item.id);
            m_table->setItem(row, 0, pathItem);
            m_table->setItem(row, 1, makeNumberItem(SizeFormatter::formatBytes(item.size), item.size));
            m_table->setItem(row, 2, makeTextItem(item.isFolder ? QStringLiteral("Folder") : QStringLiteral("File")));
            m_table->setItem(row, 3, makeTextItem(item.stagedAt.isValid()
                                                      ? item.stagedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                                      : QStringLiteral("-")));
            m_table->setItem(row, 4, makeTextItem(item.reason));
        }
    }

    const bool hasItems = !items.isEmpty();
    m_unstageButton->setEnabled(hasItems);
    m_clearButton->setEnabled(hasItems);
    m_recycleButton->setEnabled(hasItems);

    m_summaryLabel->setText(hasItems
        ? QStringLiteral("Trash: %1 staged item%2 | projected reclaim %3 | nothing is deleted until you confirm below")
              .arg(items.size())
              .arg(items.size() == 1 ? QString() : QStringLiteral("s"))
              .arg(SizeFormatter::formatBytes(totalBytes))
        : QStringLiteral("Trash: nothing staged. Right-click a file or folder anywhere in OpenTree and choose "
                         "\"Stage for Deletion\" to add it here."));
}

void TrashPanel::unstageSelected()
{
    if (!m_trashService) {
        return;
    }

    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0)) {
        QMessageBox::information(this, QStringLiteral("Remove from list"), QStringLiteral("Select a staged item first."));
        return;
    }

    QString error;
    if (!m_trashService->unstage(m_table->item(row, 0)->data(Qt::UserRole).toLongLong(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Remove from list"), error);
        return;
    }

    refresh();
}

void TrashPanel::clearStaged()
{
    if (!m_trashService || m_table->rowCount() == 0) {
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("Clear staged list"),
        QStringLiteral("Remove all %1 staged item(s) from the list?\n\nNothing on disk is deleted.").arg(m_table->rowCount()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!m_trashService->clearStaged(&error)) {
        QMessageBox::warning(this, QStringLiteral("Clear staged list"), error);
        return;
    }

    m_statusLabel->setVisible(false);
    refresh();
}

void TrashPanel::moveStagedToRecycleBin()
{
    if (!m_trashService || m_table->rowCount() == 0) {
        return;
    }

    QString error;
    const QVector<TrashItem> items = m_trashService->stagedItems(&error);
    if (!error.isEmpty() || items.isEmpty()) {
        return;
    }

    QVector<StageRequest> candidates;
    candidates.reserve(items.size());
    QStringList skipped;
    for (const TrashItem &item : items) {
        QString reason;
        if (!VirtualTrashService::canMoveToRecycleBin(item.path, &reason)) {
            skipped << QStringLiteral("%1 (%2)").arg(item.path, reason);
            continue;
        }
        StageRequest candidate;
        candidate.path = item.path;
        candidate.size = item.size;
        candidate.isFolder = item.isFolder;
        candidates.push_back(candidate);
    }

    if (candidates.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Move to Recycle Bin"),
                             QStringLiteral("None of the staged items can be moved to the Recycle Bin:\n\n%1")
                                 .arg(skipped.mid(0, 8).join(QStringLiteral("\n"))));
        return;
    }

    // One review window listing exactly what leaves the disk, then one confirmation.
    StagingReviewDialog dialog(StagingReviewDialog::Action::MoveToRecycleBin, candidates, skipped, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    int removed = 0;
    qint64 removedBytes = 0;
    QStringList failures;
    for (const StageRequest &candidate : candidates) {
        QString moveError;
        if (!VirtualTrashService::moveToRecycleBin(candidate.path, &moveError)) {
            failures << moveError;
            continue;
        }
        for (const TrashItem &item : items) {
            if (item.path.compare(candidate.path, Qt::CaseInsensitive) == 0) {
                m_trashService->unstage(item.id);
                break;
            }
        }
        ++removed;
        removedBytes += candidate.size;
    }

    refresh();

    QString status = QStringLiteral("Moved %1 (%2) to the Recycle Bin.")
                         .arg(removed == 1 ? QStringLiteral("1 item") : QStringLiteral("%1 items").arg(removed),
                              SizeFormatter::formatBytes(removedBytes));
    if (!failures.isEmpty()) {
        status += QStringLiteral(" %1 failed: %2")
                      .arg(failures.size())
                      .arg(failures.mid(0, 4).join(QStringLiteral("; ")));
    }
    m_statusLabel->setText(status);
    m_statusLabel->setVisible(true);
}

} // namespace opentree
