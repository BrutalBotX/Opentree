#pragma once

#include "services/ConfigService.h"

#include <QString>
#include <QTime>

namespace opentree::TaskSchedulerUtils {

bool syncSnapshotTask(const QString &executablePath, bool enabled, SnapshotScheduleMode mode, const QTime &time, QString *errorMessage = nullptr);

}
