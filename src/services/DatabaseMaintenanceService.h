#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVector>

namespace opentree {

struct DatabaseTableStats {
    QString name;
    qint64 rowCount = 0;
};

struct DatabaseStats {
    QString databasePath;
    qint64 fileSizeBytes = 0;
    qint64 pageCount = 0;
    qint64 freeListPageCount = 0;
    QVector<DatabaseTableStats> tables;
};

struct DatabaseMaintenanceResult {
    bool ok = false;
    qint64 reclaimedBytes = 0;
    QString message;
};

// Read-only inspection plus the two maintenance operations (VACUUM and retention
// pruning) for the local SQLite database. VACUUM rebuilds the file to drop the free
// pages left behind by deleted rows; pruning drops snapshot file events older than the
// configured retention window.
class DatabaseMaintenanceService {
public:
    DatabaseMaintenanceService(const QSqlDatabase &database, const QString &databasePath);

    DatabaseStats collectStats(QString *errorMessage = nullptr) const;
    DatabaseMaintenanceResult vacuum(QString *errorMessage = nullptr);
    // Drops snapshot_file_events whose snapshot is older than `retentionDays`. A value
    // of 0 means "keep forever" and prunes nothing. Returns the number of rows removed.
    int pruneFileEvents(int retentionDays, QString *errorMessage = nullptr);

private:
    QSqlDatabase m_database;
    QString m_databasePath;
};

} // namespace opentree
