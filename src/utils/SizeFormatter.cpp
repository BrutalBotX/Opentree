#include "utils/SizeFormatter.h"

namespace opentree::SizeFormatter {

QString formatBytes(qint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < 4) {
        value /= 1024.0;
        ++unitIndex;
    }

    if (unitIndex == 0) {
        return QString::number(bytes) + " " + units[unitIndex];
    }

    return QString::number(value, 'f', value >= 10.0 ? 1 : 2) + " " + units[unitIndex];
}

}
