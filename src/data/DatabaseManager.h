#pragma once

#include <QSqlDatabase>
#include <QString>

namespace opentree {

class DatabaseManager {
public:
    DatabaseManager();
    ~DatabaseManager();

    bool initialize();
    QSqlDatabase database() const;
    QString databasePath() const;
    QString lastError() const;

private:
    QString m_connectionName;
    QString m_databasePath;
    QString m_lastError;
};

}
