#include "models/FolderTreeModel.h"

#include <QFileInfo>
#include <QHash>

#include "utils/SizeFormatter.h"

namespace opentree {

FolderTreeModel::FolderTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
    , m_root(std::make_unique<Node>())
{
}

FolderTreeModel::~FolderTreeModel() = default;

QModelIndex FolderTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    Node *parentNode = nodeFromIndex(parent);
    if (!parentNode || row < 0 || row >= static_cast<int>(parentNode->children.size())) {
        return {};
    }

    return createIndex(row, column, parentNode->children[row].get());
}

QModelIndex FolderTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) {
        return {};
    }

    Node *node = nodeFromIndex(child);
    if (!node || !node->parent || node->parent == m_root.get()) {
        return {};
    }

    Node *parentNode = node->parent;
    Node *grandParent = parentNode->parent;
    int row = 0;
    if (grandParent) {
        for (int index = 0; index < static_cast<int>(grandParent->children.size()); ++index) {
            if (grandParent->children[index].get() == parentNode) {
                row = index;
                break;
            }
        }
    }
    return createIndex(row, 0, parentNode);
}

int FolderTreeModel::rowCount(const QModelIndex &parent) const
{
    const Node *parentNode = nodeFromIndex(parent);
    return parentNode ? static_cast<int>(parentNode->children.size()) : 0;
}

int FolderTreeModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant FolderTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const Node *node = nodeFromIndex(index);
    if (!node) {
        return {};
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0:
            return node->entry.name;
        default:
            break;
        }
    }

    if (role == Qt::DecorationRole && index.column() == 0) {
        if (node->entry.kind == TreeEntryKind::Folder) {
            return m_iconProvider.icon(QFileIconProvider::Folder);
        }
        return m_iconProvider.icon(QFileInfo(node->entry.path));
    }

    if (role == Qt::ToolTipRole) {
        return node->entry.path;
    }

    return {};
}

QVariant FolderTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case 0:
        return QStringLiteral("Name");
    default:
        return {};
    }
}

void FolderTreeModel::setEntries(const QVector<TreeEntry> &entries, const QString &rootPath)
{
    setRootSessions({RootSession {rootPath, ScanResult {rootPath, {}, {}, entries, false}}});
}

void FolderTreeModel::setRootSessions(const QVector<RootSession> &sessions)
{
    beginResetModel();
    m_root = std::make_unique<Node>();
    m_root->entry.path = QStringLiteral("__invisible_root__");
    m_root->entry.name = QStringLiteral("root");
    m_nodesByPath.clear();

    QHash<QString, Node *> nodes;
    nodes.insert(m_root->entry.path, m_root.get());

    for (const RootSession &session : sessions) {
        if (session.result.rootPath.isEmpty()) {
            continue;
        }

        for (const TreeEntry &entry : session.result.treeEntries) {
            QString parentKey = entry.parentPath;
            if (entry.path.compare(session.result.rootPath, Qt::CaseInsensitive) == 0) {
                parentKey = m_root->entry.path;
            }

            Node *parentNode = nodes.value(parentKey, m_root.get());
            auto node = std::make_unique<Node>();
            node->entry = entry;
            node->parent = parentNode;
            Node *rawNode = node.get();
            parentNode->children.push_back(std::move(node));
            nodes.insert(entry.path, rawNode);
        }
    }

    m_nodesByPath = nodes;

    endResetModel();
}

QModelIndex FolderTreeModel::indexForPath(const QString &path) const
{
    Node *node = m_nodesByPath.value(path, nullptr);
    if (!node || node == m_root.get() || !node->parent) {
        return {};
    }

    Node *parentNode = node->parent;
    for (int row = 0; row < static_cast<int>(parentNode->children.size()); ++row) {
        if (parentNode->children[row].get() == node) {
            return createIndex(row, 0, node);
        }
    }

    return {};
}

QString FolderTreeModel::entryPath(const QModelIndex &index) const
{
    const Node *node = nodeFromIndex(index);
    return node ? node->entry.path : QString();
}

TreeEntry FolderTreeModel::treeEntry(const QModelIndex &index) const
{
    const Node *node = nodeFromIndex(index);
    return node ? node->entry : TreeEntry {};
}

FolderTreeModel::Node *FolderTreeModel::nodeFromIndex(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return m_root.get();
    }
    return static_cast<Node *>(index.internalPointer());
}

}
