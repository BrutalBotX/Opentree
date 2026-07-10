#pragma once

#include <QFutureWatcher>
#include <QObject>

#include "domain/ScanTypes.h"

namespace opentree {

class ConfigService;
class EverythingClient;

class ScanService : public QObject {
    Q_OBJECT

public:
    ScanService(ConfigService *configService, EverythingClient *everythingClient, QObject *parent = nullptr);

    void scanPath(const QString &rootPath);
    bool isBusy() const;
    ScanResult lastResult() const;
    QString lastError() const;
    static ScanResult performFilesystemScan(const QString &rootPath, const QStringList &excludedPatterns);
    static ScanResult buildTreeResult(const QString &rootPath, const QVector<FolderEntry> &folders, const QVector<FileEntry> &files);

signals:
    void scanStarted(const QString &rootPath);
    void scanProgress(int percent, const QString &message);
    void scanFinished();
    void scanFailed(const QString &message);

private:
    ScanResult performScan(const QString &rootPath, const QStringList &excludedPatterns);
    static QVector<FileEntry> collectFilesystemFiles(const QString &rootPath, const QStringList &excludedPatterns, std::function<void(int, const QString &)> progressCallback);

    ConfigService *m_configService;
    EverythingClient *m_everythingClient;
    QFutureWatcher<ScanResult> m_watcher;
    ScanResult m_lastResult;
    QString m_lastError;
};

}
