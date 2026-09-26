#include "services/VirtualTrashService.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>

#include "services/DedupService.h"
#include "utils/PathUtils.h"

namespace opentree {

VirtualTrashService::VirtualTrashService(QSqlDatabase database)
    : m_database(std::move(database))
{
}

bool VirtualTrashService::stage(const QString &path, qint64 size, bool isFolder,
                                const QString &rootPath, const QString &reason,
                                QString *errorMessage)
{
    const QString normalized = PathUtils::normalizePath(path);
    if (normalized.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot stage an empty path.");
        }
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare("INSERT INTO virtual_trash(path, size, is_folder, root_path, staged_at, reason) "
                  "VALUES(?, ?, ?, ?, ?, ?) "
                  "ON CONFLICT(path) DO UPDATE SET size = excluded.size, is_folder = excluded.is_folder, "
                  "root_path = excluded.root_path, staged_at = excluded.staged_at, reason = excluded.reason");
    query.addBindValue(normalized);
    query.addBindValue(size);
    query.addBindValue(isFolder ? 1 : 0);
    query.addBindValue(rootPath);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(reason);

    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool VirtualTrashService::unstage(qint64 id, QString *errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare("DELETE FROM virtual_trash WHERE id = ?");
    query.addBindValue(id);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool VirtualTrashService::clearStaged(QString *errorMessage)
{
    QSqlQuery query(m_database);
    if (!query.exec("DELETE FROM virtual_trash")) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

QVector<TrashItem> VirtualTrashService::stagedItems(QString *errorMessage) const
{
    QVector<TrashItem> items;
    QSqlQuery query(m_database);
    if (!query.exec("SELECT id, path, size, is_folder, root_path, staged_at, reason FROM virtual_trash "
                    "ORDER BY staged_at DESC")) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return items;
    }

    while (query.next()) {
        TrashItem item;
        item.id = query.value(0).toLongLong();
        item.path = query.value(1).toString();
        item.size = query.value(2).toLongLong();
        item.isFolder = query.value(3).toInt() != 0;
        item.rootPath = query.value(4).toString();
        item.stagedAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODate);
        item.reason = query.value(6).toString();
        items.push_back(item);
    }
    return items;
}

qint64 VirtualTrashService::stagedBytes(QString *errorMessage) const
{
    QSqlQuery query(m_database);
    if (!query.exec("SELECT COALESCE(SUM(size), 0) FROM virtual_trash")) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return 0;
    }
    return query.next() ? query.value(0).toLongLong() : 0;
}

QStringList VirtualTrashService::stagedPaths(QString *errorMessage) const
{
    QStringList paths;
    const QVector<TrashItem> items = stagedItems(errorMessage);
    paths.reserve(items.size());
    for (const TrashItem &item : items) {
        paths.push_back(item.path);
    }
    return paths;
}

bool VirtualTrashService::canMoveToRecycleBin(const QString &path, QString *reason)
{
    const QString normalized = PathUtils::normalizePath(path);
    if (normalized.isEmpty()) {
        if (reason) {
            *reason = QStringLiteral("The path is empty.");
        }
        return false;
    }

    // Refuse drive roots: "C:/" has no name and deleting a volume root is never intended.
    if (normalized.length() <= 3 && normalized.endsWith(QLatin1Char('/'))) {
        if (reason) {
            *reason = QStringLiteral("Refusing to remove a drive root (%1).").arg(normalized);
        }
        return false;
    }

    if (!QFileInfo::exists(normalized)) {
        if (reason) {
            *reason = QStringLiteral("The item no longer exists on disk.");
        }
        return false;
    }

    // Never let a staged system location reach the Recycle Bin by accident.
    if (isSystemScanExcluded(normalized)) {
        if (reason) {
            *reason = QStringLiteral("Refusing to remove a protected system location.");
        }
        return false;
    }

    return true;
}

bool VirtualTrashService::moveToRecycleBin(const QString &path, QString *errorMessage)
{
    QString refusal;
    if (!canMoveToRecycleBin(path, &refusal)) {
        if (errorMessage) {
            *errorMessage = refusal;
        }
        return false;
    }

    const QString nativePath = QDir::toNativeSeparators(PathUtils::normalizePath(path));
    std::wstring buffer = nativePath.toStdWString();
    buffer.push_back(L'\0'); // SHFileOperation expects a double-null terminated list
    buffer.push_back(L'\0');

    SHFILEOPSTRUCTW operation = {};
    operation.wFunc = FO_DELETE;
    operation.pFrom = buffer.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

    const int result = SHFileOperationW(&operation);
    if (result != 0 || operation.fAnyOperationsAborted) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not move %1 to the Recycle Bin (code %2).").arg(nativePath).arg(result);
        }
        return false;
    }
    return true;
}

}
