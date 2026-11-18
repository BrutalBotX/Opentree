#pragma once

#include <QDialog>

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QTableWidget)

namespace opentree {

class DatabaseMaintenanceService;

// Database Maintenance: shows the size and per-table row counts of the local database,
// and offers VACUUM (rebuild the file to reclaim freed pages) and retention pruning
// (drop snapshot file events older than the configured window).
class DatabaseMaintenanceDialog : public QDialog {
    Q_OBJECT

public:
    explicit DatabaseMaintenanceDialog(DatabaseMaintenanceService *service, int retentionDays, QWidget *parent = nullptr);

private:
    void refreshStats();
    void runVacuum();
    void runPrune();

    DatabaseMaintenanceService *m_service;
    int m_retentionDays;
    QLabel *m_summaryLabel;
    QLabel *m_statusLabel;
    QTableWidget *m_table;
    QPushButton *m_vacuumButton;
    QPushButton *m_pruneButton;
};

} // namespace opentree
