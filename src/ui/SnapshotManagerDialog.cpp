#include "ui/SnapshotManagerDialog.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "utils/SizeFormatter.h"

namespace opentree {

SnapshotManagerDialog::SnapshotManagerDialog(SnapshotService *snapshotService, QWidget *parent)
    : QDialog(parent)
    , m_snapshotService(snapshotService)
    , m_snapshotTable(new QTableWidget(this))
    , m_statusLabel(new QLabel(this))
    , m_deleteButton(new QPushButton(QStringLiteral("Delete Snapshot"), this))
{
    setWindowTitle(QStringLiteral("Snapshot Manager"));
    resize(720, 460);

    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel(QStringLiteral("Manage snapshots stored in the database."), this);
    title->setWordWrap(true);
    layout->addWidget(title);

    m_snapshotTable->setColumnCount(7);
    m_snapshotTable->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("Date"),
        QStringLiteral("Root"),
        QStringLiteral("Size"),
        QStringLiteral("Files"),
        QStringLiteral("Folders"),
        QStringLiteral("Events")
    });
    m_snapshotTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_snapshotTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_snapshotTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_snapshotTable->setAlternatingRowColors(true);
    m_snapshotTable->setSortingEnabled(true);
    m_snapshotTable->verticalHeader()->setVisible(false);
    m_snapshotTable->horizontalHeader()->setStretchLastSection(false);
    m_snapshotTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_snapshotTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_snapshotTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_snapshotTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_snapshotTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_snapshotTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_snapshotTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    layout->addWidget(m_snapshotTable, 1);
    layout->addWidget(m_statusLabel);

    auto *buttonsRow = new QHBoxLayout();
    buttonsRow->addStretch();
    buttonsRow->addWidget(m_deleteButton);
    auto *closeButton = new QPushButton(QStringLiteral("Close"), this);
    buttonsRow->addWidget(closeButton);
    layout->addLayout(buttonsRow);

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        const int snapshotId = selectedSnapshotId();
        if (snapshotId <= 0) {
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Delete Snapshot"), QStringLiteral("Delete this snapshot and its events/items?")) != QMessageBox::Yes) {
            return;
        }
        QString error;
        if (!m_snapshotService->deleteSnapshot(snapshotId, &error)) {
            QMessageBox::warning(this, QStringLiteral("Delete Snapshot"), error.isEmpty() ? QStringLiteral("Failed to delete snapshot.") : error);
            return;
        }
        refreshList();
    });

    refreshList();
}

int SnapshotManagerDialog::selectedSnapshotId() const
{
    const QModelIndexList rows = m_snapshotTable->selectionModel() ? m_snapshotTable->selectionModel()->selectedRows() : QModelIndexList();
    if (rows.isEmpty()) {
        return 0;
    }

    const QTableWidgetItem *item = m_snapshotTable->item(rows.first().row(), 0);
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

void SnapshotManagerDialog::refreshList()
{
    m_snapshotTable->setSortingEnabled(false);
    m_snapshotTable->setRowCount(0);
    QString error;
    const QVector<SnapshotSummary> snapshots = m_snapshotService->listSnapshots(&error);
    if (!error.isEmpty()) {
        m_statusLabel->setText(error);
        m_deleteButton->setEnabled(false);
        return;
    }

    for (int row = 0; row < snapshots.size(); ++row) {
        const SnapshotSummary &snapshot = snapshots[row];
        const QString createdAt = snapshot.createdAt.contains('T') ? QString(snapshot.createdAt).replace('T', ' ') : snapshot.createdAt;
        m_snapshotTable->insertRow(row);

        auto makeItem = [](const QString &text, const QVariant &userData = {}) {
            auto *item = new QTableWidgetItem(text);
            if (userData.isValid()) {
                item->setData(Qt::UserRole, userData);
            }
            return item;
        };

        auto *idItem = makeItem(QString::number(snapshot.id), snapshot.id);
        auto *dateItem = makeItem(createdAt);
        auto *rootItem = makeItem(snapshot.rootPath);
        auto *sizeItem = makeItem(SizeFormatter::formatBytes(snapshot.totalSize), snapshot.totalSize);
        auto *filesItem = makeItem(QString::number(snapshot.fileCount), snapshot.fileCount);
        auto *foldersItem = makeItem(QString::number(snapshot.itemCount), snapshot.itemCount);
        auto *eventsItem = makeItem(QString::number(snapshot.eventCount), snapshot.eventCount);

        rootItem->setToolTip(snapshot.rootPath);
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        filesItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        foldersItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        eventsItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_snapshotTable->setItem(row, 0, idItem);
        m_snapshotTable->setItem(row, 1, dateItem);
        m_snapshotTable->setItem(row, 2, rootItem);
        m_snapshotTable->setItem(row, 3, sizeItem);
        m_snapshotTable->setItem(row, 4, filesItem);
        m_snapshotTable->setItem(row, 5, foldersItem);
        m_snapshotTable->setItem(row, 6, eventsItem);
    }

    m_snapshotTable->setSortingEnabled(true);
    m_snapshotTable->sortByColumn(0, Qt::DescendingOrder);
    if (!snapshots.isEmpty()) {
        m_snapshotTable->selectRow(0);
    }

    m_statusLabel->setText(snapshots.isEmpty() ? QStringLiteral("No snapshots available.") : QStringLiteral("%1 snapshots").arg(snapshots.size()));
    m_deleteButton->setEnabled(!snapshots.isEmpty());
}

void SnapshotManagerDialog::accept()
{
    QDialog::accept();
}

}
