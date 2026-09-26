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

namespace {

// Trim trailing separators so that "C:/Foo/" and "C:/Foo" are treated as the same root.
// A drive root collapses to "C:" which still works because the descendant check then
// requires the next character to be a separator.
QString trimmedRoot(const QString &path)
{
    QString trimmed = path;
    while (trimmed.size() > 1 && (trimmed.endsWith(QLatin1Char('/')) || trimmed.endsWith(QLatin1Char('\\')))) {
        trimmed.chop(1);
    }
    return trimmed;
}

bool isSameOrDescendantImpl(const QString &rawPath, const QString &rawRoot)
{
    if (rawRoot.isEmpty()) {
        return true;
    }

    const QString root = trimmedRoot(rawRoot);
    const QString path = trimmedRoot(rawPath);
    if (path.compare(root, Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (!path.startsWith(root, Qt::CaseInsensitive) || path.length() <= root.length()) {
        return false;
    }
    const QChar next = path.at(root.length());
    return next == QLatin1Char('/') || next == QLatin1Char('\\');
}

} // namespace

QString parentPath(const QString &path)
{
    return normalizePath(QFileInfo(path).dir().absolutePath());
}

bool isSameOrDescendant(const QString &path, const QString &rootPath)
{
    return isSameOrDescendantImpl(path, rootPath);
}

bool isAncestorOf(const QString &ancestorPath, const QString &path)
{
    return isSameOrDescendantImpl(path, ancestorPath);
}

}
