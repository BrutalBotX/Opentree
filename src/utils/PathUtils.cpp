#include "utils/PathUtils.h"

#include <QDir>
#include <QFileInfo>

namespace opentree::PathUtils {

QString normalizePath(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }

    QString normalized = QDir::fromNativeSeparators(QDir(path).absolutePath());
    const bool isDriveRoot = normalized.size() == 3
        && normalized[1] == ':'
        && normalized[2] == '/';

    if (normalized.endsWith('/') && !isDriveRoot) {
        normalized.chop(1);
    }
    return normalized;
}

QString fileName(const QString &path)
{
    return QFileInfo(path).fileName();
}

QString parentPath(const QString &path)
{
    return normalizePath(QFileInfo(path).dir().absolutePath());
}

}
