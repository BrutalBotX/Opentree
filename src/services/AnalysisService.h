#pragma once

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "domain/ScanTypes.h"

namespace opentree {

struct DiskForecast {
    bool available = false;
    qint64 volumeTotal = 0;
    qint64 volumeFree = 0;
    double growthBytesPerDay = 0.0;
    int daysUntilFull = -1; // -1 when not growing or not enough history
    int sampleCount = 0;
    QString basis;
};

struct StaleFile {
    QString path;
    qint64 size = 0;
    QDateTime lastModified;
    int daysOld = 0;
};

struct JunkGroup {
    QString category;
    QString description;
    qint64 size = 0;
    int count = 0;
    QStringList examples;
};

struct JunkFile {
    QString path;
    qint64 size = 0;
    QString category;
};

struct InsightsResult {
    DiskForecast forecast;
    QVector<StaleFile> staleFiles;
    QVector<JunkGroup> junkGroups;
    qint64 staleBytes = 0;
    qint64 junkBytes = 0;
};

// Timeline/cleanup analytics: size trend forecasting from saved snapshots, plus stale and
// junk detection over the current scan.
class AnalysisService {
public:
    explicit AnalysisService(QSqlDatabase database);

    // Linear fit over the saved snapshots of a root, extended to the volume capacity.
    DiskForecast forecastForRoot(const QString &rootPath, QString *errorMessage = nullptr) const;

    // Stats the largest files of the scan (bounded) and reports the ones untouched for a
    // while. Bounded because stat-ing every file of a huge scan is too slow.
    QVector<StaleFile> staleFiles(const ScanResult &result, int olderThanDays, int maxItems,
                                  QString *errorMessage = nullptr) const;

    // Pattern-based junk detection using the scan's own sizes (no extra IO).
    QVector<JunkGroup> junkFiles(const ScanResult &result) const;

    // Every junk file with its category (same matcher as junkFiles), used for staging.
    QVector<JunkFile> junkFileList(const ScanResult &result) const;

    static constexpr int StaleStatBudget = 800;

private:
    QSqlDatabase m_database;
};

}
