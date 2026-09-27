#include "ui/StagingReviewDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

#include "ui/TableItems.h"
#include "utils/SizeFormatter.h"

namespace opentree {

namespace {

QString itemCountText(int count)
{
    return count == 1 ? QStringLiteral("1 item") : QStringLiteral("%1 items").arg(count);
}

} // namespace

StagingReviewDialog::StagingReviewDialog(Action action, const QVector<StageRequest> &items,
                                         const QStringList &skipped, QWidget *parent)
    : QDialog(parent)
{
    const bool destructive = action == Action::MoveToRecycleBin;
    setWindowTitle(destructive ? QStringLiteral("Move to Recycle Bin") : QStringLiteral("Stage in Trash"));
    resize(720, 460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto *headline = new QLabel(this);
    headline->setWordWrap(true);
    headline->setText(destructive
        ? QStringLiteral("These %1 will be moved to the Windows Recycle Bin:").arg(itemCountText(items.size()))
        : QStringLiteral("These %1 will be added to the virtual trash list:").arg(itemCountText(items.size())));
    layout->addWidget(headline);

    m_itemCount = items.size();
    for (const StageRequest &candidate : items) {
        m_totalBytes += candidate.size;
    }

    auto *table = new QTableWidget(this);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({QStringLiteral("Item"), QStringLiteral("Size"), QStringLiteral("Kind")});
    configureStandardTable(table);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    table->setColumnWidth(1, 110);
    table->setColumnWidth(2, 90);
    table->setRowCount(items.size());
    for (int row = 0; row < items.size(); ++row) {
        const StageRequest &candidate = items[row];
        table->setItem(row, 0, makeTextItem(candidate.path, candidate.path));
        table->setItem(row, 1, makeNumberItem(SizeFormatter::formatBytes(candidate.size), candidate.size));
        table->setItem(row, 2, makeTextItem(candidate.isFolder ? QStringLiteral("Folder") : QStringLiteral("File")));
    }
    layout->addWidget(table, 1);

    auto *summary = new QLabel(this);
    summary->setWordWrap(true);
    summary->setText(QStringLiteral("Total: %1 across %2.")
                         .arg(SizeFormatter::formatBytes(m_totalBytes), itemCountText(m_itemCount)));
    layout->addWidget(summary);

    if (!skipped.isEmpty()) {
        auto *skippedLabel = new QLabel(this);
        skippedLabel->setWordWrap(true);
        skippedLabel->setText(QStringLiteral("%1 will be skipped because they are not safe to remove:\n%2")
                                  .arg(itemCountText(skipped.size()))
                                  .arg(skipped.mid(0, 6).join(QStringLiteral("\n"))));
        layout->addWidget(skippedLabel);
    }

    auto *note = new QLabel(this);
    note->setWordWrap(true);
    note->setText(destructive
        ? QStringLiteral("This removes the items from disk. They go to the Recycle Bin and can still be "
                         "restored from there, but OpenTree cannot undo this action.")
        : QStringLiteral("Nothing is deleted here. Staged items can be reviewed in the Trash tab, then "
                         "moved to the Recycle Bin or removed from the list."));
    layout->addWidget(note);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *cancelButton = buttons->addButton(QStringLiteral("Cancel"), QDialogButtonBox::RejectRole);
    QPushButton *confirmButton = buttons->addButton(
        destructive ? QStringLiteral("Move %1 to Recycle Bin").arg(itemCountText(m_itemCount))
                    : QStringLiteral("Stage %1").arg(itemCountText(m_itemCount)),
        QDialogButtonBox::AcceptRole);
    if (destructive) {
        confirmButton->setObjectName(QStringLiteral("destructiveButton"));
        // The object name is set after the widget exists, so re-polish it to pick up the
        // destructive style rule.
        confirmButton->style()->unpolish(confirmButton);
        confirmButton->style()->polish(confirmButton);
    }
    // Safest default: Enter cancels instead of confirming.
    cancelButton->setDefault(true);
    cancelButton->setAutoDefault(true);
    confirmButton->setAutoDefault(false);
    confirmButton->setDefault(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace opentree
