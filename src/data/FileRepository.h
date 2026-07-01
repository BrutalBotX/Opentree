#pragma once

#include <QSqlDatabase>
#include <QVector>

#include "domain/ScanTypes.h"

namespace opentree {

class FileRepository {
public:
    explicit FileRepository(const QSqlDatabase &database);

    bool replaceAll(const QVector<FileEntry> &files, QString *errorMessage = nullptr);

private:
    QSqlDatabase m_database;
};

}
