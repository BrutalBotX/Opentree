#include "ui/EntryActions.h"

#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>

#include "services/VirtualTrashService.h"

namespace opentree {

SharedEntryActions addSharedEntryActions(QMenu &menu)
{
    SharedEntryActions actions;
    actions.showInExplorer = menu.addAction(QStringLiteral("Show in Explorer"));
    actions.copyPath = menu.addAction(QStringLiteral("Copy Path"));
    menu.addSeparator();
    actions.stage = menu.addAction(QStringLiteral("Stage for Deletion"));
    actions.stage->setToolTip(QStringLiteral("Add this item to the virtual trash list. Nothing is deleted."));
    return actions;
}

void showEntryInExplorer(const QString &path)
{
    const QString argument = QFileInfo(path).isDir()
        ? QDir::toNativeSeparators(path)
        : QStringLiteral("/select,") + QDir::toNativeSeparators(path);
    QProcess::startDetached(QStringLiteral("explorer.exe"), {argument});
}

bool stageEntry(QWidget *context, const TreeEntry &entry)
{
    QString error;
    if (EntryActionHub::instance()->stage(entry, &error)) {
        return true;
    }
    QMessageBox::warning(context, QStringLiteral("Stage for deletion"),
                         error.isEmpty() ? QStringLiteral("The item could not be staged.") : error);
    return false;
}

bool runSharedEntryAction(QWidget *context, QAction *chosen, const TreeEntry &entry,
                          const SharedEntryActions &actions)
{
    if (!chosen) {
        return false;
    }

    if (chosen == actions.showInExplorer) {
        showEntryInExplorer(entry.path);
        return true;
    }

    if (chosen == actions.copyPath) {
        QGuiApplication::clipboard()->setText(entry.path);
        return true;
    }

    if (chosen == actions.stage) {
        stageEntry(context, entry);
        return true;
    }

    return false;
}

EntryActionHub::EntryActionHub(QObject *parent)
    : QObject(parent)
{
}

EntryActionHub *EntryActionHub::instance()
{
    static EntryActionHub hub;
    return &hub;
}

void EntryActionHub::setTrashService(VirtualTrashService *service)
{
    m_trashService = service;
}

bool EntryActionHub::isAvailable() const
{
    return m_trashService != nullptr;
}

bool EntryActionHub::stage(const QVector<StageRequest> &items, const QString &rootPath,
                           const QString &reason, int *stagedCount, qint64 *stagedBytes,
                           QString *errorMessage)
{
    if (!m_trashService) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The virtual trash is not available (database not ready).");
        }
        return false;
    }

    int count = 0;
    qint64 bytes = 0;
    QString lastError;
    for (const StageRequest &item : items) {
        if (!m_trashService->stage(item.path, item.size, item.isFolder, rootPath, reason, &lastError)) {
            continue;
        }
        ++count;
        bytes += item.size;
    }

    if (stagedCount) {
        *stagedCount = count;
    }
    if (stagedBytes) {
        *stagedBytes = bytes;
    }
    if (errorMessage) {
        *errorMessage = count > 0 ? QString() : lastError;
    }

    if (count > 0) {
        emit staged(count, bytes);
    }
    return count > 0;
}

bool EntryActionHub::stage(const TreeEntry &entry, QString *errorMessage)
{
    if (entry.path.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Nothing selected.");
        }
        return false;
    }

    StageRequest request;
    request.path = entry.path;
    request.size = entry.size;
    request.isFolder = entry.kind == TreeEntryKind::Folder;
    return stage({request}, entry.parentPath, QStringLiteral("Staged from a context menu"), nullptr, nullptr, errorMessage);
}

bool EntryActionHub::stage(const QString &path, qint64 size, bool isFolder, const QString &rootPath,
                           const QString &reason, QString *errorMessage)
{
    StageRequest request;
    request.path = path;
    request.size = size;
    request.isFolder = isFolder;
    return stage({request}, rootPath, reason, nullptr, nullptr, errorMessage);
}

} // namespace opentree
