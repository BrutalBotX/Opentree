#include "services/DatabaseMaintenanceService.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

#include "utils/Logger.h"

namespace opentree {

namespace {

qint64 fileSize(const QString &path)
{
    QFileInfo info(path);
    return info.exists() ? info.size() : 0;
}

} // namespace

DatabaseMaintenanceService::DatabaseMaintenanceService(const QSqlDatabase &database, const QString &databasePath)
    : m_database(database)
    , m_databasePath(databasePath)
{
}

DatabaseStats DatabaseMaintenanceService::collectStats(QString *errorMessage) const
{
    DatabaseStats stats;
    stats.databasePath = m_databasePath;
    stats.fileSizeBytes = fileSize(m_databasePath);

    // PRAGMA page_count / freelist_count reflect the SQLite pages, including the -wal
    // file only after a checkpoint, so they are a lower bound. The reclaimable estimate
    // shown to the user is derived from the on-disk size before/after VACUUM instead.
    QSqlQuery pageQuery(m_database);
    if (pageQuery.exec(QStringLiteral("PRAGMA page_count")) && pageQuery.next()) {
        stats.pageCount = pageQuery.value(0).toLongLong();
    }
    QSqlQuery freeQuery(m_database);
    if (freeQuery.exec(QStringLiteral("PRAGMA freelist_count")) && freeQuery.next()) {
        stats.freeListPageCount = freeQuery.value(0).toLongLong();
    }

    QSqlQuery tablesQuery(m_database);
    tablesQuery.exec(QStringLiteral(
        "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY name"));
    while (tablesQuery.next()) {
        const QString name = tablesQuery.value(0).toString();
        DatabaseTableStats tableStats;
        tableStats.name = name;

        QSqlQuery countQuery(m_database);
        if (countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM \"%1\"").arg(name)) && countQuery.next()) {
            tableStats.rowCount = countQuery.value(0).toLongLong();
        }
        stats.tables.append(tableStats);
    }

    return stats;
}

DatabaseMaintenanceResult DatabaseMaintenanceService::vacuum(QString *errorMessage)
{
    DatabaseMaintenanceResult result;

    if (!m_database.isOpen()) {
        result.message = QStringLiteral("The database connection is not open.");
        if (errorMessage) {
            *errorMessage = result.message;
        }
        return result;
    }

    const qint64 beforeBytes = fileSize(m_databasePath);

    // VACUUM must run on the connection that holds the file open (a second connection would
    // just see "database is locked"), and SQLite refuses it while any statement or
    // transaction is in progress. The maintenance dialog is modal, so the only statements
    // that could be active belong to this connection; roll back any open transaction and
    // finish stray queries before folding the WAL and rebuilding the file.
    if (m_database.transaction()) {
        m_database.rollback();
    }

    bool ok = false;
    QString detail;
    {
        QSqlQuery checkpointQuery(m_database);
        checkpointQuery.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
    }
    {
        QSqlQuery vacuumQuery(m_database);
        ok = vacuumQuery.exec(QStringLiteral("VACUUM"));
        detail = vacuumQuery.lastError().text();
    }

    if (!ok) {
        result.message = QStringLiteral("VACUUM failed: %1").arg(detail);
        if (errorMessage) {
            *errorMessage = detail;
        }
        Logger::error(result.message);
        return result;
    }

    const qint64 afterBytes = fileSize(m_databasePath);
    result.ok = true;
    result.reclaimedBytes = beforeBytes - afterBytes;
    Logger::info(QStringLiteral("Database VACUUM reclaimed %1 bytes (%2 -> %3)")
                     .arg(result.reclaimedBytes)
                     .arg(beforeBytes)
                     .arg(afterBytes));
    return result;
}

int DatabaseMaintenanceService::pruneFileEvents(int retentionDays, QString *errorMessage)
{
    if (retentionDays <= 0) {
        return 0;
    }

    const QString cutoff = QDateTime::currentDateTime().addDays(-retentionDays).toString(Qt::ISODate);

    QSqlQuery countQuery(m_database);
    countQuery.prepare(
        "SELECT COUNT(*) FROM snapshot_file_events WHERE snapshot_id IN (SELECT id FROM snapshots WHERE created_at < ?)");
    countQuery.addBindValue(cutoff);
    if (!countQuery.exec() || !countQuery.next()) {
        if (errorMessage) {
            *errorMessage = countQuery.lastError().text();
        }
        return 0;
    }
    const int count = countQuery.value(0).toInt();
    if (count == 0) {
        return 0;
    }

    QSqlQuery deleteQuery(m_database);
    deleteQuery.prepare(
        "DELETE FROM snapshot_file_events WHERE snapshot_id IN (SELECT id FROM snapshots WHERE created_at < ?)");
    deleteQuery.addBindValue(cutoff);
    if (!deleteQuery.exec()) {
        if (errorMessage) {
            *errorMessage = deleteQuery.lastError().text();
        }
        return 0;
    }

    Logger::info(QStringLiteral("Pruned %1 snapshot file events older than %2 days").arg(count).arg(retentionDays));
    return count;
}

} // namespace opentree
