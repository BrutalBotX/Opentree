#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

#include <memory>

namespace opentree {

struct FileEntry {
    QString path;
    QString parentPath;
    QString name;
    qint64 size = 0;
};

struct FolderEntry {
    QString path;
    QString parentPath;
    QString name;
    qint64 totalSize = 0;
    int fileCount = 0;
    int folderCount = 0;
};

enum class TreeEntryKind {
    Folder,
    File,
};

enum class ViewMetric {
    Size,
    Percentage,
    Files,
};

enum class SizeDisplayMode {
    Adaptive,
};

struct TreeEntry {
    TreeEntryKind kind = TreeEntryKind::Folder;
    QString path;
    QString parentPath;
    QString name;
    qint64 size = 0;
    qint64 parentSize = 0;
    int fileCount = 0;
    int folderCount = 0;
};

struct ScanResult {
    QString rootPath;
    QVector<FolderEntry> folders;
    QVector<FileEntry> files;
    QVector<TreeEntry> treeEntries;
    bool usedEverything = false;
};

// Scan results are large (a whole-drive scan is hundreds of megabytes of paths), so they are
// shared instead of copied: one instance is passed around by pointer.
using ScanResultPtr = std::shared_ptr<const ScanResult>;

struct RootSession {
    QString rootPath;
    ScanResultPtr result;
};

}

Q_DECLARE_METATYPE(opentree::TreeEntry)
