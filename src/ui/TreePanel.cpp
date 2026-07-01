#include "ui/TreePanel.h"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QProcess>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include "models/FolderTreeModel.h"

namespace opentree {

namespace {

QStringList collectVisiblePaths(QTreeView *treeView, QSortFilterProxyModel *proxyModel, FolderTreeModel *model, const QModelIndex &parent = {})
{
    QStringList paths;
    const int rowCount = proxyModel->rowCount(parent);
    for (int row = 0; row < rowCount; ++row) {
        const QModelIndex proxyIndex = proxyModel->index(row, 0, parent);
        if (!proxyIndex.isValid()) {
            continue;
        }

        const QModelIndex sourceIndex = proxyModel->mapToSource(proxyIndex);
        const TreeEntry entry = model->treeEntry(sourceIndex);
        if (!entry.path.isEmpty()) {
            paths.push_back(entry.path);
        }

        if (treeView->isExpanded(proxyIndex)) {
            paths.append(collectVisiblePaths(treeView, proxyModel, model, proxyIndex));
        }
    }
    return paths;
}

class FolderFilterProxyModel : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        if (filterRegularExpression().pattern().isEmpty()) {
            return true;
        }

        const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        if (!index.isValid()) {
            return false;
        }

        if (sourceModel()->data(index, Qt::DisplayRole).toString().contains(filterRegularExpression())) {
            return true;
        }

        const int childCount = sourceModel()->rowCount(index);
        for (int childRow = 0; childRow < childCount; ++childRow) {
            if (filterAcceptsRow(childRow, index)) {
                return true;
            }
        }

        return false;
    }
};

}

TreePanel::TreePanel(QWidget *parent)
    : QWidget(parent)
    , m_filterEdit(new QLineEdit(this))
    , m_treeView(new QTreeView(this))
    , m_proxyModel(new FolderFilterProxyModel(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_filterEdit->setPlaceholderText("Filter folders...");
    layout->addWidget(m_filterEdit);

    m_treeView->setAlternatingRowColors(true);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setSortingEnabled(false);
    m_treeView->setItemsExpandable(true);
    m_treeView->setExpandsOnDoubleClick(false);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_treeView);

    connect(m_treeView, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        if (!m_model) {
            return;
        }

        if (m_treeView->model()->hasChildren(index) && !m_treeView->isExpanded(index)) {
            m_treeView->expand(index);
        }

        const QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
        emit entryActivated(m_model->treeEntry(sourceIndex));
    });
    connect(m_treeView, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (!m_model || !m_treeView->model()->hasChildren(index) || !m_treeView->isExpanded(index)) {
            return;
        }

        m_treeView->collapse(index);
    });

    m_proxyModel->setRecursiveFilteringEnabled(true);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setFilterKeyColumn(0);

    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_proxyModel->setFilterFixedString(text);
        if (text.isEmpty()) {
            m_treeView->collapseAll();
        } else {
            m_treeView->expandAll();
        }
    });

    connect(m_treeView, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        showContextMenu(position);
    });
    connect(m_treeView, &QTreeView::expanded, this, [this]() {
        emit visiblePathsChanged(visibleFolderPaths());
    });
    connect(m_treeView, &QTreeView::collapsed, this, [this]() {
        emit visiblePathsChanged(visibleFolderPaths());
    });
}

void TreePanel::setModel(FolderTreeModel *model)
{
    m_model = model;
    m_proxyModel->setSourceModel(model);
    m_treeView->setModel(m_proxyModel);
    auto *header = m_treeView->header();
    header->setVisible(false);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(0, QHeaderView::Stretch);

    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current) {
        if (!m_model || !current.isValid()) {
            return;
        }

        emit entryActivated(m_model->treeEntry(m_proxyModel->mapToSource(current)));
    });
}

bool TreePanel::eventFilter(QObject *watched, QEvent *event)
{
    return QWidget::eventFilter(watched, event);
}

void TreePanel::setScanResult(const ScanResult &result)
{
    setRootSessions({RootSession {result.rootPath, result}});
}

void TreePanel::setRootSessions(const QVector<RootSession> &sessions)
{
    if (!m_model) {
        return;
    }

    m_model->setRootSessions(sessions);
    m_treeView->collapseAll();
    for (int row = 0; row < m_proxyModel->rowCount(); ++row) {
        const QModelIndex rootIndex = m_proxyModel->index(row, 0);
        if (rootIndex.isValid()) {
            m_treeView->expand(rootIndex);
        }
    }
    emit visiblePathsChanged(visibleFolderPaths());
}

void TreePanel::selectEntryPath(const QString &path)
{
    if (!m_model || path.isEmpty()) {
        return;
    }

    const QModelIndex sourceIndex = m_model->indexForPath(path);
    if (!sourceIndex.isValid()) {
        return;
    }

    QSignalBlocker treeBlocker(m_treeView);
    QSignalBlocker selectionBlocker(m_treeView->selectionModel());

    QModelIndex parentIndex = sourceIndex.parent();
    while (parentIndex.isValid()) {
        const QModelIndex proxyParent = m_proxyModel->mapFromSource(parentIndex);
        if (proxyParent.isValid()) {
            m_treeView->expand(proxyParent);
        }
        parentIndex = parentIndex.parent();
    }

    const QModelIndex proxyIndex = m_proxyModel->mapFromSource(sourceIndex);
    if (!proxyIndex.isValid()) {
        return;
    }

    m_treeView->setCurrentIndex(proxyIndex);
    m_treeView->scrollTo(proxyIndex, QAbstractItemView::PositionAtCenter);
}

QStringList TreePanel::visibleFolderPaths() const
{
    if (!m_model) {
        return {};
    }
    return collectVisiblePaths(m_treeView, m_proxyModel, m_model);
}

void TreePanel::showContextMenu(const QPoint &position)
{
    if (!m_model) {
        return;
    }

    const QModelIndex proxyIndex = m_treeView->indexAt(position);
    if (!proxyIndex.isValid()) {
        return;
    }

    const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    const TreeEntry entry = m_model->treeEntry(sourceIndex);
    if (entry.path.isEmpty()) {
        return;
    }

    QMenu menu(this);
    QAction *openAction = menu.addAction("Open");
    QAction *showInExplorerAction = menu.addAction("Show in Explorer");
    QAction *copyPathAction = menu.addAction("Copy Path");

    QAction *selectedAction = menu.exec(m_treeView->viewport()->mapToGlobal(position));
    if (!selectedAction) {
        return;
    }

    if (selectedAction == openAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(entry.path));
        return;
    }

    if (selectedAction == showInExplorerAction) {
        const QString argument = QFileInfo(entry.path).isDir()
            ? entry.path
            : QStringLiteral("/select,") + QDir::toNativeSeparators(entry.path);
        QProcess::startDetached("explorer.exe", {argument});
        return;
    }

    if (selectedAction == copyPathAction) {
        QGuiApplication::clipboard()->setText(entry.path);
    }
}

}
