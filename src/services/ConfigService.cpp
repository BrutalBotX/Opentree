#include "services/ConfigService.h"

#include <QDir>
#include <QStandardPaths>

namespace {

QString resolveConfigPath()
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(baseDir);
    return baseDir + "/opentree.ini";
}

}

namespace opentree {

ConfigService::ConfigService()
    : m_configPath(resolveConfigPath())
    , m_settings(m_configPath, QSettings::IniFormat)
{
    ensureDefaults();
}

QString ConfigService::configPath() const
{
    return m_configPath;
}

qint64 ConfigService::snapshotThresholdBytes() const
{
    return m_settings.value("Snapshots/Threshold", 50LL * 1024 * 1024).toLongLong();
}

void ConfigService::setSnapshotThresholdBytes(qint64 bytes)
{
    m_settings.setValue("Snapshots/Threshold", bytes);
    m_settings.sync();
}

int ConfigService::snapshotRetentionDays() const
{
    return m_settings.value("Snapshots/RetentionDays", 30).toInt();
}

void ConfigService::setSnapshotRetentionDays(int days)
{
    m_settings.setValue("Snapshots/RetentionDays", days);
    m_settings.sync();
}

qint64 ConfigService::dedupMinimumBytes() const
{
    return m_settings.value("Deduplication/MinFileSize", 50LL * 1024 * 1024).toLongLong();
}

QStringList ConfigService::excludedPatterns() const
{
    return m_settings.value("Scanning/ExclusionPatterns").toString().split(';', Qt::SkipEmptyParts);
}

int ConfigService::graphMaxNodes() const
{
    return m_settings.value("Graph/MaxNodes", 500).toInt();
}

qint64 ConfigService::graphMinimumFolderBytes() const
{
    return m_settings.value("Graph/MinFolderSize", 100LL * 1024 * 1024).toLongLong();
}

ViewMetric ConfigService::viewMetric() const
{
    const QString value = m_settings.value("General/ViewMetric", "size").toString().toLower();
    if (value == "percentage") {
        return ViewMetric::Percentage;
    }
    if (value == "files") {
        return ViewMetric::Files;
    }
    return ViewMetric::Size;
}

void ConfigService::setViewMetric(ViewMetric metric)
{
    QString value = "size";
    if (metric == ViewMetric::Percentage) {
        value = "percentage";
    } else if (metric == ViewMetric::Files) {
        value = "files";
    }
    m_settings.setValue("General/ViewMetric", value);
    m_settings.sync();
}

double ConfigService::othersThresholdPercent() const
{
    return m_settings.value("General/OthersThresholdPercent", 1.0).toDouble();
}

void ConfigService::setOthersThresholdPercent(double percent)
{
    m_settings.setValue("General/OthersThresholdPercent", percent);
    m_settings.sync();
}

QString ConfigService::themeId() const
{
    return m_settings.value("General/Theme", "dark").toString();
}

void ConfigService::setThemeId(const QString &themeId)
{
    m_settings.setValue("General/Theme", themeId);
    m_settings.sync();
}

QString ConfigService::themesDirectory() const
{
    return QDir(QFileInfo(m_configPath).absolutePath()).filePath("themes");
}

QString ConfigService::everythingExecutablePath() const
{
    return m_settings.value("Scanning/EverythingExecutablePath").toString();
}

void ConfigService::setEverythingExecutablePath(const QString &path)
{
    m_settings.setValue("Scanning/EverythingExecutablePath", path);
    m_settings.sync();
}

bool ConfigService::snapshotScheduleEnabled() const
{
    return m_settings.value("Snapshots/ScheduleEnabled", false).toBool();
}

void ConfigService::setSnapshotScheduleEnabled(bool enabled)
{
    m_settings.setValue("Snapshots/ScheduleEnabled", enabled);
    m_settings.sync();
}

SnapshotScheduleMode ConfigService::snapshotScheduleMode() const
{
    const QString value = m_settings.value("Snapshots/ScheduleMode", "daily").toString().toLower();
    if (value == "weekly") {
        return SnapshotScheduleMode::Weekly;
    }
    if (value == "monthly") {
        return SnapshotScheduleMode::Monthly;
    }
    return SnapshotScheduleMode::Daily;
}

void ConfigService::setSnapshotScheduleMode(SnapshotScheduleMode mode)
{
    QString value = "daily";
    if (mode == SnapshotScheduleMode::Weekly) {
        value = "weekly";
    } else if (mode == SnapshotScheduleMode::Monthly) {
        value = "monthly";
    }
    m_settings.setValue("Snapshots/ScheduleMode", value);
    m_settings.sync();
}

QTime ConfigService::snapshotScheduleTime() const
{
    return m_settings.value("Snapshots/ScheduleTime", QTime(22, 0)).toTime();
}

void ConfigService::setSnapshotScheduleTime(const QTime &time)
{
    m_settings.setValue("Snapshots/ScheduleTime", time);
    m_settings.sync();
}

QStringList ConfigService::snapshotWhitelist() const
{
    return m_settings.value("Snapshots/Whitelist").toStringList();
}

void ConfigService::setSnapshotWhitelist(const QStringList &paths)
{
    m_settings.setValue("Snapshots/Whitelist", paths);
    m_settings.sync();
}

void ConfigService::ensureDefaults()
{
    if (!m_settings.contains("General/Theme")) {
        m_settings.setValue("General/Theme", "dark");
        m_settings.setValue("General/ViewMetric", "size");
        m_settings.setValue("General/OthersThresholdPercent", 1.0);
        m_settings.setValue("Scanning/ExclusionPatterns", "$Recycle.Bin;System Volume Information");
        m_settings.setValue("Scanning/EverythingExecutablePath", QString());
        m_settings.setValue("Snapshots/Threshold", 50LL * 1024 * 1024);
        m_settings.setValue("Snapshots/RetentionDays", 30);
        m_settings.setValue("Snapshots/ScheduleEnabled", false);
        m_settings.setValue("Snapshots/ScheduleMode", "daily");
        m_settings.setValue("Snapshots/ScheduleTime", QTime(22, 0));
        m_settings.setValue("Snapshots/Whitelist", QStringList());
        m_settings.setValue("Deduplication/MinFileSize", 50LL * 1024 * 1024);
        m_settings.setValue("Graph/MaxNodes", 500);
        m_settings.setValue("Graph/MinFolderSize", 100LL * 1024 * 1024);
        m_settings.sync();
    }
}

}
