#pragma once

#include <QString>
#include <QVector>

#include <functional>

#include "domain/ScanTypes.h"

namespace opentree {

struct DuplicateFile {
    QString path;
    qint64 size = 0;
};

struct DuplicateGroup {
    qint64 size = 0;
    QVector<DuplicateFile> files;
    qint64 wastedBytes() const { return size * std::max<qint64>(0, files.size() - 1); }
};

struct DedupResult {
    QVector<DuplicateGroup> groups;
    qint64 wastedBytes = 0;
    int candidateFiles = 0; // files that shared a size with at least one other file
    int hashedFiles = 0;    // files that went through full hashing
    int skippedFiles = 0;   // files below the size threshold or unreadable
    int filteredFiles = 0;  // system/unsafe paths left out before hashing
};

// True for locations that are slow to hash or unsafe to touch: Windows, Program Files,
// ProgramData, recycle/volume metadata, page/hibernation files and packaged app caches.
// Hashing these can trip over locked system files, so the duplicate finder skips them by
// default.
bool isSystemScanExcluded(const QString &path);

// Finds identical files using the size-grouped, staged hashing approach from the project
// plan: group by size, compare a partial hash, then confirm with a full hash.
class DedupService {
public:
    using ProgressFn = std::function<void(int percent, const QString &message)>;

    static constexpr qint64 PartialHashBytes = 64 * 1024;

    DedupResult findDuplicates(const QVector<FileEntry> &files,
                               qint64 minimumBytes,
                               bool skipSystemPaths = true,
                               const ProgressFn &progress = {}) const;
};

}
