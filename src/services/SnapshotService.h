#pragma once

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

class SnapshotService {
public:
    explicit SnapshotService(const QSqlDatabase &database);

    SnapshotCreateResult createSnapshot(const ScanResult &result, qint64 thresholdBytes, QString *errorMessage = nullptr);
    QVector<SnapshotSummary> listSnapshots(QString *errorMessage = nullptr) const;
    SnapshotCompareResult compareSnapshotToCurrent(int snapshotId, const ScanResult &current, QString *errorMessage = nullptr) const;
    QVector<SnapshotCompareRow> compareSnapshotRows(int snapshotId, const ScanResult &current, QString *errorMessage = nullptr) const;
    QVector<SnapshotFileEvent> snapshotFileEvents(int snapshotId, QString *errorMessage = nullptr) const;
    int compactFileEvents(int retentionDays, QString *errorMessage = nullptr);
    bool recordBackgroundRun(const BackgroundRunSummary &summary, QString *errorMessage = nullptr);
    BackgroundRunSummary latestBackgroundRun(QString *errorMessage = nullptr) const;

private:
    QHash<QString, qint64> loadFolderState(const QString &rootPath, int upToSnapshotId, QString *errorMessage = nullptr) const;
    QSqlDatabase m_database;
};

}
