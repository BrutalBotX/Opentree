#pragma once

#include <QDialog>

#include "services/SnapshotService.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QTableWidget)

namespace opentree {

class SnapshotManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit SnapshotManagerDialog(SnapshotService *snapshotService, int retentionDays, QWidget *parent = nullptr);

signals:
    // Emitted after a snapshot is deleted or pruned so the timeline can refresh.
    void snapshotsChanged();

private slots:
    void accept() override;

private:
    void refreshList();
    void pruneOldSnapshots();
    int selectedSnapshotId() const;

    SnapshotService *m_snapshotService;
    int m_retentionDays;
    QTableWidget *m_snapshotTable;
    QLabel *m_statusLabel;
    QPushButton *m_deleteButton;
    QPushButton *m_pruneButton;
};

}
