#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "domain/ScanTypes.h"

QT_FORWARD_DECLARE_CLASS(QAction)
QT_FORWARD_DECLARE_CLASS(QMenu)
QT_FORWARD_DECLARE_CLASS(QWidget)

namespace opentree {

class VirtualTrashService;

// One item to stage in the virtual trash.
struct StageRequest {
    QString path;
    qint64 size = 0;
    bool isFolder = false;
};

// The actions every view shares, so the context menus are identical across the app.
struct SharedEntryActions {
    QAction *showInExplorer = nullptr;
    QAction *copyPath = nullptr;
    QAction *stage = nullptr;
};

// Appends "Show in Explorer", "Copy Path" and "Stage for Deletion" to a context menu.
SharedEntryActions addSharedEntryActions(QMenu &menu);

// Stages one entry and reports failures to the user. Returns true when it was staged.
bool stageEntry(QWidget *context, const TreeEntry &entry);

// Runs whichever shared action was chosen; returns true when it was handled here. Actions
// that need application state (staging) go through EntryActionHub.
bool runSharedEntryAction(QWidget *context, QAction *chosen, const TreeEntry &entry,
                          const SharedEntryActions &actions);

// Opens the containing folder and selects the item.
void showEntryInExplorer(const QString &path);

// Staging hub: every view stages through this, so the Trash tab refreshes no matter where
// the action came from. AppController installs the trash service once the database is up.
class EntryActionHub : public QObject {
    Q_OBJECT

public:
    static EntryActionHub *instance();

    void setTrashService(VirtualTrashService *service);
    bool isAvailable() const;

    // Returns true when at least one item was staged. staged() is emitted once per call.
    bool stage(const QVector<StageRequest> &items, const QString &rootPath, const QString &reason,
               int *stagedCount = nullptr, qint64 *stagedBytes = nullptr, QString *errorMessage = nullptr);
    bool stage(const TreeEntry &entry, QString *errorMessage = nullptr);
    bool stage(const QString &path, qint64 size, bool isFolder, const QString &rootPath,
               const QString &reason, QString *errorMessage = nullptr);

signals:
    void staged(int count, qint64 bytes);

private:
    explicit EntryActionHub(QObject *parent = nullptr);

    VirtualTrashService *m_trashService = nullptr;
};

} // namespace opentree
