#include "services/ScanService.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QtConcurrent>

#include <algorithm>
#include <functional>
#include <memory>

#include <QHash>
#include <vector>

#include "integrations/EverythingClient.h"
#include "services/ConfigService.h"
#include "utils/Logger.h"
#include "utils/PathUtils.h"

namespace {

bool isExcluded(const QString &path, const QStringList &patterns)
{
    for (const QString &pattern : patterns) {
        if (path.contains(pattern, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

}

namespace opentree {

ScanService::ScanService(ConfigService *configService, EverythingClient *everythingClient, QObject *parent)
    : QObject(parent)
    , m_configService(configService)
    , m_everythingClient(everythingClient)
{
    connect(&m_watcher, &QFutureWatcher<ScanResult>::finished, this, [this]() {
        m_lastResult = m_watcher.result();
        if (m_lastResult.rootPath.isEmpty()) {
            emit scanFailed(m_lastError.isEmpty() ? QStringLiteral("Scan failed") : m_lastError);
            return;
        }
        emit scanFinished();
    });
}

void ScanService::scanPath(const QString &rootPath)
{
    if (isBusy()) {
        return;
    }

    const QString normalizedRoot = PathUtils::normalizePath(rootPath);
    m_lastError.clear();
    emit scanStarted(normalizedRoot);
    emit scanProgress(0, QStringLiteral("Starting scan"));

    const QStringList excluded = m_configService->excludedPatterns();
    m_watcher.setFuture(QtConcurrent::run([this, normalizedRoot, excluded]() {
        return performScan(normalizedRoot, excluded);
    }));
}

bool ScanService::isBusy() const
{
    return m_watcher.isRunning();
}

ScanResult ScanService::lastResult() const
{
    return m_lastResult;
}

QString ScanService::lastError() const
{
    return m_lastError;
}

ScanResult ScanService::performScan(const QString &rootPath, const QStringList &excludedPatterns)
{
    emit scanProgress(10, QStringLiteral("Scanning filesystem"));
    QVector<FileEntry> files = collectFilesystemFiles(rootPath, excludedPatterns, [this](int percent, const QString &message) {
        emit scanProgress(percent, message);
    });

    emit scanProgress(90, QStringLiteral("Building tree"));
    ScanResult result = buildTreeResult(rootPath, {}, files);
    result.usedEverything = false;
    emit scanProgress(100, QStringLiteral("Scan complete"));
    return result;
}

ScanResult ScanService::performFilesystemScan(const QString &rootPath, const QStringList &excludedPatterns)
{
    QVector<FileEntry> files = collectFilesystemFiles(rootPath, excludedPatterns, nullptr);
    return buildTreeResult(rootPath, {}, files);
}

QVector<FileEntry> ScanService::collectFilesystemFiles(
    const QString &rootPath,
    const QStringList &excludedPatterns,
    std::function<void(int, const QString &)> progressCallback)
{
    QVector<FileEntry> files;
    QDirIterator it(rootPath, QDir::Files, QDirIterator::Subdirectories);
    int scannedCount = 0;
    int nextProgressUpdate = 200;

    while (it.hasNext()) {
        const QString path = PathUtils::normalizePath(it.next());
        ++scannedCount;

        if (isExcluded(path, excludedPatterns)) {
            continue;
        }

        const QFileInfo info = it.fileInfo();
        FileEntry file;
        file.path = path;
        file.parentPath = PathUtils::normalizePath(info.dir().absolutePath());
        file.name = info.fileName();
        file.size = info.size();
        files.push_back(file);

        if (progressCallback && scannedCount >= nextProgressUpdate) {
            const int boundedPercent = std::min(85, 10 + (scannedCount / 200));
            progressCallback(boundedPercent, QStringLiteral("Scanning files: %1 found").arg(files.size()));
            nextProgressUpdate += 200;
        }
    }

    if (progressCallback) {
        progressCallback(88, QStringLiteral("Scanned %1 files").arg(files.size()));
    }

    return files;
}

ScanResult ScanService::buildTreeResult(const QString &rootPath, const QVector<FolderEntry> &folders, const QVector<FileEntry> &files)
{
    ScanResult result;
    result.rootPath = rootPath;
    result.files = files;

    QHash<QString, FolderEntry> folderMap;
    auto ensureFolder = [&](const QString &path) -> FolderEntry & {
        const QString normalized = PathUtils::normalizePath(path);
        auto it = folderMap.find(normalized);
        if (it != folderMap.end()) {
            return it.value();
        }

        FolderEntry entry;
        entry.path = normalized;
        entry.parentPath = normalized.compare(rootPath, Qt::CaseInsensitive) == 0
            ? QString()
            : PathUtils::parentPath(normalized);
        entry.name = PathUtils::fileName(normalized);
        if (entry.name.isEmpty()) {
            entry.name = normalized;
        }
        return folderMap.insert(normalized, entry).value();
    };

    ensureFolder(rootPath);
    for (const FolderEntry &folder : folders) {
        FolderEntry &entry = ensureFolder(folder.path);
        entry.parentPath = folder.parentPath;
        entry.name = folder.name;
    }

    for (const FileEntry &file : files) {
        QString currentPath = file.parentPath;
        while (!currentPath.isEmpty() && currentPath.startsWith(rootPath, Qt::CaseInsensitive)) {
            FolderEntry &folder = ensureFolder(currentPath);
            folder.totalSize += file.size;
            folder.fileCount += 1;

            if (currentPath.compare(rootPath, Qt::CaseInsensitive) == 0) {
                break;
            }
            const QString parent = PathUtils::parentPath(currentPath);
            if (parent == currentPath) {
                break;
            }
            currentPath = parent;
        }
    }

    for (auto it = folderMap.begin(); it != folderMap.end(); ++it) {
        QString currentPath = it->path;
        while (!currentPath.isEmpty() && currentPath.startsWith(rootPath, Qt::CaseInsensitive)) {
            if (currentPath.compare(it->path, Qt::CaseInsensitive) != 0) {
                FolderEntry &ancestor = ensureFolder(currentPath);
                ancestor.folderCount += 1;
            }

            if (currentPath.compare(rootPath, Qt::CaseInsensitive) == 0) {
                break;
            }

            const QString parent = PathUtils::parentPath(currentPath);
            if (parent == currentPath) {
                break;
            }
            currentPath = parent;
        }
    }

    result.folders.reserve(folderMap.size());
    for (auto it = folderMap.cbegin(); it != folderMap.cend(); ++it) {
        result.folders.push_back(it.value());
    }

    std::sort(result.folders.begin(), result.folders.end(), [](const FolderEntry &left, const FolderEntry &right) {
        return left.path < right.path;
    });

    result.treeEntries.reserve(result.folders.size() + result.files.size());
    for (const FolderEntry &folder : result.folders) {
        TreeEntry entry;
        entry.kind = TreeEntryKind::Folder;
        entry.path = folder.path;
        entry.parentPath = folder.parentPath;
        entry.name = folder.name;
        entry.size = folder.totalSize;
        entry.parentSize = folder.parentPath.isEmpty() ? 0 : folderMap.value(folder.parentPath).totalSize;
        entry.fileCount = folder.fileCount;
        entry.folderCount = folder.folderCount;
        result.treeEntries.push_back(entry);
    }
    for (const FileEntry &file : result.files) {
        TreeEntry entry;
        entry.kind = TreeEntryKind::File;
        entry.path = file.path;
        entry.parentPath = file.parentPath;
        entry.name = file.name;
        entry.size = file.size;
        entry.parentSize = folderMap.value(file.parentPath).totalSize;
        result.treeEntries.push_back(entry);
    }

    std::sort(result.treeEntries.begin(), result.treeEntries.end(), [](const TreeEntry &left, const TreeEntry &right) {
        if (left.parentPath != right.parentPath) {
            return left.parentPath < right.parentPath;
        }
        if (left.kind != right.kind) {
            return left.kind == TreeEntryKind::Folder;
        }
        return left.name.toLower() < right.name.toLower();
    });

    return result;
}

}
