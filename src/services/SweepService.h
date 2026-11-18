#pragma once

#include <QSqlDatabase>
#include <QString>

#include "domain/ScanTypes.h"

namespace opentree {

struct SweepResult {
    int stagedCount = 0;
    qint64 stagedBytes = 0;
    int duplicateCandidates = 0;
    int junkCandidates = 0;
    QString message;
};

// Scheduled duplicate/junk sweep. Runs against a scan result and stages the candidates in
// the virtual trash (the database only, nothing on disk is touched), so the user can review
// them in the Trash tab before anything is deleted.
class SweepService {
public:
    explicit SweepService(QSqlDatabase database);

    // Stages "all but the kept copy" of every duplicate group at or above
    // `duplicateMinimumBytes`, and junk files at or above `junkMinimumBytes`. The duplicate
    // threshold and the system-folder skip come from the Deduplication settings, so the
    // sweep and the Duplicates tab agree on what counts. A cap keeps a single run from
    // flooding the trash.
    SweepResult run(const ScanResult &result,
                    bool findDuplicates,
                    bool findJunk,
                    qint64 duplicateMinimumBytes,
                    qint64 junkMinimumBytes,
                    bool skipSystemPaths = true,
                    int maxStaged = 500);

private:
    QSqlDatabase m_database;
};

} // namespace opentree
