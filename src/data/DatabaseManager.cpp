#include "data/DatabaseManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

#include "utils/Logger.h"

namespace {

QString resolveDatabasePath()
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    return dataDir + "/opentree.db";
}

QString tableSql(QSqlDatabase &db, const QString &table)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT sql FROM sqlite_master WHERE type = 'table' AND name = ?"));
    query.addBindValue(table);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return {};
}

bool execute(QSqlDatabase &db, const QString &statement, QString *errorMessage)
{
    QSqlQuery query(db);
    if (!query.exec(statement)) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

// Older databases declared "path UNIQUE" on folders and files. That breaks as soon as two
// scanned roots overlap (a folder inside C:\ also being scanned as its own root), because
// the same path then belongs to two roots. The tables are rebuilt with a composite key
// before the schema is applied.
bool migrateLegacyPathUniqueness(QSqlDatabase &db, QString *errorMessage)
{
    const struct {
        const char *table;
        const char *createStatement;
        const char *copyStatement;
    } migrations[] = {
        {"folders",
         "CREATE TABLE folders ("
         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "path TEXT NOT NULL,"
         "parent_path TEXT,"
         "name TEXT NOT NULL,"
         "total_size INTEGER NOT NULL DEFAULT 0,"
         "file_count INTEGER NOT NULL DEFAULT 0,"
         "last_scan_root TEXT,"
         "UNIQUE(last_scan_root, path))",
         "INSERT OR REPLACE INTO folders(path, parent_path, name, total_size, file_count, last_scan_root) "
         "SELECT path, parent_path, name, total_size, file_count, last_scan_root FROM folders_legacy"},
        {"files",
         "CREATE TABLE files ("
         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "root_path TEXT NOT NULL,"
         "path TEXT NOT NULL,"
         "parent_path TEXT NOT NULL,"
         "name TEXT NOT NULL,"
         "size INTEGER NOT NULL DEFAULT 0,"
         "UNIQUE(root_path, path))",
         "INSERT OR REPLACE INTO files(root_path, path, parent_path, name, size) "
         "SELECT root_path, path, parent_path, name, size FROM files_legacy"},
    };

    for (const auto &migration : migrations) {
        const QString sql = tableSql(db, QString::fromLatin1(migration.table));
        if (sql.isEmpty() || !sql.contains(QStringLiteral("UNIQUE"), Qt::CaseInsensitive)) {
            continue;
        }
        // Only the legacy single-column form needs rebuilding.
        if (sql.contains(QStringLiteral("UNIQUE(last_scan_root, path)"), Qt::CaseInsensitive)
            || sql.contains(QStringLiteral("UNIQUE(root_path, path)"), Qt::CaseInsensitive)) {
            continue;
        }

        opentree::Logger::info(QStringLiteral("Migrating legacy %1 table to a composite key").arg(QString::fromLatin1(migration.table)));

        if (!db.transaction()) {
            if (errorMessage) {
                *errorMessage = db.lastError().text();
            }
            return false;
        }

        const QString legacyName = QStringLiteral("%1_legacy").arg(QString::fromLatin1(migration.table));
        const QStringList statements = {
            QStringLiteral("ALTER TABLE %1 RENAME TO %2").arg(QString::fromLatin1(migration.table), legacyName),
            QString::fromLatin1(migration.createStatement),
            QString::fromLatin1(migration.copyStatement),
            QStringLiteral("DROP TABLE %1").arg(legacyName),
        };
        for (const QString &statement : statements) {
            if (!execute(db, statement, errorMessage)) {
                db.rollback();
                return false;
            }
        }

        if (!db.commit()) {
            if (errorMessage) {
                *errorMessage = db.lastError().text();
            }
            return false;
        }
    }

    return true;
}

QString resolveFallbackSchemaPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString workspacePath = QDir(appDir).absoluteFilePath("../resources/sql/schema.sql");
    if (QFile::exists(workspacePath)) {
        return workspacePath;
    }

    const QString directPath = QDir(appDir).absoluteFilePath("resources/sql/schema.sql");
    if (QFile::exists(directPath)) {
        return directPath;
    }

    const QString parentPath = QDir(appDir).absoluteFilePath("../../resources/sql/schema.sql");
    if (QFile::exists(parentPath)) {
        return parentPath;
    }

    return parentPath;
}

}

namespace opentree {

DatabaseManager::DatabaseManager()
    : m_connectionName("opentree-main")
    , m_databasePath(resolveDatabasePath())
{
}

DatabaseManager::~DatabaseManager()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool DatabaseManager::initialize()
{
    Logger::info(QStringLiteral("Initializing database at %1").arg(m_databasePath));

    QSqlDatabase db = QSqlDatabase::contains(m_connectionName)
                          ? QSqlDatabase::database(m_connectionName)
                          : QSqlDatabase::addDatabase("QSQLITE", m_connectionName);

    db.setDatabaseName(m_databasePath);
    if (!db.open()) {
        m_lastError = db.lastError().text();
        return false;
    }

    QSqlQuery pragma(db);
    pragma.exec("PRAGMA journal_mode=WAL");
    pragma.exec("PRAGMA foreign_keys=ON");

    QString migrationError;
    if (!migrateLegacyPathUniqueness(db, &migrationError)) {
        m_lastError = QStringLiteral("Schema migration failed: %1").arg(migrationError);
        Logger::error(m_lastError);
        return false;
    }

    QFile schemaFile(":/sql/schema.sql");
    if (!schemaFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString fallbackPath = resolveFallbackSchemaPath();
        schemaFile.setFileName(fallbackPath);
        if (!schemaFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_lastError = QStringLiteral("Failed to open schema. Resource path and fallback path both failed: %1")
                              .arg(fallbackPath);
            Logger::error(m_lastError);
            return false;
        }
        Logger::warning(QStringLiteral("Embedded schema unavailable, using fallback schema file: %1").arg(fallbackPath));
    }

    const QStringList statements = QString::fromUtf8(schemaFile.readAll()).split(';', Qt::SkipEmptyParts);
    for (const QString &statement : statements) {
        const QString trimmed = statement.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        QSqlQuery query(db);
        if (!query.exec(trimmed)) {
            m_lastError = query.lastError().text();
            Logger::error(QStringLiteral("Schema statement failed: %1").arg(m_lastError));
            return false;
        }
    }

    QSqlQuery columnsQuery(db);
    if (!columnsQuery.exec(QStringLiteral("PRAGMA table_info(files)"))) {
        m_lastError = columnsQuery.lastError().text();
        return false;
    }

    bool hasRootPathColumn = false;
    while (columnsQuery.next()) {
        if (columnsQuery.value(1).toString().compare(QStringLiteral("root_path"), Qt::CaseInsensitive) == 0) {
            hasRootPathColumn = true;
            break;
        }
    }

    if (!hasRootPathColumn) {
        QSqlQuery alterQuery(db);
        if (!alterQuery.exec(QStringLiteral("ALTER TABLE files ADD COLUMN root_path TEXT"))) {
            m_lastError = alterQuery.lastError().text();
            Logger::error(QStringLiteral("Schema migration failed: %1").arg(m_lastError));
            return false;
        }
        QSqlQuery backfillQuery(db);
        if (!backfillQuery.exec(QStringLiteral("UPDATE files SET root_path = '' WHERE root_path IS NULL"))) {
            m_lastError = backfillQuery.lastError().text();
            Logger::error(QStringLiteral("Schema migration backfill failed: %1").arg(m_lastError));
            return false;
        }
    }

    Logger::info("Database initialized successfully");

    return true;
}

QSqlDatabase DatabaseManager::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

QString DatabaseManager::databasePath() const
{
    return m_databasePath;
}

QString DatabaseManager::lastError() const
{
    return m_lastError;
}

}
