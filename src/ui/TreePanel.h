#pragma once

#include <QWidget>

#include "domain/ScanTypes.h"

QT_FORWARD_DECLARE_CLASS(QLineEdit)
QT_FORWARD_DECLARE_CLASS(QModelIndex)
QT_FORWARD_DECLARE_CLASS(QSortFilterProxyModel)
QT_FORWARD_DECLARE_CLASS(QTreeView)

namespace opentree {

class FolderTreeModel;

class TreePanel : public QWidget {
    Q_OBJECT

public:
    explicit TreePanel(QWidget *parent = nullptr);

    void setModel(FolderTreeModel *model);
    void setScanResult(const ScanResult &result);
    void setRootSessions(const QVector<RootSession> &sessions);
    void selectEntryPath(const QString &path);
    QStringList visibleFolderPaths() const;

signals:
    void entryActivated(const TreeEntry &entry);
    void visiblePathsChanged(const QStringList &paths);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void showContextMenu(const QPoint &position);

    QLineEdit *m_filterEdit;
    QTreeView *m_treeView;
    QSortFilterProxyModel *m_proxyModel;
    FolderTreeModel *m_model = nullptr;
};

}
