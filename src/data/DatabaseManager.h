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

    // Applies the in-place column migrations (files.root_path, the background-run sweep
    // columns) to an already-open schema database. Public so the tests can exercise the
    // migration from an old schema directly.
    static bool ensureColumnMigrations(QSqlDatabase &db, QString *errorMessage = nullptr);

private:
    QString m_connectionName;
    QString m_databasePath;
    QString m_lastError;
};

}
