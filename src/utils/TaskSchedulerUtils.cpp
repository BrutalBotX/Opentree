#include "utils/TaskSchedulerUtils.h"

#include <QProcess>

namespace opentree::TaskSchedulerUtils {

bool syncSnapshotTask(const QString &executablePath, bool enabled, SnapshotScheduleMode mode, const QTime &time, QString *errorMessage)
{
    if (!enabled) {
        QProcess process;
        process.start("schtasks.exe", {"/Delete", "/TN", "OpenTreeBackgroundSnapshots", "/F"});
        process.waitForFinished();
        return true;
    }

    const QString taskCommand = QStringLiteral("\"%1\" --background-snapshot").arg(executablePath);
    QString scheduleValue = "DAILY";
    if (mode == SnapshotScheduleMode::Weekly) {
        scheduleValue = "WEEKLY";
    } else if (mode == SnapshotScheduleMode::Monthly) {
        scheduleValue = "MONTHLY";
    }

    QProcess process;
    process.start(
        "schtasks.exe",
        {
            "/Create",
            "/F",
            "/SC",
            scheduleValue,
            "/TN",
            "OpenTreeBackgroundSnapshots",
            "/TR",
            taskCommand,
            "/ST",
            time.toString("HH:mm"),
        });
    if (!process.waitForFinished()) {
        if (errorMessage) {
            *errorMessage = process.errorString();
        }
        return false;
    }

    if (process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = QString::fromLocal8Bit(process.readAllStandardError());
        }
        return false;
    }

    return true;
}

}
