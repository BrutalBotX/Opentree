#include "services/DedupService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QStringList>

#include <algorithm>

namespace opentree {

namespace {

QByteArray hashFile(const QString &path, qint64 limit, bool *ok)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok) {
            *ok = false;
        }
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    qint64 remaining = limit > 0 ? limit : file.size();
    constexpr qint64 chunkSize = 1 << 20;
    while (remaining > 0) {
        const QByteArray chunk = file.read(std::min(chunkSize, remaining));
        if (chunk.isEmpty()) {
            break;
        }
        hash.addData(chunk);
        remaining -= chunk.size();
    }

    if (ok) {
        *ok = true;
    }
    return hash.result();
}

// Stable key so equal sizes/hashes group together.
QString sizeKey(qint64 size)
{
    return QString::number(size);
}

} // namespace

bool isSystemScanExcluded(const QString &path)
{
    const QString normalized = QDir::fromNativeSeparators(path).toLower();

    // Compare drive-root folders after the "c:/" prefix so a user folder that merely has
    // "Windows" in its name elsewhere is not excluded.
    QString relative = normalized;
    if (relative.size() > 2 && relative.at(1) == QLatin1Char(':')) {
        relative = relative.mid(2);
    }
    while (relative.startsWith(QLatin1Char('/'))) {
        relative.remove(0, 1);
    }

    static const QStringList rootPrefixes = {
        QStringLiteral("windows/"),
        QStringLiteral("program files/"),
        QStringLiteral("program files (x86)/"),
        QStringLiteral("programdata/"),
        QStringLiteral("$recycle.bin/"),
        QStringLiteral("system volume information/"),
        QStringLiteral("$winreagent/"),
        QStringLiteral("$windows.~bt/"),
        QStringLiteral("$windows.~ws/"),
        QStringLiteral("recovery/"),
        QStringLiteral("perflogs/"),
        QStringLiteral("config.msi/"),
    };
    for (const QString &prefix : rootPrefixes) {
        if (relative.startsWith(prefix)) {
            return true;
        }
    }

    // Volatile caches that are safe to ignore and expensive to hash.
    static const QStringList anywhereFragments = {
        QStringLiteral("/winsxs/"),
        QStringLiteral("/appdata/local/packages/"),
        QStringLiteral("/appdata/local/crashdumps/"),
        QStringLiteral("/appdata/local/temp/"),
    };
    for (const QString &fragment : anywhereFragments) {
        if (normalized.contains(fragment)) {
            return true;
        }
    }

    // Root-level system files.
    const QString fileName = normalized.section(QLatin1Char('/'), -1);
    if (fileName == QStringLiteral("pagefile.sys")
        || fileName == QStringLiteral("hiberfil.sys")
        || fileName == QStringLiteral("swapfile.sys")
        || fileName == QStringLiteral("dumpstack.log.tmp")) {
        return true;
    }

    return false;
}

DedupResult DedupService::findDuplicates(const QVector<FileEntry> &files,
                                         qint64 minimumBytes,
                                         bool skipSystemPaths,
                                         const ProgressFn &progress) const
{
    DedupResult result;

    if (progress) {
        progress(0, QStringLiteral("Grouping files by size"));
    }

    QHash<qint64, QVector<const FileEntry *>> bySize;
    for (const FileEntry &file : files) {
        if (skipSystemPaths && isSystemScanExcluded(file.path)) {
            ++result.filteredFiles;
            continue;
        }
        if (file.size < minimumBytes || file.size <= 0) {
            ++result.skippedFiles;
            continue;
        }
        bySize[file.size].push_back(&file);
    }

    // Only sizes shared by two or more files can contain duplicates.
    QVector<qint64> candidateSizes;
    for (auto it = bySize.cbegin(); it != bySize.cend(); ++it) {
        if (it.value().size() >= 2) {
            candidateSizes.push_back(it.key());
            result.candidateFiles += int(it.value().size());
        }
    }
    std::sort(candidateSizes.begin(), candidateSizes.end(), std::greater<qint64>());

    const int totalCandidates = std::max(1, int(candidateSizes.size()));
    int processed = 0;

    for (qint64 size : candidateSizes) {
        const QVector<const FileEntry *> &sameSize = bySize.value(size);

        // Stage 1: a cheap partial hash of the first chunk.
        QHash<QString, QVector<const FileEntry *>> byPartial;
        for (const FileEntry *file : sameSize) {
            bool ok = false;
            const QString partial = QString::fromLatin1(hashFile(file->path, PartialHashBytes, &ok).toHex());
            if (!ok) {
                ++result.skippedFiles;
                continue;
            }
            byPartial[partial].push_back(file);
        }

        // Stage 2: only partial-hash matches get a full hash.
        for (auto it = byPartial.cbegin(); it != byPartial.cend(); ++it) {
            if (it.value().size() < 2) {
                continue;
            }

            QHash<QString, QVector<const FileEntry *>> byFull;
            for (const FileEntry *file : it.value()) {
                bool ok = false;
                const QString full = QString::fromLatin1(hashFile(file->path, -1, &ok).toHex());
                if (!ok) {
                    ++result.skippedFiles;
                    continue;
                }
                ++result.hashedFiles;
                byFull[full].push_back(file);
            }

            for (auto full = byFull.cbegin(); full != byFull.cend(); ++full) {
                if (full.value().size() < 2) {
                    continue;
                }

                DuplicateGroup group;
                group.size = size;
                for (const FileEntry *file : full.value()) {
                    group.files.push_back({file->path, file->size});
                }
                std::sort(group.files.begin(), group.files.end(), [](const DuplicateFile &left, const DuplicateFile &right) {
                    return left.path < right.path;
                });
                result.wastedBytes += group.wastedBytes();
                result.groups.push_back(group);
            }
        }

        ++processed;
        if (progress) {
            progress(int(100.0 * processed / totalCandidates),
                     QStringLiteral("Hashed %1 of %2 size groups").arg(processed).arg(candidateSizes.size()));
        }
    }

    std::sort(result.groups.begin(), result.groups.end(), [](const DuplicateGroup &left, const DuplicateGroup &right) {
        return left.wastedBytes() > right.wastedBytes();
    });

    if (progress) {
        progress(100, QStringLiteral("Duplicate scan complete"));
    }
    return result;
}

}
