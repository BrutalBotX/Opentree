#pragma once

#include <QString>

namespace opentree {

// Logs the memory this process is using (private bytes and working set) with a stage label.
// Private bytes is the number that matters: it is the memory Windows has committed to the
// process and cannot hand to anyone else.
void logMemoryUsage(const QString &stage);

// Whole-process private bytes in megabytes, or 0 where unsupported.
double privateMegabytes();

}
