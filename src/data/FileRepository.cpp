#include "data/FileRepository.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace opentree {

FileRepository::FileRepository(const QSqlDatabase &database)
    : m_database(database)
{
}

bool FileRepository::replaceAll(const QVector<FileEntry> &files, QString *errorMessage)
{
    if (!m_database.transaction()) {
        if (errorMessage) {
            *errorMessage = m_database.lastError().text();
        }
        return false;
    }

    QSqlQuery deleteQuery(m_database);
    if (!deleteQuery.exec("DELETE FROM files")) {
        if (errorMessage) {
            *errorMessage = deleteQuery.lastError().text();
        }
        m_database.rollback();
        return false;
    }

    QSqlQuery insertQuery(m_database);
    insertQuery.prepare(
        "INSERT INTO files(path, parent_path, name, size) VALUES(?, ?, ?, ?)");

    for (const FileEntry &file : files) {
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

}
