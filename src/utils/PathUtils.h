#pragma once

#include <QString>
#include <QStringView>

namespace opentree::PathUtils {

QString normalizePath(const QString &path);
QString fileName(const QString &path);
QString parentPath(const QString &path);

// The same parent path without allocating: used in hot loops (sorting or grouping hundreds of
// thousands of scan entries) where a QString per call would be pure overhead.
QStringView parentPathView(const QString &path);

// True when `path` is `rootPath` itself or lives underneath it. Handles drive roots
// ("C:/") and trailing separators correctly (a naive root + "/" check breaks on drives).
bool isSameOrDescendant(const QString &path, const QString &rootPath);
// True when `ancestorPath` is `path` itself or one of its ancestors.
bool isAncestorOf(const QString &ancestorPath, const QString &path);

}
