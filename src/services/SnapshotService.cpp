#include "services/SnapshotService.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QStorageInfo>

#include <algorithm>

#include "utils/PathUtils.h"

namespace opentree {

QHash<QString, qint64> SnapshotService::loadFolderState(const QString &rootPath, int upToSnapshotId, QString *errorMessage) const
{
    QHash<QString, qint64> state;
    QSqlQuery query(m_database);
    query.prepare(
        "SELECT si.path, si.size "
        "FROM snapshot_items si "
        "JOIN snapshots s ON s.id = si.snapshot_id "
        "WHERE s.root_path = ? AND s.id <= ? AND si.kind = 'folder' "
        "ORDER BY s.id ASC, si.id ASC");
    query.addBindValue(rootPath);
    query.addBindValue(upToSnapshotId);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return {};
    }

    while (query.next()) {
        state.insert(query.value(0).toString(), query.value(1).toLongLong());
    }

    return state;
}

SnapshotService::SnapshotService(const QSqlDatabase &database)
    : m_database(database)
{
}

SnapshotCreateResult SnapshotService::createSnapshot(const ScanResult &result, qint64 thresholdBytes, QString *errorMessage)
{
    SnapshotCreateResult createResult;

    int previousSnapshotId = 0;
    QSqlQuery previousSnapshotQuery(m_database);
    previousSnapshotQuery.prepare("SELECT MAX(id) FROM snapshots WHERE root_path = ?");
    previousSnapshotQuery.addBindValue(result.rootPath);
    if (!previousSnapshotQuery.exec() || !previousSnapshotQuery.next()) {
        if (errorMessage) {
            *errorMessage = previousSnapshotQuery.lastError().text();
        }
        return createResult;
    }
    previousSnapshotId = previousSnapshotQuery.value(0).toInt();

    const bool hasPreviousSnapshot = previousSnapshotId > 0;
    const QHash<QString, qint64> previousSizes = hasPreviousSnapshot
        ? loadFolderState(result.rootPath, previousSnapshotId, errorMessage)
        : QHash<QString, qint64> {};
    if (errorMessage && !errorMessage->isEmpty()) {
        return createResult;
    }

    QHash<QString, TreeEntry> currentFolders;
    for (const TreeEntry &entry : result.treeEntries) {
        if (entry.kind == TreeEntryKind::Folder) {
            currentFolders.insert(entry.path, entry);
        }
    }

    QHash<QString, FileEntry> previousFiles;
    QSqlQuery previousFilesQuery(m_database);
    previousFilesQuery.prepare("SELECT path, parent_path, name, size FROM snapshot_current_files WHERE root_path = ?");
    previousFilesQuery.addBindValue(result.rootPath);
    if (!previousFilesQuery.exec()) {
        if (errorMessage) {
            *errorMessage = previousFilesQuery.lastError().text();
        }
        return createResult;
    }
    while (previousFilesQuery.next()) {
        FileEntry file;
        file.path = previousFilesQuery.value(0).toString();
        file.parentPath = previousFilesQuery.value(1).toString();
        file.name = previousFilesQuery.value(2).toString();
        file.size = previousFilesQuery.value(3).toLongLong();
        previousFiles.insert(file.path, file);
    }

    QHash<QString, FileEntry> currentFiles;
    for (const FileEntry &file : result.files) {
        currentFiles.insert(file.path, file);
    }

    QVector<TreeEntry> changedEntries;
    if (!hasPreviousSnapshot) {
        for (auto it = currentFolders.cbegin(); it != currentFolders.cend(); ++it) {
            changedEntries.push_back(it.value());
            createResult.maxDeltaBytes = std::max(createResult.maxDeltaBytes, std::llabs(it.value().size));
        }
    } else {
        QSet<QString> allPaths;
        for (auto it = previousSizes.cbegin(); it != previousSizes.cend(); ++it) {
            allPaths.insert(it.key());
        }
        for (auto it = currentFolders.cbegin(); it != currentFolders.cend(); ++it) {
            allPaths.insert(it.key());
        }

        for (const QString &path : allPaths) {
            const qint64 previousSize = previousSizes.value(path, 0);
            const TreeEntry currentEntry = currentFolders.value(path);
            const qint64 currentSize = currentEntry.path.isEmpty() ? 0 : currentEntry.size;
            const qint64 delta = std::llabs(currentSize - previousSize);
            createResult.maxDeltaBytes = std::max(createResult.maxDeltaBytes, delta);
            if (delta < thresholdBytes) {
                continue;
            }

            TreeEntry entry = currentEntry;
            if (entry.path.isEmpty()) {
                entry.kind = TreeEntryKind::Folder;
                entry.path = path;
                entry.parentPath = path.compare(result.rootPath, Qt::CaseInsensitive) == 0 ? QString() : PathUtils::parentPath(path);
                entry.name = PathUtils::fileName(path);
                if (entry.name.isEmpty()) {
                    entry.name = path;
                }
                entry.size = 0;
                entry.parentSize = 0;
                entry.fileCount = 0;
                entry.folderCount = 0;
            }
            changedEntries.push_back(entry);
        }
    }

    createResult.changedItemCount = changedEntries.size();
    if (changedEntries.isEmpty()) {
        createResult.message = QStringLiteral("No snapshot created. Threshold not exceeded.");
        return createResult;
    }

    if (!m_database.transaction()) {
        if (errorMessage) {
            *errorMessage = m_database.lastError().text();
        }
        return createResult;
    }

    qint64 totalSize = 0;
    int fileCount = 0;
    for (const TreeEntry &entry : result.treeEntries) {
        if (entry.kind == TreeEntryKind::File) {
            totalSize += entry.size;
            ++fileCount;
        }
    }

    QSqlQuery snapshotQuery(m_database);
    snapshotQuery.prepare("INSERT INTO snapshots(created_at, root_path, total_size, file_count) VALUES(?, ?, ?, ?)");
    snapshotQuery.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    snapshotQuery.addBindValue(result.rootPath);
    snapshotQuery.addBindValue(totalSize);
    snapshotQuery.addBindValue(fileCount);
    if (!snapshotQuery.exec()) {
        if (errorMessage) {
            *errorMessage = snapshotQuery.lastError().text();
        }
        m_database.rollback();
        return createResult;
    }

    const int snapshotId = snapshotQuery.lastInsertId().toInt();

    // Merkle-style structural ledger: register the folder paths once and store one small row
    // per folder whose recorded values changed, so unchanged trees cost nothing extra.
    writeLedgerRows(snapshotId, result, {}, errorMessage);

    QSqlQuery itemQuery(m_database);
    itemQuery.prepare(
        "INSERT INTO snapshot_items(snapshot_id, kind, path, parent_path, name, size, parent_size, file_count, folder_count) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)");

    for (const TreeEntry &entry : changedEntries) {
        itemQuery.addBindValue(snapshotId);
        itemQuery.addBindValue(entry.kind == TreeEntryKind::Folder ? "folder" : "file");
        itemQuery.addBindValue(entry.path);
        itemQuery.addBindValue(entry.parentPath);
        itemQuery.addBindValue(entry.name);
        itemQuery.addBindValue(entry.size);
        itemQuery.addBindValue(entry.parentSize);
        itemQuery.addBindValue(entry.fileCount);
        itemQuery.addBindValue(entry.folderCount);
        if (!itemQuery.exec()) {
            if (errorMessage) {
                *errorMessage = itemQuery.lastError().text();
            }
            m_database.rollback();
            return createResult;
        }
    }

    QSqlQuery deleteStateQuery(m_database);
    deleteStateQuery.prepare("DELETE FROM snapshot_current_files WHERE root_path = ?");
    deleteStateQuery.addBindValue(result.rootPath);
    if (!deleteStateQuery.exec()) {
        if (errorMessage) {
            *errorMessage = deleteStateQuery.lastError().text();
        }
        m_database.rollback();
        return createResult;
    }

    QSqlQuery stateInsertQuery(m_database);
    stateInsertQuery.prepare(
        "INSERT INTO snapshot_current_files(root_path, path, parent_path, name, size) VALUES(?, ?, ?, ?, ?)");
    for (const FileEntry &file : result.files) {
        stateInsertQuery.addBindValue(result.rootPath);
        stateInsertQuery.addBindValue(file.path);
        stateInsertQuery.addBindValue(file.parentPath);
        stateInsertQuery.addBindValue(file.name);
        stateInsertQuery.addBindValue(file.size);
        if (!stateInsertQuery.exec()) {
            if (errorMessage) {
                *errorMessage = stateInsertQuery.lastError().text();
            }
            m_database.rollback();
            return createResult;
        }
    }

    QSqlQuery eventInsertQuery(m_database);
    eventInsertQuery.prepare(
        "INSERT INTO snapshot_file_events(snapshot_id, event_type, path, parent_path, name, old_size, new_size) VALUES(?, ?, ?, ?, ?, ?, ?)");

    QSet<QString> allFilePaths;
    for (auto it = previousFiles.cbegin(); it != previousFiles.cend(); ++it) {
        allFilePaths.insert(it.key());
    }
    for (auto it = currentFiles.cbegin(); it != currentFiles.cend(); ++it) {
        allFilePaths.insert(it.key());
    }

    for (const QString &path : allFilePaths) {
        const bool hadPrevious = previousFiles.contains(path);
        const bool hasCurrent = currentFiles.contains(path);
        QString eventType;
        qint64 oldSize = 0;
        qint64 newSize = 0;
        QString parentPath;
        QString name;

        if (!hadPrevious && hasCurrent) {
            eventType = "ADD";
            newSize = currentFiles.value(path).size;
            parentPath = currentFiles.value(path).parentPath;
            name = currentFiles.value(path).name;
        } else if (hadPrevious && !hasCurrent) {
            eventType = "DELETE";
            oldSize = previousFiles.value(path).size;
            parentPath = previousFiles.value(path).parentPath;
            name = previousFiles.value(path).name;
        } else if (hadPrevious && hasCurrent && previousFiles.value(path).size != currentFiles.value(path).size) {
            eventType = "MODIFY";
            oldSize = previousFiles.value(path).size;
            newSize = currentFiles.value(path).size;
            parentPath = currentFiles.value(path).parentPath;
            name = currentFiles.value(path).name;
        }

        if (eventType.isEmpty()) {
            continue;
        }

        // Three-tier routing: only high-resolution paths keep a file changelog. Macro paths
        // are tracked in the ledger by folder size, blacklisted paths are skipped entirely.
        if (tierForPath(path) != ResolutionTier::HighResolution) {
            continue;
        }

        eventInsertQuery.addBindValue(snapshotId);
        eventInsertQuery.addBindValue(eventType);
        eventInsertQuery.addBindValue(path);
        eventInsertQuery.addBindValue(parentPath);
        eventInsertQuery.addBindValue(name);
        eventInsertQuery.addBindValue(oldSize);
        eventInsertQuery.addBindValue(newSize);
        if (!eventInsertQuery.exec()) {
            if (errorMessage) {
                *errorMessage = eventInsertQuery.lastError().text();
            }
            m_database.rollback();
            return createResult;
        }
    }

    if (!m_database.commit()) {
        if (errorMessage) {
            *errorMessage = m_database.lastError().text();
        }
        return createResult;
    }

    createResult.created = true;
    createResult.message = hasPreviousSnapshot
        ? QStringLiteral("Snapshot created with %1 changed folders.").arg(changedEntries.size())
        : QStringLiteral("Baseline snapshot created with %1 folders.").arg(changedEntries.size());
    return createResult;
}

QVector<SnapshotSummary> SnapshotService::listSnapshots(QString *errorMessage) const
{
    QVector<SnapshotSummary> snapshots;
    QSqlQuery query(m_database);
    if (!query.exec(
            "SELECT s.id, s.created_at, s.root_path, s.total_size, s.file_count, COUNT(DISTINCT si.id), COUNT(DISTINCT sfe.id) "
            "FROM snapshots s LEFT JOIN snapshot_items si ON si.snapshot_id = s.id "
            "LEFT JOIN snapshot_file_events sfe ON sfe.snapshot_id = s.id "
            "GROUP BY s.id, s.created_at, s.root_path, s.total_size, s.file_count "
            "ORDER BY s.id DESC")) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return snapshots;
    }

    while (query.next()) {
        SnapshotSummary snapshot;
        snapshot.id = query.value(0).toInt();
        snapshot.createdAt = query.value(1).toString();
        snapshot.rootPath = query.value(2).toString();
        snapshot.totalSize = query.value(3).toLongLong();
        snapshot.fileCount = query.value(4).toInt();
        snapshot.itemCount = query.value(5).toInt();
        snapshot.eventCount = query.value(6).toInt();
        snapshots.push_back(snapshot);
    }

    return snapshots;
}

bool SnapshotService::deleteSnapshot(int snapshotId, QString *errorMessage)
{
    if (snapshotId <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid snapshot id.");
        }
        return false;
    }

    if (!m_database.transaction()) {
        if (errorMessage) {
            *errorMessage = m_database.lastError().text();
        }
        return false;
    }

    auto execDelete = [&](const QString &sql) -> bool {
        QSqlQuery query(m_database);
        query.prepare(sql);
        query.addBindValue(snapshotId);
        if (!query.exec()) {
            if (errorMessage) {
                *errorMessage = query.lastError().text();
            }
            m_database.rollback();
            return false;
        }
        return true;
    };

    if (!execDelete("DELETE FROM snapshot_file_events WHERE snapshot_id = ?")) {
        return false;
    }
    if (!execDelete("DELETE FROM snapshot_items WHERE snapshot_id = ?")) {
        return false;
    }
    if (!execDelete("DELETE FROM snapshots WHERE id = ?")) {
        return false;
    }

    if (!m_database.commit()) {
        if (errorMessage) {
            *errorMessage = m_database.lastError().text();
        }
        return false;
    }

    return true;
}

SnapshotCompareResult SnapshotService::compareSnapshotToCurrent(int snapshotId, const ScanResult &current, QString *errorMessage) const
{
    SnapshotCompareResult result;

    const QVector<SnapshotCompareRow> rows = compareSnapshotRows(snapshotId, current, errorMessage);
    if (errorMessage && !errorMessage->isEmpty()) {
        return result;
    }
    if (rows.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Nothing to compare.");
        }
        return result;
    }

    QSqlQuery snapshotMeta(m_database);
    snapshotMeta.prepare("SELECT created_at FROM snapshots WHERE id = ?");
    snapshotMeta.addBindValue(snapshotId);
    if (snapshotMeta.exec() && snapshotMeta.next()) {
        result.snapshotCreatedAt = snapshotMeta.value(0).toString();
    }

    result.found = true;
    result.changedFolderCount = rows.size();
    result.fileEventCount = snapshotFileEvents(snapshotId, errorMessage).size();
    for (const SnapshotCompareRow &row : rows) {
        result.totalDeltaBytes += row.deltaBytes;
        if (std::llabs(row.deltaBytes) > std::llabs(result.largestChangeBytes)) {
            result.largestChangeBytes = row.deltaBytes;
            result.largestChangePath = row.path;
        }
        if (row.deltaBytes > result.largestGrowthBytes) {
            result.largestGrowthBytes = row.deltaBytes;
            result.largestGrowthPath = row.path;
        }
        if (row.deltaBytes < result.largestShrinkBytes) {
            result.largestShrinkBytes = row.deltaBytes;
            result.largestShrinkPath = row.path;
        }
    }

    return result;
}

QVector<SnapshotFileEvent> SnapshotService::snapshotFileEvents(int snapshotId, QString *errorMessage) const
{
    QVector<SnapshotFileEvent> events;
    QSqlQuery query(m_database);
    query.prepare(
        "SELECT event_type, path, old_size, new_size FROM snapshot_file_events WHERE snapshot_id = ? ORDER BY id ASC");
    query.addBindValue(snapshotId);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return events;
    }

    while (query.next()) {
        SnapshotFileEvent event;
        event.eventType = query.value(0).toString();
        event.path = query.value(1).toString();
        event.oldSize = query.value(2).toLongLong();
        event.newSize = query.value(3).toLongLong();
        events.push_back(event);
    }

    return events;
}

int SnapshotService::compactFileEvents(int retentionDays, QString *errorMessage)
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

    return count;
}

bool SnapshotService::recordBackgroundRun(const BackgroundRunSummary &summary, QString *errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(
        "INSERT INTO snapshot_background_runs(started_at, finished_at, status, roots_processed, snapshots_created, events_compacted, message) "
        "VALUES(?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(summary.startedAt);
    query.addBindValue(summary.finishedAt);
    query.addBindValue(summary.status);
    query.addBindValue(summary.rootsProcessed);
    query.addBindValue(summary.snapshotsCreated);
    query.addBindValue(summary.eventsCompacted);
    query.addBindValue(summary.message);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

BackgroundRunSummary SnapshotService::latestBackgroundRun(QString *errorMessage) const
{
    BackgroundRunSummary summary;
    QSqlQuery query(m_database);
    if (!query.exec(
            "SELECT started_at, finished_at, status, roots_processed, snapshots_created, events_compacted, message "
            "FROM snapshot_background_runs ORDER BY id DESC LIMIT 1")) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return summary;
    }
    if (!query.next()) {
        return summary;
    }

    summary.startedAt = query.value(0).toString();
    summary.finishedAt = query.value(1).toString();
    summary.status = query.value(2).toString();
    summary.rootsProcessed = query.value(3).toInt();
    summary.snapshotsCreated = query.value(4).toInt();
    summary.eventsCompacted = query.value(5).toInt();
    summary.message = query.value(6).toString();
    return summary;
}

QVector<SnapshotCompareRow> SnapshotService::compareSnapshotRows(int snapshotId, const ScanResult &current, QString *errorMessage) const
{
    QVector<SnapshotCompareRow> rows;

    QSqlQuery snapshotRootQuery(m_database);
    snapshotRootQuery.prepare("SELECT root_path FROM snapshots WHERE id = ?");
    snapshotRootQuery.addBindValue(snapshotId);
    if (!snapshotRootQuery.exec() || !snapshotRootQuery.next()) {
        if (errorMessage) {
            *errorMessage = snapshotRootQuery.lastError().text().isEmpty() ? QStringLiteral("Snapshot not found") : snapshotRootQuery.lastError().text();
        }
        return rows;
    }
    const QString rootPath = snapshotRootQuery.value(0).toString();

    const QHash<QString, qint64> snapshotState = loadFolderState(rootPath, snapshotId, errorMessage);
    if (errorMessage && !errorMessage->isEmpty()) {
        return rows;
    }

    QHash<QString, qint64> currentSizes;
    for (const TreeEntry &entry : current.treeEntries) {
        if (entry.kind == TreeEntryKind::Folder) {
            currentSizes.insert(entry.path, entry.size);
        }
    }

    QSet<QString> allPaths;
    for (auto it = snapshotState.cbegin(); it != snapshotState.cend(); ++it) {
        allPaths.insert(it.key());
    }
    for (auto it = currentSizes.cbegin(); it != currentSizes.cend(); ++it) {
        allPaths.insert(it.key());
    }

    for (const QString &path : allPaths) {
        const qint64 previousSize = snapshotState.value(path, 0);
        const qint64 currentSize = currentSizes.value(path, 0);
        const qint64 delta = currentSize - previousSize;
        if (delta == 0) {
            continue;
        }

        SnapshotCompareRow row;
        row.path = path;
        row.previousSize = previousSize;
        row.currentSize = currentSize;
        row.deltaBytes = delta;
        if (previousSize == 0) {
            row.percentChangeText = QStringLiteral("new");
        } else {
            const double percent = 100.0 * static_cast<double>(delta) / static_cast<double>(previousSize);
            row.percentChangeText = QString::number(percent, 'f', 1) + "%";
        }
        rows.push_back(row);
    }

    std::sort(rows.begin(), rows.end(), [](const SnapshotCompareRow &left, const SnapshotCompareRow &right) {
        return std::llabs(left.deltaBytes) > std::llabs(right.deltaBytes);
    });

    return rows;
}

// ---------------------------------------------------------------------------
// Merkle-style structural ledger
// ---------------------------------------------------------------------------

bool SnapshotService::ensureDefaultResolutionRules(QString *errorMessage)
{
    QSqlQuery countQuery(m_database);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM resolution_rules")) || !countQuery.next()) {
        if (errorMessage) {
            *errorMessage = countQuery.lastError().text();
        }
        return false;
    }
    if (countQuery.value(0).toInt() > 0) {
        return true;
    }

    const QString home = QDir::fromNativeSeparators(QDir::homePath());
    struct Seed {
        QString path;
        ResolutionTier tier;
    };

    QVector<Seed> seeds;
    // High resolution: the folders a user actually works in.
    for (const QString &name : {QStringLiteral("Documents"), QStringLiteral("Desktop"),
                                QStringLiteral("Downloads"), QStringLiteral("Pictures")}) {
        seeds.push_back({home + QLatin1Char('/') + name, ResolutionTier::HighResolution});
    }
    // Secondary drives get file-level tracking too.
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        const QString root = PathUtils::normalizePath(storage.rootPath());
        if (!storage.isValid() || !storage.isReady() || root.isEmpty()) {
            continue;
        }
        if (!root.startsWith(QStringLiteral("C:"), Qt::CaseInsensitive)) {
            seeds.push_back({root, ResolutionTier::HighResolution});
        }
    }
    // Macro resolution: system trees tracked by folder size only.
    for (const QString &path : {QStringLiteral("C:/Windows"), QStringLiteral("C:/Program Files"),
                                QStringLiteral("C:/Program Files (x86)"), QStringLiteral("C:/ProgramData"),
                                home + QStringLiteral("/AppData/Roaming")}) {
        seeds.push_back({path, ResolutionTier::Macro});
    }
    // Blacklist: volatile or protected locations.
    for (const QString &path : {QStringLiteral("C:/$Recycle.Bin"),
                                QStringLiteral("C:/System Volume Information"),
                                home + QStringLiteral("/AppData/Local/Packages"),
                                home + QStringLiteral("/AppData/Local/CrashDumps"),
                                home + QStringLiteral("/AppData/Local/Google/Chrome/User Data/Default/Cache")}) {
        seeds.push_back({path, ResolutionTier::Blacklist});
    }

    for (const Seed &seed : seeds) {
        if (!setResolutionRule(seed.path, seed.tier, errorMessage)) {
            return false;
        }
    }
    return true;
}

QVector<QPair<QString, ResolutionTier>> SnapshotService::resolutionRules(QString *errorMessage) const
{
    QVector<QPair<QString, ResolutionTier>> rules;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT path, tier FROM resolution_rules ORDER BY path ASC"))) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return rules;
    }
    while (query.next()) {
        rules.push_back({query.value(0).toString(), ResolutionTier(query.value(1).toInt())});
    }
    return rules;
}

bool SnapshotService::setResolutionRule(const QString &path, ResolutionTier tier, QString *errorMessage)
{
    const QString normalized = PathUtils::normalizePath(path);
    if (normalized.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot store a resolution rule for an empty path.");
        }
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO resolution_rules(path, tier) VALUES(?, ?) "
                                 "ON CONFLICT(path) DO UPDATE SET tier = excluded.tier"));
    query.addBindValue(normalized);
    query.addBindValue(int(tier));
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    m_rulesLoaded = false;
    return true;
}

ResolutionTier SnapshotService::tierForPath(const QString &path) const
{
    if (!m_rulesLoaded) {
        m_ruleCache = resolutionRules(nullptr);
        m_rulesLoaded = true;
    }

    const QString normalized = PathUtils::normalizePath(path).toLower();
    ResolutionTier best = ResolutionTier::HighResolution;
    int bestLength = -1;
    for (const QPair<QString, ResolutionTier> &rule : m_ruleCache) {
        QString prefix = PathUtils::normalizePath(rule.first).toLower();
        if (prefix.isEmpty()) {
            continue;
        }
        if (!normalized.startsWith(prefix)) {
            continue;
        }
        const bool boundary = normalized.length() == prefix.length()
            || prefix.endsWith(QLatin1Char('/'))
            || normalized.at(prefix.length()) == QLatin1Char('/');
        if (!boundary) {
            continue;
        }
        if (prefix.length() > bestLength) {
            bestLength = prefix.length();
            best = rule.second;
        }
    }
    return best;
}

int SnapshotService::registerFolder(const QString &path, QString *errorMessage) const
{
    QSqlQuery selectQuery(m_database);
    selectQuery.prepare(QStringLiteral("SELECT id FROM master_folders WHERE path = ?"));
    selectQuery.addBindValue(path);
    if (selectQuery.exec() && selectQuery.next()) {
        return selectQuery.value(0).toInt();
    }

    QSqlQuery insertQuery(m_database);
    insertQuery.prepare(QStringLiteral("INSERT INTO master_folders(path) VALUES(?)"));
    insertQuery.addBindValue(path);
    if (!insertQuery.exec()) {
        if (errorMessage) {
            *errorMessage = insertQuery.lastError().text();
        }
        return 0;
    }
    return insertQuery.lastInsertId().toInt();
}

void SnapshotService::writeLedgerRows(int snapshotId, const ScanResult &result,
                                      const QStringList &tiersByPath, QString *errorMessage) const
{
    Q_UNUSED(tiersByPath);

    // Baseline = the most recent *recorded* value per folder for this root, so a snapshot
    // that changed nothing does not force the next one to re-record the whole tree.
    QHash<int, QPair<qint64, QPair<int, int>>> previous;
    QSqlQuery previousQuery(m_database);
    previousQuery.prepare(QStringLiteral(
        "SELECT l.folder_id, l.total_size, l.file_count, l.folder_count "
        "FROM snapshot_ledger l "
        "JOIN snapshots s ON s.id = l.snapshot_id "
        "WHERE s.root_path = ? AND l.snapshot_id = ("
        "    SELECT MAX(l2.snapshot_id) FROM snapshot_ledger l2 "
        "    JOIN snapshots s2 ON s2.id = l2.snapshot_id "
        "    WHERE l2.folder_id = l.folder_id AND s2.root_path = ? AND s2.id < ?)"));
    previousQuery.addBindValue(result.rootPath);
    previousQuery.addBindValue(result.rootPath);
    previousQuery.addBindValue(snapshotId);
    if (previousQuery.exec()) {
        while (previousQuery.next()) {
            const int folderId = previousQuery.value(0).toInt();
            previous.insert(folderId, {previousQuery.value(1).toLongLong(),
                                       {previousQuery.value(2).toInt(), previousQuery.value(3).toInt()}});
        }
    }

    QSqlQuery insertQuery(m_database);
    insertQuery.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO snapshot_ledger(snapshot_id, folder_id, total_size, file_count, folder_count) "
        "VALUES(?, ?, ?, ?, ?)"));

    int written = 0;
    constexpr int kLedgerRowBudget = 50000;

    auto record = [&](const QString &path, qint64 size, int fileCount, int folderCount) {
        if (written >= kLedgerRowBudget) {
            return;
        }
        if (tierForPath(path) == ResolutionTier::Blacklist) {
            return;
        }
        const int folderId = registerFolder(path, errorMessage);
        if (folderId <= 0) {
            return;
        }
        const auto previousIt = previous.constFind(folderId);
        if (previousIt != previous.constEnd()
            && previousIt.value().first == size
            && previousIt.value().second.first == fileCount
            && previousIt.value().second.second == folderCount) {
            return; // unchanged: the ledger already describes it
        }

        insertQuery.addBindValue(snapshotId);
        insertQuery.addBindValue(folderId);
        insertQuery.addBindValue(size);
        insertQuery.addBindValue(fileCount);
        insertQuery.addBindValue(folderCount);
        if (insertQuery.exec()) {
            ++written;
        }
    };

    for (const FolderEntry &folder : result.folders) {
        record(folder.path, folder.totalSize, folder.fileCount, folder.folderCount);
    }
    if (result.rootPath.isEmpty()) {
        return;
    }

    // Make sure the root itself is always present.
    bool rootSeen = false;
    for (const FolderEntry &folder : result.folders) {
        if (folder.path.compare(result.rootPath, Qt::CaseInsensitive) == 0) {
            rootSeen = true;
            break;
        }
    }
    if (!rootSeen) {
        qint64 totalSize = 0;
        int fileCount = 0;
        for (const FileEntry &file : result.files) {
            totalSize += file.size;
            ++fileCount;
        }
        record(result.rootPath, totalSize, fileCount, 0);
    }
}

QVector<FolderHistoryPoint> SnapshotService::folderHistory(const QString &rootPath, const QString &folderPath,
                                                           int maxPoints, QString *errorMessage) const
{
    QVector<FolderHistoryPoint> points;
    const QString normalizedRoot = PathUtils::normalizePath(rootPath);
    const QString normalizedFolder = PathUtils::normalizePath(folderPath);

    // All snapshots of the root, oldest first.
    QVector<QPair<int, QDateTime>> snapshots;
    QSqlQuery snapshotQuery(m_database);
    snapshotQuery.prepare(QStringLiteral("SELECT id, created_at FROM snapshots WHERE root_path = ? ORDER BY id ASC"));
    snapshotQuery.addBindValue(normalizedRoot);
    if (!snapshotQuery.exec()) {
        if (errorMessage) {
            *errorMessage = snapshotQuery.lastError().text();
        }
        return points;
    }
    while (snapshotQuery.next()) {
        snapshots.push_back({snapshotQuery.value(0).toInt(),
                             QDateTime::fromString(snapshotQuery.value(1).toString(), Qt::ISODate)});
    }

    // Recorded ledger values for this folder, keyed by snapshot id.
    QHash<int, FolderHistoryPoint> recorded;
    QSqlQuery ledgerQuery(m_database);
    ledgerQuery.prepare(QStringLiteral(
        "SELECT l.snapshot_id, l.total_size, l.file_count, l.folder_count "
        "FROM snapshot_ledger l "
        "JOIN master_folders f ON f.id = l.folder_id "
        "WHERE f.path = ?"));
    ledgerQuery.addBindValue(normalizedFolder);
    if (!ledgerQuery.exec()) {
        if (errorMessage) {
            *errorMessage = ledgerQuery.lastError().text();
        }
        return points;
    }
    while (ledgerQuery.next()) {
        FolderHistoryPoint point;
        point.size = ledgerQuery.value(1).toLongLong();
        point.fileCount = ledgerQuery.value(2).toInt();
        point.folderCount = ledgerQuery.value(3).toInt();
        recorded.insert(ledgerQuery.value(0).toInt(), point);
    }

    // Carry the last recorded value forward so every snapshot gets a point (the ledger only
    // stores changes, which is what keeps it small).
    bool haveValue = false;
    for (const QPair<int, QDateTime> &snapshot : snapshots) {
        const auto it = recorded.constFind(snapshot.first);
        if (it != recorded.constEnd()) {
            haveValue = true;
            FolderHistoryPoint point = it.value();
            point.recordedAt = snapshot.second;
            points.push_back(point);
        } else if (haveValue) {
            FolderHistoryPoint point = points.last();
            point.recordedAt = snapshot.second;
            points.push_back(point);
        }
    }

    if (points.size() > maxPoints) {
        points = points.mid(points.size() - maxPoints);
    }
    return points;
}

int SnapshotService::ledgerRowCount(QString *errorMessage) const
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM snapshot_ledger")) || !query.next()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return 0;
    }
    return query.value(0).toInt();
}

}
