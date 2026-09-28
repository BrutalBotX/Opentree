#include "utils/MemoryProbe.h"

#include "utils/Logger.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace opentree {

double privateMegabytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters {};
    counters.cb = sizeof(counters);
    // GetProcessMemoryInfo is exported by kernel32, so no extra library is needed.
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters))) {
        return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
    }
#endif
    return 0.0;
}

void logMemoryUsage(const QString &stage)
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters {};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters))) {
        return;
    }
    const double privateMb = static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
    const double workingMb = static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
    Logger::info(QStringLiteral("memory[%1]: %2 MB private, %3 MB working")
                     .arg(stage)
                     .arg(privateMb, 0, 'f', 1)
                     .arg(workingMb, 0, 'f', 1));
#else
    Q_UNUSED(stage)
#endif
}

}
