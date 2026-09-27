#include "utils/TaskSchedulerUtils.h"

#include "utils/Logger.h"
#include "utils/ProcessUtils.h"

namespace opentree::TaskSchedulerUtils {

namespace {

constexpr int kSchtasksTimeoutMs = 20000;

} // namespace

bool syncSnapshotTask(const QString &executablePath, bool enabled, SnapshotScheduleMode mode, const QTime &time, QString *errorMessage)
{
    // schtasks.exe is a console program: run it hidden so no command prompt flashes over
    // the app while the scheduled task is updated.
    if (!enabled) {
        int exitCode = 0;
        QByteArray standardError;
        QString runError;
        if (!ProcessUtils::runHidden(QStringLiteral("schtasks.exe"),
                                     {QStringLiteral("/Delete"), QStringLiteral("/TN"),
                                      QStringLiteral("OpenTreeBackgroundSnapshots"), QStringLiteral("/F")},
                                     kSchtasksTimeoutMs, &exitCode, &standardError, &runError)) {
            if (errorMessage) {
                *errorMessage = runError;
            }
            return false;
        }
        // Deleting a task that does not exist is not an error.
        if (exitCode != 0 && !QString::fromLocal8Bit(standardError).contains(QStringLiteral("cannot find"), Qt::CaseInsensitive)) {
            Logger::warning(QStringLiteral("schtasks /Delete exited with %1: %2")
                                .arg(exitCode)
                                .arg(QString::fromLocal8Bit(standardError).trimmed()));
        }
        return true;
    }

    const QString taskCommand = QStringLiteral("\"%1\" --background-snapshot").arg(executablePath);
    QString scheduleValue = QStringLiteral("DAILY");
    if (mode == SnapshotScheduleMode::Weekly) {
        scheduleValue = QStringLiteral("WEEKLY");
    } else if (mode == SnapshotScheduleMode::Monthly) {
        scheduleValue = QStringLiteral("MONTHLY");
    }

    int exitCode = 0;
    QByteArray standardError;
    QString runError;
    if (!ProcessUtils::runHidden(QStringLiteral("schtasks.exe"),
                                 {QStringLiteral("/Create"), QStringLiteral("/F"),
                                  QStringLiteral("/SC"), scheduleValue,
                                  QStringLiteral("/TN"), QStringLiteral("OpenTreeBackgroundSnapshots"),
                                  QStringLiteral("/TR"), taskCommand,
                                  QStringLiteral("/ST"), time.toString(QStringLiteral("HH:mm"))},
                                 kSchtasksTimeoutMs, &exitCode, &standardError, &runError)) {
        if (errorMessage) {
            *errorMessage = runError;
        }
        return false;
    }

    if (exitCode != 0) {
        if (errorMessage) {
            *errorMessage = QString::fromLocal8Bit(standardError).trimmed();
        }
        return false;
    }

    return true;
}

} // namespace opentree::TaskSchedulerUtils
