#include "services/AnalysisService.h"

#include <QFileInfo>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QStorageInfo>
#include <QVariant>

#include <algorithm>
#include <cmath>

#include "utils/SizeFormatter.h"

namespace opentree {

namespace {

struct JunkPattern {
    const char *category;
    const char *description;
    QStringList suffixes;
    QStringList fragments;
    QStringList names;
};

const QVector<JunkPattern> &junkPatterns()
{
    static const QVector<JunkPattern> patterns = {
        {"Temporary files", "Editor scratch files, temp output and swap files",
         {QStringLiteral("tmp"), QStringLiteral("temp"), QStringLiteral("~")},
         {QStringLiteral("/temp/"), QStringLiteral("/tmp/")},
         {}},
        {"Log files", "Application and system logs that are safe to prune",
         {QStringLiteral("log"), QStringLiteral("log1"), QStringLiteral("old")},
         {},
         {}},
        {"Crash dumps", "Crash/hang dumps and minidumps",
         {QStringLiteral("dmp"), QStringLiteral("mdmp")},
         {QStringLiteral("/crashdumps/")},
         {}},
        {"Cache data", "Recreatable caches",
         {QStringLiteral("cache")},
         {QStringLiteral("/cache/"), QStringLiteral("/caches/"), QStringLiteral("/.cache/")},
         {}},
        {"Backup copies", "Editor backups and renamed copies",
         {QStringLiteral("bak"), QStringLiteral("backup"), QStringLiteral("orig"), QStringLiteral("sav")},
         {},
         {}},
        {"Shell clutter", "Thumbnail caches and folder metadata files",
         {},
         {},
         {QStringLiteral("thumbs.db"), QStringLiteral("desktop.ini"), QStringLiteral(".ds_store")}},
    };
    return patterns;
}

} // namespace

AnalysisService::AnalysisService(QSqlDatabase database)
    : m_database(std::move(database))
{
}

DiskForecast AnalysisService::forecastForRoot(const QString &rootPath, QString *errorMessage) const
{
    DiskForecast forecast;

    QSqlQuery query(m_database);
    query.prepare("SELECT created_at, total_size FROM snapshots WHERE root_path = ? ORDER BY id ASC");
    query.addBindValue(rootPath);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return forecast;
    }

    QVector<QPair<double, double>> samples; // days since first sample, bytes
    QDateTime firstStamp;
    while (query.next()) {
        const QDateTime stamp = QDateTime::fromString(query.value(0).toString(), Qt::ISODate);
        if (!stamp.isValid()) {
            continue;
        }
        if (!firstStamp.isValid()) {
            firstStamp = stamp;
        }
        const double days = firstStamp.msecsTo(stamp) / 86400000.0;
        samples.push_back({days, double(query.value(1).toLongLong())});
    }

    forecast.sampleCount = int(samples.size());
    if (samples.size() < 2) {
        forecast.basis = samples.isEmpty()
            ? QStringLiteral("No snapshots saved for this root yet.")
            : QStringLiteral("Only one snapshot so far, need at least two to forecast.");
        return forecast;
    }

    // Least squares fit of size over time.
    const double n = double(samples.size());
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXY = 0.0;
    double sumXX = 0.0;
    for (const QPair<double, double> &sample : samples) {
        sumX += sample.first;
        sumY += sample.second;
        sumXY += sample.first * sample.second;
        sumXX += sample.first * sample.first;
    }
    const double denominator = n * sumXX - sumX * sumX;
    const double slopePerDay = std::abs(denominator) < 1e-9
        ? 0.0
        : (n * sumXY - sumX * sumY) / denominator;

    const QStorageInfo storage(rootPath);
    if (storage.isValid() && storage.isReady()) {
        forecast.volumeTotal = storage.bytesTotal();
        forecast.volumeFree = storage.bytesFree();
    }

    forecast.growthBytesPerDay = slopePerDay;
    forecast.available = true;
    forecast.basis = QStringLiteral("%1 snapshots over %2 days")
                         .arg(samples.size())
                         .arg(QString::number(samples.last().first, 'f', 1));

    if (slopePerDay > 1.0 && forecast.volumeFree > 0) {
        const double days = double(forecast.volumeFree) / slopePerDay;
        forecast.daysUntilFull = days > 36500.0 ? -1 : int(std::ceil(days));
    }

    return forecast;
}

QVector<StaleFile> AnalysisService::staleFiles(const ScanResult &result, int olderThanDays,
                                               int maxItems, QString *errorMessage) const
{
    Q_UNUSED(errorMessage);

    // Only inspect the largest files: stat-ing a million small files is not worth it.
    QVector<FileEntry> candidates = result.files;
    std::sort(candidates.begin(), candidates.end(), [](const FileEntry &left, const FileEntry &right) {
        return left.size > right.size;
    });
    if (candidates.size() > StaleStatBudget) {
        candidates.resize(StaleStatBudget);
    }

    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-olderThanDays);
    QVector<StaleFile> stale;
    for (const FileEntry &file : candidates) {
        const QFileInfo info(file.path);
        if (!info.exists()) {
            continue;
        }
        const QDateTime modified = info.lastModified();
        if (!modified.isValid() || modified > cutoff) {
            continue;
        }

        StaleFile entry;
        entry.path = file.path;
        entry.size = file.size;
        entry.lastModified = modified;
        entry.daysOld = int(modified.daysTo(QDateTime::currentDateTime()));
        stale.push_back(entry);
    }

    std::sort(stale.begin(), stale.end(), [](const StaleFile &left, const StaleFile &right) {
        return left.size > right.size;
    });
    if (stale.size() > maxItems) {
        stale.resize(maxItems);
    }
    return stale;
}

QVector<JunkGroup> AnalysisService::junkFiles(const ScanResult &result) const
{
    QHash<QString, JunkGroup> groups;
    for (const JunkPattern &pattern : junkPatterns()) {
        JunkGroup group;
        group.category = QString::fromLatin1(pattern.category);
        group.description = QString::fromLatin1(pattern.description);
        groups.insert(group.category, group);
    }

    for (const FileEntry &file : result.files) {
        const QString path = file.path.toLower();
        const QString name = file.name.toLower();
        const QString suffix = QFileInfo(name).suffix();

        for (const JunkPattern &pattern : junkPatterns()) {
            bool match = false;
            if (!pattern.suffixes.isEmpty() && pattern.suffixes.contains(suffix)) {
                match = true;
            }
            if (!match) {
                for (const QString &fragment : pattern.fragments) {
                    if (path.contains(fragment)) {
                        match = true;
                        break;
                    }
                }
            }
            if (!match && !pattern.names.isEmpty() && pattern.names.contains(name)) {
                match = true;
            }
            if (!match) {
                continue;
            }

            const QString key = QString::fromLatin1(pattern.category);
            JunkGroup &group = groups[key];
            group.size += file.size;
            group.count += 1;
            if (group.examples.size() < 5) {
                group.examples << file.path;
            }
            break;
        }
    }

    QVector<JunkGroup> junk;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        if (it.value().count > 0) {
            junk.push_back(it.value());
        }
    }
    std::sort(junk.begin(), junk.end(), [](const JunkGroup &left, const JunkGroup &right) {
        return left.size > right.size;
    });
    return junk;
}

}
