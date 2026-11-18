#include "ui/DatabaseMaintenanceDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "services/DatabaseMaintenanceService.h"
#include "utils/SizeFormatter.h"

namespace opentree {

DatabaseMaintenanceDialog::DatabaseMaintenanceDialog(DatabaseMaintenanceService *service, int retentionDays, QWidget *parent)
    : QDialog(parent)
    , m_service(service)
    , m_retentionDays(retentionDays)
    , m_summaryLabel(new QLabel(this))
    , m_statusLabel(new QLabel(this))
    , m_table(new QTableWidget(this))
    , m_vacuumButton(new QPushButton(QStringLiteral("VACUUM Database"), this))
    , m_pruneButton(new QPushButton(QStringLiteral("Prune Old File Events"), this))
{
    setWindowTitle(QStringLiteral("Database Maintenance"));
    resize(640, 480);

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(QStringLiteral(
        "The local database stores scans, snapshots and the change ledger. Deleting rows leaves "
        "free pages behind, so the file can stay large after old data is gone. VACUUM rebuilds "
        "the file to reclaim that space."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_summaryLabel);

    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Table"), QStringLiteral("Rows")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    auto *actions = new QHBoxLayout();
    actions->addWidget(m_vacuumButton);
    actions->addWidget(m_pruneButton);
    actions->addStretch();
    layout->addLayout(actions);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    m_vacuumButton->setToolTip(QStringLiteral("Rebuild the database file so freed pages are returned to disk."));
    m_pruneButton->setToolTip(m_retentionDays > 0
                                  ? QStringLiteral("Drop snapshot file events older than %1 days.").arg(m_retentionDays)
                                  : QStringLiteral("Retention is set to keep forever, so there is nothing to prune."));
    m_pruneButton->setEnabled(m_retentionDays > 0);

    connect(m_vacuumButton, &QPushButton::clicked, this, &DatabaseMaintenanceDialog::runVacuum);
    connect(m_pruneButton, &QPushButton::clicked, this, &DatabaseMaintenanceDialog::runPrune);

    refreshStats();
}

void DatabaseMaintenanceDialog::refreshStats()
{
    QString error;
    const DatabaseStats stats = m_service->collectStats(&error);
    if (!error.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Could not read database statistics: %1").arg(error));
        m_table->setRowCount(0);
        return;
    }

    m_summaryLabel->setText(QStringLiteral(
        "File: %1\nSize: %2  ·  %3 pages  ·  %4 free-list pages")
                                .arg(stats.databasePath,
                                     SizeFormatter::formatBytes(stats.fileSizeBytes),
                                     QString::number(stats.pageCount),
                                     QString::number(stats.freeListPageCount)));

    m_table->setRowCount(stats.tables.size());
    for (int row = 0; row < stats.tables.size(); ++row) {
        const DatabaseTableStats &table = stats.tables[row];
        auto *nameItem = new QTableWidgetItem(table.name);
        auto *rowsItem = new QTableWidgetItem(QString::number(table.rowCount));
        rowsItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, rowsItem);
    }

    m_statusLabel->setText(QStringLiteral("%1 tables").arg(stats.tables.size()));
}

void DatabaseMaintenanceDialog::runVacuum()
{
    m_vacuumButton->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("Running VACUUM..."));

    QString error;
    const DatabaseMaintenanceResult result = m_service->vacuum(&error);
    if (!result.ok) {
        m_statusLabel->setText(result.message.isEmpty() ? QStringLiteral("VACUUM failed.") : result.message);
    } else {
        m_statusLabel->setText(QStringLiteral("VACUUM complete — reclaimed %1.")
                                   .arg(SizeFormatter::formatBytes(result.reclaimedBytes)));
    }
    m_vacuumButton->setEnabled(true);
    refreshStats();
}

void DatabaseMaintenanceDialog::runPrune()
{
    if (m_retentionDays <= 0) {
        return;
    }

    QString error;
    const int pruned = m_service->pruneFileEvents(m_retentionDays, &error);
    if (!error.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Pruning failed: %1").arg(error));
        return;
    }
    m_statusLabel->setText(pruned == 0
                               ? QStringLiteral("Nothing to prune — no file events are older than %1 days.").arg(m_retentionDays)
                               : QStringLiteral("Pruned %1 file event(s) older than %2 days.").arg(pruned).arg(m_retentionDays));
    refreshStats();
}

} // namespace opentree
