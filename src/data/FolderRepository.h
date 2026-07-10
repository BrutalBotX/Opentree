#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "domain/ScanTypes.h"

namespace opentree {

class FolderRepository {
public:
    explicit FolderRepository(const QSqlDatabase &database);

    bool replaceAll(const QVector<FolderEntry> &folders, const QString &rootPath, QString *errorMessage = nullptr);
    QVector<FolderEntry> loadByRoot(const QString &rootPath, QString *errorMessage = nullptr) const;

private:
    QSqlDatabase m_database;
};

}
