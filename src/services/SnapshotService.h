#pragma once

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "domain/ScanTypes.h"

namespace opentree {

struct SnapshotSummary {
    int id = 0;
    QString createdAt;
    QString rootPath;
    qint64 totalSize = 0;
    int fileCount = 0;
    int itemCount = 0;
    int eventCount = 0;
};

struct BackgroundRunSummary {
    QString startedAt;
    QString finishedAt;
    QString status;
    int rootsProcessed = 0;
    int snapshotsCreated = 0;
    int eventsCompacted = 0;
    QString message;
};

struct SnapshotCreateResult {
    bool created = false;
    int changedItemCount = 0;
    qint64 maxDeltaBytes = 0;
    QString message;
};

struct SnapshotCompareResult {
    bool found = false;
    QString snapshotCreatedAt;
    qint64 totalDeltaBytes = 0;
    int changedFolderCount = 0;
    QString largestChangePath;
    qint64 largestChangeBytes = 0;
    QString largestGrowthPath;
    qint64 largestGrowthBytes = 0;
    QString largestShrinkPath;
    qint64 largestShrinkBytes = 0;
    int fileEventCount = 0;
};

struct SnapshotCompareRow {
    QString path;
    qint64 previousSize = 0;
    qint64 currentSize = 0;
    qint64 deltaBytes = 0;
    QString percentChangeText;
};

struct SnapshotFileEvent {
    QString eventType;
    QString path;
    qint64 oldSize = 0;
    qint64 newSize = 0;
};

struct FolderHistoryPoint {
    QDateTime recordedAt;
    qint64 size = 0;
    int fileCount = 0;
    int folderCount = 0;
};

// Scan resolution tiers from the project plan.
enum class ResolutionTier {
    Blacklist = 0, // skipped entirely
    Macro = 1,     // folder sizes only, no file events
    HighResolution = 2,
};

class SnapshotService {
public:
    explicit SnapshotService(const QSqlDatabase &database);

    SnapshotCreateResult createSnapshot(const ScanResult &result, qint64 thresholdBytes, QString *errorMessage = nullptr);
    QVector<SnapshotSummary> listSnapshots(QString *errorMessage = nullptr) const;
    bool deleteSnapshot(int snapshotId, QString *errorMessage = nullptr);
    SnapshotCompareResult compareSnapshotToCurrent(int snapshotId, const ScanResult &current, QString *errorMessage = nullptr) const;
    QVector<SnapshotCompareRow> compareSnapshotRows(int snapshotId, const ScanResult &current, QString *errorMessage = nullptr) const;
    QVector<SnapshotFileEvent> snapshotFileEvents(int snapshotId, QString *errorMessage = nullptr) const;
    int compactFileEvents(int retentionDays, QString *errorMessage = nullptr);
    bool recordBackgroundRun(const BackgroundRunSummary &summary, QString *errorMessage = nullptr);
    BackgroundRunSummary latestBackgroundRun(QString *errorMessage = nullptr) const;

    // ---- Merkle-style structural ledger ----

    // Seeds the default three-tier resolution rules when the table is empty.
    bool ensureDefaultResolutionRules(QString *errorMessage = nullptr);
    QVector<QPair<QString, ResolutionTier>> resolutionRules(QString *errorMessage = nullptr) const;
    bool setResolutionRule(const QString &path, ResolutionTier tier, QString *errorMessage = nullptr);
    ResolutionTier tierForPath(const QString &path) const;

    // Recorded ledger history for one folder (one point per snapshot that recorded a change).
    QVector<FolderHistoryPoint> folderHistory(const QString &rootPath, const QString &folderPath,
                                              int maxPoints = 60, QString *errorMessage = nullptr) const;

    // Total ledger rows, handy for diagnostics.
    int ledgerRowCount(QString *errorMessage = nullptr) const;

private:
    QHash<QString, qint64> loadFolderState(const QString &rootPath, int upToSnapshotId, QString *errorMessage = nullptr) const;
    int registerFolder(const QString &path, QString *errorMessage) const;
    void writeLedgerRows(int snapshotId, const ScanResult &result, const QStringList &tiersByPath,
                         QString *errorMessage) const;
    QSqlDatabase m_database;
    mutable QVector<QPair<QString, ResolutionTier>> m_ruleCache;
    mutable bool m_rulesLoaded = false;
};

}
