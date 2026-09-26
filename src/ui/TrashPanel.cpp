#include "ui/TrashPanel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "services/VirtualTrashService.h"
#include "utils/SizeFormatter.h"

namespace opentree {

TrashPanel::TrashPanel(VirtualTrashService *trashService, QWidget *parent)
    : QWidget(parent)
    , m_trashService(trashService)
    , m_summaryLabel(new QLabel(this))
    , m_stageButton(new QPushButton(QStringLiteral("Stage current selection"), this))
    , m_unstageButton(new QPushButton(QStringLiteral("Remove from list"), this))
    , m_clearButton(new QPushButton(QStringLiteral("Clear list"), this))
    , m_recycleButton(new QPushButton(QStringLiteral("Move to Recycle Bin..."), this))
    , m_table(new QTableWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    m_summaryLabel->setWordWrap(true);

    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Item"), QStringLiteral("Size"),
                                        QStringLiteral("Kind"), QStringLiteral("Staged"),
                                        QStringLiteral("Reason")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 5; ++column) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    m_table->setColumnWidth(1, 110);
    m_table->setColumnWidth(2, 80);
    m_table->setColumnWidth(3, 150);
    m_table->setColumnWidth(4, 200);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);

    m_stageButton->setToolTip(QStringLiteral("Add the item selected in the tree to the staged list.\nThis only records the intent; nothing is deleted."));
    m_unstageButton->setToolTip(QStringLiteral("Remove the selected row from the staged list."));
    m_clearButton->setToolTip(QStringLiteral("Empty the staged list. Nothing on disk is affected."));
    m_recycleButton->setToolTip(QStringLiteral("Move every staged item to the Windows Recycle Bin. Asks for confirmation first."));
    m_recycleButton->setObjectName(QStringLiteral("destructiveButton"));

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addWidget(m_stageButton, 0);
    buttonRow->addWidget(m_unstageButton, 0);
    buttonRow->addWidget(m_clearButton, 0);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_recycleButton, 0);

    layout->addWidget(m_summaryLabel);
    layout->addLayout(buttonRow);
    layout->addWidget(m_table, 1);

    connect(m_stageButton, &QPushButton::clicked, this, &TrashPanel::stageSelection);
    connect(m_unstageButton, &QPushButton::clicked, this, &TrashPanel::unstageSelected);
    connect(m_clearButton, &QPushButton::clicked, this, &TrashPanel::clearStaged);
    connect(m_recycleButton, &QPushButton::clicked, this, &TrashPanel::moveStagedToRecycleBin);

    refresh();
}

void TrashPanel::setSelection(const TreeEntry &entry)
{
    m_selection = entry;
    m_stageButton->setEnabled(!entry.path.isEmpty());
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

    m_table->setRowCount(items.size());
    qint64 totalBytes = 0;
    for (int row = 0; row < items.size(); ++row) {
        const TrashItem &item = items[row];
        totalBytes += item.size;

        auto *pathItem = new QTableWidgetItem(item.path);
        pathItem->setData(Qt::UserRole, item.id);
        auto *sizeItem = new QTableWidgetItem(SizeFormatter::formatBytes(item.size));
        auto *kindItem = new QTableWidgetItem(item.isFolder ? QStringLiteral("Folder") : QStringLiteral("File"));
        auto *stagedItem = new QTableWidgetItem(item.stagedAt.isValid()
            ? item.stagedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
            : QStringLiteral("-"));
        auto *reasonItem = new QTableWidgetItem(item.reason);

        m_table->setItem(row, 0, pathItem);
        m_table->setItem(row, 1, sizeItem);
        m_table->setItem(row, 2, kindItem);
        m_table->setItem(row, 3, stagedItem);
        m_table->setItem(row, 4, reasonItem);
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
        : QStringLiteral("Trash: nothing staged. Select an item in the tree and press \"Stage current selection\"."));
}

void TrashPanel::stageSelection()
{
    if (!m_trashService || m_selection.path.isEmpty()) {
        return;
    }

    QString error;
    if (!m_trashService->stage(m_selection.path, m_selection.size,
                               m_selection.kind == TreeEntryKind::Folder,
                               m_selection.parentPath,
                               QStringLiteral("Staged from the tree"),
                               &error)) {
        QMessageBox::warning(this, QStringLiteral("Stage for deletion"), error);
        return;
    }

    refresh();
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

    qint64 totalBytes = 0;
    QStringList blocked;
    for (const TrashItem &item : items) {
        totalBytes += item.size;
        QString reason;
        if (!VirtualTrashService::canMoveToRecycleBin(item.path, &reason)) {
            blocked << QStringLiteral("%1 (%2)").arg(item.path, reason);
        }
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle(QStringLiteral("Move to Recycle Bin"));
    box.setText(QStringLiteral("Move %1 staged item(s) to the Windows Recycle Bin?").arg(items.size()));
    QString informative = QStringLiteral("This removes %1 from disk.\n\n"
                                         "The items go to the Recycle Bin, so they can still be restored from there, "
                                         "but OpenTree cannot undo this action.")
                              .arg(SizeFormatter::formatBytes(totalBytes));
    if (!blocked.isEmpty()) {
        informative += QStringLiteral("\n\n%1 item(s) will be skipped because they are not safe to remove:\n%2")
                           .arg(blocked.size())
                           .arg(blocked.mid(0, 8).join(QStringLiteral("\n")));
    }
    box.setInformativeText(informative);
    QPushButton *confirmButton = box.addButton(QStringLiteral("Move to Recycle Bin"), QMessageBox::DestructiveRole);
    QPushButton *cancelButton = box.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancelButton);
    box.exec();
    if (box.clickedButton() != confirmButton) {
        return;
    }

    int removed = 0;
    QStringList failures;
    for (const TrashItem &item : items) {
        QString moveError;
        if (!VirtualTrashService::moveToRecycleBin(item.path, &moveError)) {
            failures << moveError;
            continue;
        }
        m_trashService->unstage(item.id);
        ++removed;
    }

    refresh();

    if (failures.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Move to Recycle Bin"),
                                 QStringLiteral("Moved %1 item(s) to the Recycle Bin.").arg(removed));
    } else {
        QMessageBox::warning(this, QStringLiteral("Move to Recycle Bin"),
                             QStringLiteral("Moved %1 item(s). %2 failed:\n\n%3")
                                 .arg(removed)
                                 .arg(failures.size())
                                 .arg(failures.mid(0, 8).join(QStringLiteral("\n"))));
    }
}

}
