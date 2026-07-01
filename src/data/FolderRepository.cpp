#include "data/FolderRepository.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace opentree {

FolderRepository::FolderRepository(const QSqlDatabase &database)
    : m_database(database)
{
}

bool FolderRepository::replaceAll(const QVector<FolderEntry> &folders, const QString &rootPath, QString *errorMessage)
{
    if (!m_database.transaction()) {
        if (errorMessage) {
            *errorMessage = m_database.lastError().text();
        }
        return false;
    }

    QSqlQuery deleteQuery(m_database);
    if (!deleteQuery.exec("DELETE FROM folders")) {
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
        insertQuery.addBindValue(rootPath);
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
