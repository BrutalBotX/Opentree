#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

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

struct RootSession {
    QString rootPath;
    ScanResult result;
};

}

Q_DECLARE_METATYPE(opentree::TreeEntry)
