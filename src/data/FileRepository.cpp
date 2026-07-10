#include "data/FileRepository.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "utils/PathUtils.h"

namespace opentree {

FileRepository::FileRepository(const QSqlDatabase &database)
    : m_database(database)
{
}

bool FileRepository::replaceAll(const QVector<FileEntry> &files, const QString &rootPath, QString *errorMessage)
{
    const QString normalizedRoot = PathUtils::normalizePath(rootPath);
    if (!m_database.transaction()) {
        if (errorMessage) {
            *errorMessage = m_database.lastError().text();
        }
        return false;
    }

    QSqlQuery deleteQuery(m_database);
    deleteQuery.prepare("DELETE FROM files WHERE root_path = ?");
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
        "INSERT INTO files(root_path, path, parent_path, name, size) VALUES(?, ?, ?, ?, ?)");

    for (const FileEntry &file : files) {
        insertQuery.addBindValue(normalizedRoot);
        insertQuery.addBindValue(file.path);
        insertQuery.addBindValue(file.parentPath);
        insertQuery.addBindValue(file.name);
        insertQuery.addBindValue(file.size);
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

QVector<FileEntry> FileRepository::loadByRoot(const QString &rootPath, QString *errorMessage) const
{
    const QString normalizedRoot = PathUtils::normalizePath(rootPath);
    QVector<FileEntry> files;
    QSqlQuery query(m_database);
    query.prepare(
        "SELECT path, parent_path, name, size "
        "FROM files WHERE root_path = ? ORDER BY path ASC");
    query.addBindValue(normalizedRoot);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return {};
    }

    while (query.next()) {
        FileEntry file;
        file.path = query.value(0).toString();
        file.parentPath = query.value(1).toString();
        file.name = query.value(2).toString();
        file.size = query.value(3).toLongLong();
        files.push_back(file);
    }

    return files;
}

}
