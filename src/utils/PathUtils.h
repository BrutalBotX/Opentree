#pragma once

#include <QString>

namespace opentree::PathUtils {

QString normalizePath(const QString &path);
QString fileName(const QString &path);
QString parentPath(const QString &path);

}
