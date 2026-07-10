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
