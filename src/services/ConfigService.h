#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTime>

#include "domain/ScanTypes.h"

namespace opentree {

enum class SnapshotScheduleMode {
    Daily,
    Weekly,
    Monthly,
};

class ConfigService {
public:
    ConfigService();

    QString configPath() const;
    qint64 snapshotThresholdBytes() const;
    void setSnapshotThresholdBytes(qint64 bytes);
    int snapshotRetentionDays() const;
    void setSnapshotRetentionDays(int days);
    qint64 dedupMinimumBytes() const;
    void setDedupMinimumBytes(qint64 bytes);
    bool dedupSkipSystemFolders() const;
    void setDedupSkipSystemFolders(bool skip);
    QStringList excludedPatterns() const;
    int graphMaxNodes() const;
    bool graphFollowTreeExpansion() const;
    void setGraphFollowTreeExpansion(bool follow);
    qint64 graphMinimumFolderBytes() const;
    ViewMetric viewMetric() const;
    void setViewMetric(ViewMetric metric);
    SizeDisplayMode sizeDisplayMode() const;
    void setSizeDisplayMode(SizeDisplayMode mode);
    double othersThresholdPercent() const;
    void setOthersThresholdPercent(double percent);
    QString themeId() const;
    void setThemeId(const QString &themeId);
    QString themesDirectory() const;
    QStringList recentRoots() const;
    void setRecentRoots(const QStringList &paths);
    QString everythingExecutablePath() const;
    void setEverythingExecutablePath(const QString &path);
    bool useEverything() const;
    void setUseEverything(bool enabled);
    // Returns the configured Everything.exe path, or auto-detects a common install location.
    QString resolvedEverythingExecutablePath() const;
    bool snapshotScheduleEnabled() const;
    void setSnapshotScheduleEnabled(bool enabled);
    SnapshotScheduleMode snapshotScheduleMode() const;
    void setSnapshotScheduleMode(SnapshotScheduleMode mode);
    QTime snapshotScheduleTime() const;
    void setSnapshotScheduleTime(const QTime &time);
    QStringList snapshotWhitelist() const;
    void setSnapshotWhitelist(const QStringList &paths);

private:
    void ensureDefaults();

    QString m_configPath;
    QSettings m_settings;
};

}
