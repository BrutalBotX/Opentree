#pragma once

#include <QSqlDatabase>
#include <QVector>

#include "domain/ScanTypes.h"

namespace opentree {

class FileRepository {
public:
    explicit FileRepository(const QSqlDatabase &database);

    bool replaceAll(const QVector<FileEntry> &files, const QString &rootPath, QString *errorMessage = nullptr);
    QVector<FileEntry> loadByRoot(const QString &rootPath, QString *errorMessage = nullptr) const;

private:
    QSqlDatabase m_database;
};

}
