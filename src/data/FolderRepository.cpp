#include "data/FolderRepository.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "utils/PathUtils.h"

namespace opentree {

FolderRepository::FolderRepository(const QSqlDatabase &database)
    : m_database(database)
{
}

bool FolderRepository::replaceAll(const QVector<FolderEntry> &folders, const QString &rootPath, QString *errorMessage)
{
    const QString normalizedRoot = PathUtils::normalizePath(rootPath);
    if (!m_database.transaction()) {
        if (errorMessage) {
            *errorMessage = m_database.lastError().text();
        }
        return false;
    }

    QSqlQuery deleteQuery(m_database);
    deleteQuery.prepare("DELETE FROM folders WHERE last_scan_root = ?");
    deleteQuery.addBindValue(normalizedRoot);
    if (!deleteQuery.exec()) {
        if (errorMessage) {
            *errorMessage = deleteQuery.lastError().text();
        }
        m_database.rollback();
        return false;
    }

    QSqlQuery insertQuery(m_database);
    insertQuery.prepare(
        "INSERT INTO folders(path, parent_path, name, total_size, file_count, last_scan_root) "
        "VALUES(?, ?, ?, ?, ?, ?)");

    for (const FolderEntry &folder : folders) {
        insertQuery.addBindValue(folder.path);
        insertQuery.addBindValue(folder.parentPath);
        insertQuery.addBindValue(folder.name);
        insertQuery.addBindValue(folder.totalSize);
        insertQuery.addBindValue(folder.fileCount);
        insertQuery.addBindValue(normalizedRoot);
        if (!insertQuery.exec()) {
            if (errorMessage) {
                *errorMessage = insertQuery.lastError().text();
            }
            m_database.rollback();
            return false;
        }
    }

    return m_database.commit();
}

QVector<FolderEntry> FolderRepository::loadByRoot(const QString &rootPath, QString *errorMessage) const
{
    const QString normalizedRoot = PathUtils::normalizePath(rootPath);
    QVector<FolderEntry> folders;
    QSqlQuery query(m_database);
    query.prepare(
        "SELECT path, parent_path, name, total_size, file_count "
        "FROM folders WHERE last_scan_root = ? ORDER BY path ASC");
    query.addBindValue(normalizedRoot);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return {};
    }

    while (query.next()) {
        FolderEntry folder;
        folder.path = query.value(0).toString();
        folder.parentPath = query.value(1).toString();
        folder.name = query.value(2).toString();
        folder.totalSize = query.value(3).toLongLong();
        folder.fileCount = query.value(4).toInt();
        folders.push_back(folder);
    }

    return folders;
}

}
