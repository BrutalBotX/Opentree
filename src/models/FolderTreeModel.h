#pragma once

#include <QAbstractItemModel>
#include <QFileIconProvider>
#include <QHash>
#include <vector>
#include <memory>

#include "domain/ScanTypes.h"

namespace opentree {

class FolderTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    explicit FolderTreeModel(QObject *parent = nullptr);
    ~FolderTreeModel() override;

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void setEntries(const QVector<TreeEntry> &entries, const QString &rootPath);
    void setRootSessions(const QVector<RootSession> &sessions);
    QModelIndex indexForPath(const QString &path) const;
    QString entryPath(const QModelIndex &index) const;
    TreeEntry treeEntry(const QModelIndex &index) const;

private:
    struct Node {
        TreeEntry entry;
        Node *parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
    };

    Node *nodeFromIndex(const QModelIndex &index) const;

    std::unique_ptr<Node> m_root;
    QHash<QString, Node *> m_nodesByPath;
    QFileIconProvider m_iconProvider;
};

}
