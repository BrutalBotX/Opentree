#pragma once

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

namespace opentree {

struct TrashItem {
    qint64 id = 0;
    QString path;
    qint64 size = 0;
    bool isFolder = false;
    QString rootPath;
    QDateTime stagedAt;
    QString reason;
};

// Staged-deletion bookkeeping. Staging never touches the file system: it only records the
// intent so the projected reclaim can be shown and reviewed. Moving items to the Recycle
// Bin is a separate, explicitly confirmed action.
class VirtualTrashService {
public:
    explicit VirtualTrashService(QSqlDatabase database);

    bool stage(const QString &path, qint64 size, bool isFolder, const QString &rootPath,
               const QString &reason, QString *errorMessage = nullptr);
    bool unstage(qint64 id, QString *errorMessage = nullptr);
    bool clearStaged(QString *errorMessage = nullptr);
    QVector<TrashItem> stagedItems(QString *errorMessage = nullptr) const;
    qint64 stagedBytes(QString *errorMessage = nullptr) const;
    QStringList stagedPaths(QString *errorMessage = nullptr) const;

    // Whether a path is a safe candidate for the Recycle Bin (absolute, not a drive root,
    // not a protected system location).
    static bool canMoveToRecycleBin(const QString &path, QString *reason = nullptr);

    // Destructive. Sends the path to the Windows Recycle Bin (FOF_ALLOWUNDO) so it stays
    // recoverable. Never call this without an explicit user confirmation.
    static bool moveToRecycleBin(const QString &path, QString *errorMessage = nullptr);

private:
    QSqlDatabase m_database;
};

}
