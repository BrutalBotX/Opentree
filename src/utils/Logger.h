#pragma once

#include <QString>

namespace opentree::Logger {

void initialize();
QString logFilePath();
void info(const QString &message);
void warning(const QString &message);
void error(const QString &message);

}
