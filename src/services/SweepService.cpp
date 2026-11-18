#include "services/SweepService.h"

#include <algorithm>

#include "services/AnalysisService.h"
#include "services/DedupService.h"
#include "services/VirtualTrashService.h"
#include "utils/Logger.h"

namespace opentree {

SweepService::SweepService(QSqlDatabase database)
    : m_database(std::move(database))
{
}

SweepResult SweepService::run(const ScanResult &result,
                              bool findDuplicates,
                              bool findJunk,
                              qint64 duplicateMinimumBytes,
                              qint64 junkMinimumBytes,
                              bool skipSystemPaths,
                              int maxStaged)
{
    SweepResult sweep;
    if (result.rootPath.isEmpty() || maxStaged <= 0) {
        return sweep;
    }

    VirtualTrashService trashService(m_database);
    const QString reasonBase = QStringLiteral("Scheduled sweep");

    if (findDuplicates && !result.files.isEmpty()) {
        DedupService dedupService;
        const DedupResult dedup = dedupService.findDuplicates(result.files, duplicateMinimumBytes, skipSystemPaths);
        for (const DuplicateGroup &group : dedup.groups) {
            if (sweep.stagedCount >= maxStaged) {
                break;
            }

            // Keep one copy: always the lexicographically smallest path, so the choice is
            // stable across runs. Depending on scan order used to stage "all but the first",
            // successive runs could stage the other copy too and end up with the whole group
            // in the trash.
            const QString keep = std::min_element(group.files.cbegin(), group.files.cend(),
                                                  [](const DuplicateFile &a, const DuplicateFile &b) {
                                                      return a.path < b.path;
                                                  })->path;
            for (const DuplicateFile &file : group.files) {
                if (sweep.stagedCount >= maxStaged) {
                    break;
                }
                if (file.path.compare(keep, Qt::CaseInsensitive) == 0) {
                    continue;
                }
                ++sweep.duplicateCandidates;
                QString error;
                if (!trashService.stage(file.path, file.size, false, result.rootPath,
                                        QStringLiteral("%1: duplicate of %2").arg(reasonBase, keep),
                                        &error)) {
                    Logger::warning(QStringLiteral("sweep: could not stage %1: %2").arg(file.path, error));
                    continue;
                }
                ++sweep.stagedCount;
                sweep.stagedBytes += file.size;
            }
        }
    }

    if (findJunk && !result.files.isEmpty()) {
        AnalysisService analysisService(m_database);
        const QVector<JunkFile> junkFiles = analysisService.junkFileList(result);
        for (const JunkFile &file : junkFiles) {
            if (sweep.stagedCount >= maxStaged) {
                break;
            }
            if (file.size < junkMinimumBytes) {
                continue;
            }
            ++sweep.junkCandidates;
            QString error;
            if (!trashService.stage(file.path, file.size, false, result.rootPath,
                                    QStringLiteral("%1: %2").arg(reasonBase, file.category), &error)) {
                Logger::warning(QStringLiteral("sweep: could not stage %1: %2").arg(file.path, error));
                continue;
            }
            ++sweep.stagedCount;
            sweep.stagedBytes += file.size;
        }
    }

    if (sweep.stagedCount > 0) {
        sweep.message = QStringLiteral("Staged %1 item(s) (%2 duplicate candidates, %3 junk candidates)")
                            .arg(sweep.stagedCount)
                            .arg(sweep.duplicateCandidates)
                            .arg(sweep.junkCandidates);
        Logger::info(QStringLiteral("sweep: %1").arg(sweep.message));
    }
    return sweep;
}

} // namespace opentree
