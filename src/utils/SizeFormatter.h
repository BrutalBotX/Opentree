#pragma once

#include <QString>

namespace opentree::SizeFormatter {

QString formatAdaptiveBytes(qint64 bytes);
QString formatBytes(qint64 bytes);

}
