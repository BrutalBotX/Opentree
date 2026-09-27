#include "ui/GraphPanel.h"

#include <QHash>
#include <QCheckBox>#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSet>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "ui/EntryActions.h"
#include "utils/PathUtils.h"
#include "utils/SizeFormatter.h"
#include "utils/Logger.h"

#if defined(OPENTREE_HAVE_WEBENGINE)
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineView>
#endif

namespace opentree {

namespace {

bool isSameOrDescendant(const QString &path, const QString &rootPath)
{
    return PathUtils::isSameOrDescendant(path, rootPath);
}

bool pathIsAncestorOf(const QString &ancestorPath, const QString &path)
{
    return PathUtils::isAncestorOf(ancestorPath, path);
}

QString escapeJsString(QString value)
{
    value.replace("\\", "\\\\");
    value.replace("'", "\\'");
    value.replace("\r", "");
    value.replace("\n", "\\n");
    // Never let a path/name terminate the surrounding <script> block, and escape
    // the JS line separators that are invalid inside string literals.
    value.replace("</", "<\\/");
    value.replace(QChar(0x2028), "\\u2028");
    value.replace(QChar(0x2029), "\\u2029");
    return value;
}

double nodeMetric(const TreeEntry &entry, GraphPanel::NodeSizeMode mode)
{
    switch (mode) {
    case GraphPanel::NodeSizeMode::Files:
        return entry.fileCount;
    case GraphPanel::NodeSizeMode::Folders:
        return entry.folderCount;
    case GraphPanel::NodeSizeMode::Size:
    default:
        return entry.size / (1024.0 * 1024.0);
    }
}

double normalizedNodeSize(double value, double minValue, double maxValue)
{
    if (maxValue <= minValue) {
        return 24.0;
    }

    const double normalized = (std::log1p(std::max(0.0, value)) - std::log1p(std::max(0.0, minValue)))
        / (std::log1p(std::max(0.0, maxValue)) - std::log1p(std::max(0.0, minValue)));
    return 14.0 + std::clamp(normalized, 0.0, 1.0) * 28.0;
}

} // namespace

GraphPanel::GraphPanel(QWidget *parent)
    : QWidget(parent)
    , m_addressBar(new QLineEdit(this))
    , m_followTreeCheck(new QCheckBox(QStringLiteral("Follow tree expansion"), this))
    , m_summaryLabel(new QLabel(this))
#if defined(OPENTREE_HAVE_WEBENGINE)
    , m_bridge(new GraphBridge(this))
    , m_channel(new QWebChannel(this))
#endif
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    m_addressBar->setPlaceholderText("Enter folder path...");
    connect(m_addressBar, &QLineEdit::returnPressed, this, &GraphPanel::handleAddressSubmitted);
    m_summaryLabel->setWordWrap(true);
    m_followTreeCheck->setChecked(m_followTreeExpansion);
    m_followTreeCheck->setToolTip(QStringLiteral("On (default): the graph matches the folders expanded in the tree.\nOff: the graph shows the whole subtree of the current graph folder."));

    auto *addressRow = new QHBoxLayout;
    addressRow->setContentsMargins(0, 0, 0, 0);
    addressRow->addWidget(m_addressBar, 1);
    addressRow->addWidget(m_followTreeCheck, 0);
    layout->addLayout(addressRow);
#if defined(OPENTREE_HAVE_WEBENGINE)
    m_bridge->onActivate = [this](const QString &path) { activateNode(path); };
    m_bridge->onOpen = [this](const QString &path) { openNode(path); };
    m_bridge->onContextMenu = [this](const QString &path, int x, int y) { showNodeContextMenu(path, x, y); };
    m_channel->registerObject(QStringLiteral("graphBridge"), m_bridge);
#endif
    layout->addWidget(m_summaryLabel);

    connect(m_followTreeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_followTreeExpansion = checked;
        emit followTreeExpansionChanged(checked);
        markGraphDirty();
        if (m_batchDepth > 0) {
            return;
        }
        renderGraph();
    });

    setGraphData(QString(), {}, {});
}

void GraphPanel::ensureView()
{
    if (m_view) {
        return;
    }

#if defined(OPENTREE_HAVE_WEBENGINE)
    auto *view = new QWebEngineView(this);
    view->page()->setWebChannel(m_channel);
    m_view = view;
#else
    auto *view = new QTextBrowser(this);
    view->setReadOnly(true);
    m_view = view;
#endif

    // Place the view right under the summary label, where the constructor used to add it.
    if (auto *box = qobject_cast<QVBoxLayout *>(layout())) {
        const int summaryIndex = box->indexOf(m_summaryLabel);
        box->insertWidget(summaryIndex + 1, m_view, 1);
    }

    m_renderDirty = true;
    // renderGraph() runs from showEvent() so the page is loaded exactly once.
}

void GraphPanel::releaseView()
{
#if defined(OPENTREE_HAVE_WEBENGINE)
    if (!m_view) {
        return;
    }
    // Leaving the Graph tab destroys the WebEngine view. That shuts the renderer process
    // down and returns its memory (~100-150 MB) instead of merely freezing the page; the
    // view is recreated on the next visit. Chromium's in-process state stays loaded either
    // way, which is why the lazy first start matters more than this step.
    m_view->deleteLater();
    m_view = nullptr;
    m_renderDirty = true;
    Logger::info(QStringLiteral("graph-debug releaseView: graph tab hidden, WebEngine view released"));
#endif
}

void GraphPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    // Chromium is started here, on the first time the Graph tab is shown. It is deferred to
    // the next event-loop turn because creating and inserting a WebEngine view while the
    // show event is being delivered re-enters the layout/show handling.
    QTimer::singleShot(0, this, [this]() {
        const bool created = !m_view;
        ensureView();

#if defined(OPENTREE_HAVE_WEBENGINE)
        if (m_view && m_view->page()
            && m_view->page()->lifecycleState() == QWebEnginePage::LifecycleState::Discarded) {
            m_view->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
            m_renderDirty = true;
        }
#endif

        if (!m_renderDirty) {
            return;
        }

        if (created) {
            // Let the freshly created page finish initialising before the first load.
            QTimer::singleShot(400, this, [this]() {
                if (m_renderDirty) {
                    renderGraph();
                }
            });
            return;
        }

        renderGraph();
    });
}

void GraphPanel::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    releaseView();
}

void GraphPanel::setGraphData(const QString &rootPath, const QVector<TreeEntry> &entries, const QVector<SnapshotCompareRow> &compareRows)
{
    m_currentRootPath = rootPath;
    m_currentEntries = entries;
    m_currentCompareRows = compareRows;
    if (m_graphRootPath.isEmpty() || !isSameOrDescendant(m_graphRootPath, rootPath)) {
        m_graphRootPath = rootPath;
    }
    if (!isSameOrDescendant(m_selectedPath, rootPath)) {
        m_selectedPath.clear();
    }
    if (m_batchDepth > 0) {
        markGraphDirty();
        return;
    }
    renderGraph();
}

void GraphPanel::setNodeSizeMode(NodeSizeMode mode)
{
    if (m_nodeSizeMode == mode) {
        return;
    }

    m_nodeSizeMode = mode;
    markGraphDirty();
    if (m_batchDepth > 0) {
        return;
    }
    renderGraph();
}

void GraphPanel::setGraphRootPath(const QString &path)
{
    if (path.isEmpty() || !isSameOrDescendant(path, m_currentRootPath)) {
        return;
    }

    if (m_graphRootPath.compare(path, Qt::CaseInsensitive) == 0) {
        return;
    }

    m_graphRootPath = path;
    m_addressBar->setText(path);
    markGraphDirty();
    if (m_batchDepth > 0) {
        return;
    }
    renderGraph();
}

void GraphPanel::setSelectedPath(const QString &path)
{
    if (m_selectedPath.compare(path, Qt::CaseInsensitive) == 0) {
        return;
    }

    if (!path.isEmpty() && !isSameOrDescendant(path, m_graphRootPath)) {
        m_graphRootPath = m_currentRootPath;
    }

    m_selectedPath = path;
    markGraphDirty();
    if (m_batchDepth > 0 || m_suspendRender) {
        return;
    }
    refreshSelectionVisuals();
}

void GraphPanel::setVisiblePaths(const QStringList &paths)
{
    m_visiblePaths = paths;
    markGraphDirty();
    if (m_batchDepth > 0) {
        return;
    }
    renderGraph();
}

void GraphPanel::setOtherThresholdPercent(double percent)
{    const double clamped = std::max(0.0, percent);
    if (std::abs(m_otherThresholdPercent - clamped) < 0.0001) {
        return;
    }

    m_otherThresholdPercent = clamped;
    markGraphDirty();
    if (m_batchDepth > 0) {
        return;
    }
    renderGraph();
}

void GraphPanel::setFollowTreeExpansion(bool follow)
{
    if (m_followTreeExpansion == follow) {
        return;
    }

    m_followTreeExpansion = follow;
    if (m_followTreeCheck) {
        const QSignalBlocker blocker(m_followTreeCheck);
        m_followTreeCheck->setChecked(follow);
    }
    markGraphDirty();
    if (m_batchDepth > 0) {
        return;
    }
    renderGraph();
}

bool GraphPanel::followTreeExpansion() const
{
    return m_followTreeExpansion;
}

void GraphPanel::setMaxNodes(int maxNodes)
{
    const int clamped = std::clamp(maxNodes, 20, 600);
    if (m_maxNodes == clamped) {
        return;
    }
    m_maxNodes = clamped;
    markGraphDirty();
    if (m_batchDepth > 0) {
        return;
    }
    renderGraph();
}

void GraphPanel::beginBatchUpdate()
{
    ++m_batchDepth;
}

void GraphPanel::endBatchUpdate()
{
    if (m_batchDepth > 0) {
        --m_batchDepth;
    }
    if (m_batchDepth == 0 && m_renderDirty) {
        m_renderDirty = false;
        renderGraph();
    }
}

void GraphPanel::markGraphDirty()
{
    m_renderDirty = true;
}

void GraphPanel::activateNode(const QString &path)
{
    const TreeEntry *entry = findEntryByPath(path);
    if (!entry) {
        Logger::warning(QStringLiteral("graph-debug activateNode missing path=%1")
                            .arg(path));
        return;
    }

    Logger::info(QStringLiteral("graph-debug activateNode kind=%1 path=%2 graphRoot=%3 selected=%4")
                     .arg(entry->kind == TreeEntryKind::Folder ? QStringLiteral("folder") : QStringLiteral("file"))
                     .arg(entry->path)
                     .arg(m_graphRootPath)
                     .arg(m_selectedPath));

    m_suspendRender = true;
    m_selectedPath = entry->path;
    emit nodeActivated(entry->path);
    emit entryActivated(*entry);
    m_suspendRender = false;
    markGraphDirty();
    refreshSelectionVisuals();
}

void GraphPanel::openNode(const QString &path)
{
    if (path == QStringLiteral("__up__")) {
        const TreeEntry *entry = findEntryByPath(m_graphRootPath);
        const TreeEntry *parentEntry = entry ? findEntryByPath(entry->parentPath) : nullptr;
        if (parentEntry) {
            Logger::info(QStringLiteral("graph-debug openNode up from=%1 to=%2")
                             .arg(m_graphRootPath)
                             .arg(parentEntry->path));
            m_suspendRender = true;
            m_selectedPath = parentEntry->path;
            emit entryOpened(*parentEntry);
            m_suspendRender = false;
            markGraphDirty();
            renderGraph();
        }
        return;
    }

    const TreeEntry *entry = findEntryByPath(path);
    if (!entry) {
        Logger::warning(QStringLiteral("graph-debug openNode missing path=%1")
                            .arg(path));
        return;
    }

    Logger::info(QStringLiteral("graph-debug openNode kind=%1 path=%2 graphRoot=%3 selected=%4")
                     .arg(entry->kind == TreeEntryKind::Folder ? QStringLiteral("folder") : QStringLiteral("file"))
                     .arg(entry->path)
                     .arg(m_graphRootPath)
                     .arg(m_selectedPath));

    m_suspendRender = true;
    m_selectedPath = entry->path;
    if (entry->kind == TreeEntryKind::Folder) {
        emit entryOpened(*entry);
    } else {
        emit entryActivated(*entry);
    }
    m_suspendRender = false;
    markGraphDirty();
    renderGraph();
}

namespace {

int sizePlanetStyle(const QVector<double> &sortedSizes, double metric)
{
    if (sortedSizes.isEmpty()) {
        return 2;
    }

    int count = 0;
    for (double s : sortedSizes) {
        if (s <= metric) {
            ++count;
        }
    }
    const double pct = double(count) / double(sortedSizes.size());
    if (pct >= 0.80) return 4;
    if (pct >= 0.60) return 3;
    if (pct >= 0.40) return 2;
    if (pct >= 0.20) return 1;
    return 0;
}

}

void GraphPanel::showNodeContextMenu(const QString &nodeId, int screenX, int screenY)
{
    const TreeEntry *entry = findEntryByPath(nodeId);
    if (!entry && (nodeId == QStringLiteral("__other_folders__") || nodeId == QStringLiteral("__other_files__"))) {
        entry = findEntryByPath(m_graphRootPath);
    }
    if (!entry) {
        return;
    }

    QMenu menu(this);
    QAction *pieAction = menu.addAction(QStringLiteral("View in Pie Chart"));
    QAction *barsAction = menu.addAction(QStringLiteral("View in Bar Chart"));
    QAction *treemapAction = menu.addAction(QStringLiteral("View in Treemap"));
    menu.addSeparator();
    QAction *extensionsAction = menu.addAction(QStringLiteral("View in Extensions"));
    QAction *heatmapAction = menu.addAction(QStringLiteral("View in Heatmap"));
    menu.addSeparator();
    const SharedEntryActions shared = addSharedEntryActions(menu);
    QAction *selected = menu.exec(QPoint(screenX, screenY));
    if (!selected) {
        return;
    }

    if (runSharedEntryAction(this, selected, *entry, shared)) {
        return;
    }

    QString tab;
    if (selected == pieAction) {
        tab = QStringLiteral("pie");
    } else if (selected == barsAction) {
        tab = QStringLiteral("bars");
    } else if (selected == treemapAction) {
        tab = QStringLiteral("treemap");
    } else if (selected == extensionsAction) {
        tab = QStringLiteral("extensions");
    } else if (selected == heatmapAction) {
        tab = QStringLiteral("heatmap");
    }
    if (!tab.isEmpty()) {
        emit viewInTabRequested(*entry, tab);
    }
}

const TreeEntry *GraphPanel::findEntryByPath(const QString &path) const
{
    for (const TreeEntry &entry : m_currentEntries) {
        if (entry.path.compare(path, Qt::CaseInsensitive) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

void GraphPanel::handleAddressSubmitted()
{
    const QString path = m_addressBar->text().trimmed();
    if (!path.isEmpty()) {
        emit pathEntered(path);
    }
}

void GraphPanel::renderGraph()
{
    m_renderDirty = false;
    Logger::info(QStringLiteral("graph-debug renderGraph root=%1 selected=%2 entries=%3 visibleCount=%4 compareRows=%5")
                     .arg(m_graphRootPath)
                     .arg(m_selectedPath)
                     .arg(m_currentEntries.size())
                     .arg(m_visiblePaths.size())
                     .arg(m_currentCompareRows.size()));
    if (m_addressBar->text().compare(m_graphRootPath, Qt::CaseInsensitive) != 0) {
        m_addressBar->setText(m_graphRootPath);
    }
    if (!m_view) {
        // The graph tab has not been shown yet: keep the payload pending and render it on
        // the first show, so Chromium is never started for users who do not open the graph.
        m_renderDirty = true;
        return;
    }
    if (m_currentEntries.isEmpty()) {
        m_summaryLabel->setText("Graph: scan a folder to visualize its structure.");

#if defined(OPENTREE_HAVE_WEBENGINE)
        m_view->setHtml(buildEmptyHtml(), QUrl("https://local.opentree/"));
#else
        m_view->setText("vis.js graph requires the MSVC WebEngine build. Use build_msvc.bat and run build-msvc/OpenTree.exe.");
#endif
        return;
    }

#if defined(OPENTREE_HAVE_WEBENGINE)
    QString html = buildHtml();
    html.replace("__GRAPH_DATA__", buildGraphPayload(m_graphRootPath, m_currentEntries, m_currentCompareRows));
    m_view->setHtml(html, QUrl("https://local.opentree/"));
#else
    QVector<TreeEntry> listed;
    for (const TreeEntry &entry : m_currentEntries) {
        if (isSameOrDescendant(entry.path, m_graphRootPath)) {
            listed.push_back(entry);
        }
    }
    std::sort(listed.begin(), listed.end(), [](const TreeEntry &left, const TreeEntry &right) {
        return left.size > right.size;
    });
    if (listed.size() > 40) {
        listed.resize(40);
    }

    QStringList lines;
    lines << QStringLiteral("Top items for %1").arg(m_graphRootPath);
    for (const TreeEntry &entry : listed) {
        lines << QStringLiteral("- %1 (%2)").arg(entry.path, SizeFormatter::formatBytes(entry.size));
    }
    m_view->setText(lines.join('\n'));
#endif

    int folderCount = 0;
    for (const TreeEntry &entry : m_currentEntries) {
        if (entry.kind == TreeEntryKind::Folder && isSameOrDescendant(entry.path, m_graphRootPath)) {
            ++folderCount;
        }
    }

    const bool isDrilledIn = m_graphRootPath.compare(m_currentRootPath, Qt::CaseInsensitive) != 0;
    m_summaryLabel->setText(QStringLiteral("Graph: %1 folders shown for %2 | other cutoff %3% | %4%5%6")
                                .arg(folderCount)
                                .arg(m_graphRootPath)
                                .arg(QString::number(m_otherThresholdPercent, 'f', 1))
                                .arg(m_followTreeExpansion ? QStringLiteral("following tree") : QStringLiteral("whole subtree"))
                                .arg(m_currentCompareRows.isEmpty() ? QString() : QStringLiteral(" | delta colors active"))
                                .arg(isDrilledIn ? QStringLiteral(" | drilled in") : QString()));

    m_renderedRoot = m_graphRootPath;
    m_renderedEntryCount = m_currentEntries.size();
}

void GraphPanel::refreshSelectionVisuals()
{
    // Selecting a node only changes colours, so update them inside the page instead of
    // rebuilding the whole graph (which used to reset the camera and re-run the layout).
    if (m_renderedRoot != m_graphRootPath || m_renderedEntryCount != m_currentEntries.size()) {
        renderGraph();
        return;
    }

#if defined(OPENTREE_HAVE_WEBENGINE)
    updateSelectionInView();
#endif
}

void GraphPanel::updateSelectionInView()
{
#if defined(OPENTREE_HAVE_WEBENGINE)
    if (!m_view || !m_view->page()) {
        return;
    }

    const QString argument = m_selectedPath.isEmpty()
        ? QStringLiteral("null")
        : QStringLiteral("'%1'").arg(escapeJsString(m_selectedPath));
    m_view->page()->runJavaScript(
        QStringLiteral("if (typeof applySelectionState === 'function') { applySelectionState(%1); }").arg(argument));
#endif
}

QString GraphPanel::buildEmptyHtml() const
{
        return QStringLiteral(
        R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>OpenTree Graph</title>
<style>
  body {
    margin: 0;
    background: radial-gradient(circle at top, #1a2030 0%, #11131a 58%, #0b0d12 100%);
    color: #d8e0ef;
    font-family: "Segoe UI", sans-serif;
    display: flex;
    align-items: center;
    justify-content: center;
    height: 100vh;
  }
  .empty {
    padding: 16px 18px;
    border: 1px solid #2f3950;
    border-radius: 12px;
    background: rgba(16, 19, 27, 0.86);
  }
</style>
</head>
<body>
<div class="empty">Scan a folder, then open Graph.</div>
</body>
</html>
)HTML"
    );
}

QString GraphPanel::buildGraphPayload(const QString &rootPath, const QVector<TreeEntry> &entries, const QVector<SnapshotCompareRow> &compareRows) const
{
    Logger::info(QStringLiteral("graph-debug buildGraphPayload root=%1 entries=%2 visibleCount=%3 selected=%4 compareRows=%5")
                     .arg(rootPath)
                     .arg(entries.size())
                     .arg(m_visiblePaths.size())
                     .arg(m_selectedPath)
                     .arg(compareRows.size()));
    QHash<QString, qint64> deltaByPath;
    for (const SnapshotCompareRow &row : compareRows) {
        deltaByPath.insert(row.path, row.deltaBytes);
    }

    QVector<TreeEntry> folders;
    QVector<TreeEntry> directFolders;
    QVector<TreeEntry> directFiles;

    // Build the tree-visibility lookup once (lowercased keys for case-insensitive membership).
    const bool filterByTree = m_followTreeExpansion && !m_visiblePaths.isEmpty();
    QSet<QString> visiblePathSet;
    if (filterByTree) {
        visiblePathSet.reserve(m_visiblePaths.size());
        for (const QString &path : m_visiblePaths) {
            visiblePathSet.insert(path.toLower());
        }
    }
    TreeEntry rootEntry;
    bool hasRootEntry = false;
    bool hasUpNode = false;
    QString upTargetPath;
    QVector<TreeEntry> selectedAncestors;
    for (const TreeEntry &entry : entries) {
        if (!isSameOrDescendant(entry.path, rootPath)) {
            continue;
        }

        const bool isRootFolder = entry.kind == TreeEntryKind::Folder
            && entry.path.compare(rootPath, Qt::CaseInsensitive) == 0;
        // Case-insensitive set lookup: a QStringList::contains() here was O(n) per entry
        // and made expanding a large tree quadratic.
        const bool hiddenByTreePruning = filterByTree
            && entry.kind == TreeEntryKind::Folder
            && !isRootFolder
            && !visiblePathSet.contains(entry.path.toLower());
        if (hiddenByTreePruning) {
            continue;
        }

        if (isRootFolder) {
            rootEntry = entry;
            hasRootEntry = true;
            if (!entry.parentPath.isEmpty()) {
                hasUpNode = true;
                upTargetPath = entry.parentPath;
            }
            continue;
        }

        if (entry.kind == TreeEntryKind::Folder && entry.parentPath.compare(rootPath, Qt::CaseInsensitive) == 0) {
            directFolders.push_back(entry);
        }

        if (entry.kind == TreeEntryKind::Folder) {
            folders.push_back(entry);
        } else if (entry.parentPath.compare(rootPath, Qt::CaseInsensitive) == 0) {
            directFiles.push_back(entry);
        }

        if (!m_selectedPath.isEmpty() && pathIsAncestorOf(entry.path, m_selectedPath)) {
            selectedAncestors.push_back(entry);
        }
    }

    std::sort(folders.begin(), folders.end(), [](const TreeEntry &left, const TreeEntry &right) {
        return left.size > right.size;
    });
    std::sort(directFolders.begin(), directFolders.end(), [](const TreeEntry &left, const TreeEntry &right) {
        return left.size > right.size;
    });
    std::sort(directFiles.begin(), directFiles.end(), [](const TreeEntry &left, const TreeEntry &right) {
        return left.size > right.size;
    });
    const int folderCap = std::max(4, m_maxNodes - 1);
    if (folders.size() > (hasRootEntry ? folderCap - 1 : folderCap)) {
        folders.resize(hasRootEntry ? folderCap - 1 : folderCap);
    }
    if (hasRootEntry) {
        folders.prepend(rootEntry);
    }

    for (const TreeEntry &entry : selectedAncestors) {
        bool exists = false;
        for (const TreeEntry &existing : folders) {
            if (existing.path.compare(entry.path, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            folders.push_back(entry);
        }
    }

    QStringList nodeJson;
    QStringList edgeJson;
    QSet<QString> allowedPaths;

    QVector<TreeEntry> keptDirectFolders;
    qint64 otherFolderBytes = 0;
    for (const TreeEntry &entry : directFolders) {
        const double percent = rootEntry.size <= 0 ? 0.0 : (100.0 * double(entry.size) / double(rootEntry.size));
        const bool selectedBranch = !m_selectedPath.isEmpty() && pathIsAncestorOf(entry.path, m_selectedPath);
        if ((keptDirectFolders.size() < 10 && percent >= m_otherThresholdPercent) || selectedBranch) {
            keptDirectFolders.push_back(entry);
            allowedPaths.insert(entry.path);
        } else {
            otherFolderBytes += entry.size;
        }
    }

    QVector<TreeEntry> keptDirectFiles;
    qint64 otherFileBytes = 0;
    for (const TreeEntry &entry : directFiles) {
        const double percent = rootEntry.size <= 0 ? 0.0 : (100.0 * double(entry.size) / double(rootEntry.size));
        const bool selectedFile = !m_selectedPath.isEmpty() && entry.path.compare(m_selectedPath, Qt::CaseInsensitive) == 0;
        if ((keptDirectFiles.size() < 8 && percent >= m_otherThresholdPercent) || selectedFile) {
            keptDirectFiles.push_back(entry);
        } else {
            otherFileBytes += entry.size;
        }
    }

    allowedPaths.insert(rootPath);
    for (const TreeEntry &entry : folders) {
        bool keep = entry.path.compare(rootPath, Qt::CaseInsensitive) == 0;
        for (const TreeEntry &directFolder : keptDirectFolders) {
            if (isSameOrDescendant(entry.path, directFolder.path)) {
                keep = true;
                break;
            }
        }
        const bool selectedAncestor = !m_selectedPath.isEmpty() && pathIsAncestorOf(entry.path, m_selectedPath);
        if (keep || selectedAncestor) {
            allowedPaths.insert(entry.path);
        }
    }

    double minMetric = 0.0;
    double maxMetric = 0.0;
    bool firstMetric = true;
    for (const TreeEntry &entry : folders) {
        if (!allowedPaths.contains(entry.path)) {
            continue;
        }
        const double metric = nodeMetric(entry, m_nodeSizeMode);
        if (firstMetric) {
            minMetric = metric;
            maxMetric = metric;
            firstMetric = false;
        } else {
            minMetric = std::min(minMetric, metric);
            maxMetric = std::max(maxMetric, metric);
        }
    }

    // ponytail: inline SVG planet data URIs with style variations
    auto makePlanet = [](const QString &light, const QString &dark, bool ringed, int style) -> QString {
        const int c = ringed ? 28 : 24;
        QString vs = QString::number(c * 2);
        QPointF sp(c - 6, c - 8);
        QString s = QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 %1 %1'>"
            "<defs><radialGradient id='g' cx='35%' cy='30%' r='60%'>"
            "<stop offset='0%' stop-color='%2'/>"
            "<stop offset='100%' stop-color='%3'/>"
            "</radialGradient></defs>"
            "<circle cx='%4' cy='%4' r='20' fill='url(#g)'/>"
            "<ellipse cx='%5' cy='%6' rx='7' ry='4' fill='rgba(255,255,255,0.22)' transform='rotate(-25 %5 %6)'/>"
        ).arg(vs, light, dark).arg(c).arg(sp.x()).arg(sp.y());
        if (style == 2) {
            for (int y = -10; y <= 10; y += 5)
                s += QStringLiteral("<ellipse cx='%1' cy='%2' rx='19' ry='1.5' fill='rgba(255,255,255,0.1)'/>")
                    .arg(c).arg(c + y);
        }
        if (style == 3) {
            s += QStringLiteral("<path d='M%1 %2 Q%3 %4 %5 %6 Q%7 %8 %9 %10 Z' fill='rgba(60,200,60,0.2)'/>")
                .arg(c - 12).arg(c - 4).arg(c - 8).arg(c - 10).arg(c - 4).arg(c - 8)
                .arg(c).arg(c - 6).arg(c - 10).arg(c - 2);
            s += QStringLiteral("<path d='M%1 %2 Q%3 %4 %5 %6 Q%7 %8 %9 %10 Z' fill='rgba(60,200,60,0.15)'/>")
                .arg(c + 2).arg(c - 10).arg(c + 6).arg(c - 14).arg(c + 8).arg(c - 10)
                .arg(c + 6).arg(c - 6).arg(c + 2).arg(c - 8);
        }
        if (style == 4) {
            s += QStringLiteral("<circle cx='%1' cy='%2' r='6' fill='rgba(0,0,0,0.15)'/>").arg(c - 6).arg(c - 8);
            s += QStringLiteral("<circle cx='%1' cy='%2' r='4' fill='rgba(0,0,0,0.12)'/>").arg(c + 8).arg(c + 4);
            s += QStringLiteral("<circle cx='%1' cy='%2' r='3' fill='rgba(0,0,0,0.1)'/>").arg(c - 2).arg(c + 10);
            s += QStringLiteral("<circle cx='%1' cy='%2' r='2' fill='rgba(0,0,0,0.08)'/>").arg(c + 4).arg(c - 12);
        }
        if (ringed) {
            s += QStringLiteral("<ellipse cx='%1' cy='%1' rx='26' ry='6' fill='none' stroke='rgba(255,255,255,0.3)' stroke-width='1.5' transform='rotate(-15 %1 %1)'/>")
                .arg(c);
        }
        s += QStringLiteral("</svg>");
        return QStringLiteral("data:image/svg+xml;base64,") + QString::fromLatin1(s.toUtf8().toBase64());
    };

    if (hasUpNode) {
        const QString upLabel = rootPath == m_currentRootPath ? QStringLiteral("Root") : QStringLiteral("Up");
        const QString upTitle = escapeJsString(QStringLiteral("Go to %1").arg(upTargetPath));
        QString moonSvg = QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'>"
            "<circle cx='24' cy='24' r='20' fill='#8890A0'/>"
            "<circle cx='20' cy='17' r='5' fill='rgba(0,0,0,0.15)'/>"
            "<circle cx='29' cy='27' r='3' fill='rgba(0,0,0,0.12)'/>"
            "<circle cx='22' cy='30' r='4' fill='rgba(0,0,0,0.1)'/>"
            "</svg>"
        );
        const QString moonImg = escapeJsString(QStringLiteral("data:image/svg+xml;base64,") + QString::fromLatin1(moonSvg.toUtf8().toBase64()));
        nodeJson << QStringLiteral("{id:'__up__',label:%1,name:%1,title:%2,size:20,shape:'circularImage',image:'%3',borderWidth:1,color:{background:'#1E2740',border:'#5A7AD6'},font:{color:'#D0E0FF'}}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(upLabel)))
                        .arg(QStringLiteral("'%1'").arg(upTitle))
                        .arg(moonImg);
    }

    // Size percentile → planet style: 0=moon,1=rocky,2=earth,3=giant,4=ringed
    QVector<double> sizeMetrics;
    for (const TreeEntry &entry : folders) {
        if (allowedPaths.contains(entry.path))
            sizeMetrics.append(nodeMetric(entry, NodeSizeMode::Size));
    }
    std::sort(sizeMetrics.begin(), sizeMetrics.end());

    double minFileMetric = 0.0;
    double maxFileMetric = 0.0;
    bool firstFileMetric = true;
    for (const TreeEntry &entry : keptDirectFiles) {
        const double metric = entry.size / (1024.0 * 1024.0);
        if (firstFileMetric) {
            minFileMetric = metric;
            maxFileMetric = metric;
            firstFileMetric = false;
        } else {
            minFileMetric = std::min(minFileMetric, metric);
            maxFileMetric = std::max(maxFileMetric, metric);
        }
    }

    static const QString styleLight[] = { QStringLiteral("#8890A0"), QStringLiteral("#E07060"), QStringLiteral("#59A14F"), QStringLiteral("#00D4FF"), QStringLiteral("#FFD700") };
    static const QString styleDark[]  = { QStringLiteral("#505868"), QStringLiteral("#802030"), QStringLiteral("#207030"), QStringLiteral("#0066AA"), QStringLiteral("#E87D00") };
    static const bool styleRing[]     = { false, false, false, false, true };

    // Only the largest folders keep a permanent label; the rest reveal their name on
    // hover so dense graphs stay readable.
    QVector<TreeEntry> labelCandidates;
    for (const TreeEntry &entry : folders) {
        if (allowedPaths.contains(entry.path)) {
            labelCandidates.push_back(entry);
        }
    }
    std::sort(labelCandidates.begin(), labelCandidates.end(), [](const TreeEntry &left, const TreeEntry &right) {
        return left.size > right.size;
    });
    QSet<QString> labeledPaths;
    const int labelBudget = 24;
    for (int i = 0; i < labelCandidates.size() && i < labelBudget; ++i) {
        labeledPaths.insert(labelCandidates[i].path);
    }
    if (!m_selectedPath.isEmpty()) {
        labeledPaths.insert(m_selectedPath);
    }

    for (const TreeEntry &entry : folders) {
        if (!allowedPaths.contains(entry.path)) {
            continue;
        }
        const qint64 delta = deltaByPath.value(entry.path, 0);
        bool sel = !m_selectedPath.isEmpty() && entry.path.compare(m_selectedPath, Qt::CaseInsensitive) == 0;
        bool anc = !sel && !m_selectedPath.isEmpty() && pathIsAncestorOf(entry.path, m_selectedPath);

        const double sizeMetric = nodeMetric(entry, NodeSizeMode::Size);
        const int pStyle = sizePlanetStyle(sizeMetrics, sizeMetric);
        QString planetImg = makePlanet(styleLight[pStyle], styleDark[pStyle], styleRing[pStyle], pStyle);

        QString color, borderColor;
        if (sel) {
            color = "#FFD700"; borderColor = "#FFE066";
        } else if (anc) {
            color = "#B388FF"; borderColor = "#CCAAFF";
        } else if (delta > 0) {
            color = "#FF4081"; borderColor = "#FF80AB";
        } else if (delta < 0) {
            color = "#39FF14"; borderColor = "#80FF60";
        } else {
            color = styleLight[pStyle]; borderColor = styleDark[pStyle];
        }

        const double size = normalizedNodeSize(nodeMetric(entry, m_nodeSizeMode), minMetric, maxMetric);
        const QString escapedPath = escapeJsString(entry.path);
        const QString escapedName = escapeJsString(entry.name);
        const QString escapedTitle = escapeJsString(QStringLiteral("%1\nSize: %2\nDelta: %3")
                                                        .arg(entry.path, SizeFormatter::formatBytes(entry.size), QString::number(delta)));
        const QString escapedImg = escapeJsString(planetImg);
        const QString shownLabel = labeledPaths.contains(entry.path) ? escapedName : QString();
        nodeJson << QStringLiteral("{id:%1,label:'%2',name:'%3',title:%4,size:%5,borderWidth:%6,shape:'circularImage',image:'%7',color:{background:%8,border:%9}}")
                        .arg(QStringLiteral("'%1'").arg(escapedPath))
                        .arg(shownLabel, escapedName)
                        .arg(QStringLiteral("'%1'").arg(escapedTitle))
                        .arg(size)
                        .arg(sel ? QStringLiteral("3") : QStringLiteral("1.5"))
                        .arg(escapedImg)
                        .arg(QStringLiteral("'%1'").arg(color))
                        .arg(QStringLiteral("'%1'").arg(borderColor));

        if (!entry.parentPath.isEmpty() && allowedPaths.contains(entry.parentPath)) {
            edgeJson << QStringLiteral("{from:%1,to:%2}")
                            .arg(QStringLiteral("'%1'").arg(escapeJsString(entry.parentPath)))
                            .arg(QStringLiteral("'%1'").arg(escapedPath));
        }
    }

    for (const TreeEntry &entry : keptDirectFiles) {
        const qint64 delta = deltaByPath.value(entry.path, 0);
        const bool sel = !m_selectedPath.isEmpty() && entry.path.compare(m_selectedPath, Qt::CaseInsensitive) == 0;
        const double size = normalizedNodeSize(entry.size / (1024.0 * 1024.0), minFileMetric, maxFileMetric);

        QString color = QStringLiteral("#4DD0E1");
        QString borderColor = QStringLiteral("#80DEEA");
        if (sel) {
            color = QStringLiteral("#FFD700"); borderColor = QStringLiteral("#FFE066");
        } else if (delta > 0) {
            color = QStringLiteral("#FF4081"); borderColor = QStringLiteral("#FF80AB");
        } else if (delta < 0) {
            color = QStringLiteral("#39FF14"); borderColor = QStringLiteral("#80FF60");
        }

        const QString escapedPath = escapeJsString(entry.path);
        const QString escapedName = escapeJsString(entry.name);
        const QString escapedTitle = escapeJsString(QStringLiteral("%1\nSize: %2\nDelta: %3")
                                                        .arg(entry.path, SizeFormatter::formatBytes(entry.size), QString::number(delta)));
        nodeJson << QStringLiteral("{id:%1,label:'%2',name:'%3',title:%4,size:%5,shape:'diamond',borderWidth:%6,color:{background:'%7',border:'%8'}}")
                        .arg(QStringLiteral("'%1'").arg(escapedPath))
                        .arg(escapedName, escapedName)
                        .arg(QStringLiteral("'%1'").arg(escapedTitle))
                        .arg(size)
                        .arg(sel ? QStringLiteral("3") : QStringLiteral("1.5"))
                        .arg(color, borderColor);

        edgeJson << QStringLiteral("{from:%1,to:%2}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(rootPath)))
                        .arg(QStringLiteral("'%1'").arg(escapedPath));
    }

    if (otherFolderBytes > 0) {
        QString asteroidSvg = QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'>"
            "<polygon points='24,8 36,16 34,34 14,34 10,16' fill='#4A5A6A' stroke='#6A8AAA' stroke-width='1'/>"
            "<circle cx='20' cy='22' r='2.5' fill='rgba(0,0,0,0.18)'/>"
            "<circle cx='30' cy='26' r='2' fill='rgba(0,0,0,0.14)'/>"
            "<circle cx='24' cy='32' r='1.5' fill='rgba(0,0,0,0.1)'/>"
            "</svg>"
        );
        const QString asteroidImg = escapeJsString(QStringLiteral("data:image/svg+xml;base64,") + QString::fromLatin1(asteroidSvg.toUtf8().toBase64()));
        nodeJson << QStringLiteral("{id:'__other_folders__',label:'Other folders',name:'Other folders',title:%1,size:20,shape:'circularImage',image:'%2',borderWidth:1.5,color:{background:'#3A4A5A',border:'#6A8AAA'}}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(QStringLiteral("Other direct folders under %1\nSize: %2").arg(rootPath, SizeFormatter::formatBytes(otherFolderBytes)))))
                        .arg(asteroidImg);
        edgeJson << QStringLiteral("{from:%1,to:'__other_folders__',dashes:true}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(rootPath)));
    }

    if (otherFileBytes > 0) {
        nodeJson << QStringLiteral("{id:'__other_files__',label:'Other files',name:'Other files',title:%1,size:18,shape:'diamond',borderWidth:1.5,color:{background:'#455A64',border:'#90A4AE'}}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(QStringLiteral("Other direct files under %1\nSize: %2").arg(rootPath, SizeFormatter::formatBytes(otherFileBytes)))));
        edgeJson << QStringLiteral("{from:%1,to:'__other_files__',dashes:true}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(rootPath)));
    }

    Logger::info(QStringLiteral("graph-debug payloadSummary root=%1 directFolders=%2 keptDirectFolders=%3 directFiles=%4 keptDirectFiles=%5 allowedPaths=%6 otherFolderBytes=%7 otherFileBytes=%8 nodes=%9 edges=%10")
                     .arg(rootPath)
                     .arg(directFolders.size())
                     .arg(keptDirectFolders.size())
                     .arg(directFiles.size())
                     .arg(keptDirectFiles.size())
                     .arg(allowedPaths.size())
                     .arg(otherFolderBytes)
                     .arg(otherFileBytes)
                     .arg(nodeJson.size())
                     .arg(edgeJson.size()));

    return QStringLiteral("const GRAPH_DATA={nodes:[%1],edges:[%2],root:%3,selected:%4};")
        .arg(nodeJson.join(','),
             edgeJson.join(','),
             QStringLiteral("'%1'").arg(escapeJsString(rootPath)),
             m_selectedPath.isEmpty() ? QStringLiteral("null") : QStringLiteral("'%1'").arg(escapeJsString(m_selectedPath)));
}

QString GraphPanel::buildHtml() const
{
    return QStringLiteral(
        R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>OpenTree Graph</title>
<script src="qrc:///js/vis-network.min.js"></script>
<script src="qrc:///qtwebchannel/qwebchannel.js"></script>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: radial-gradient(ellipse 140% 90% at 30% 20%, #101624 0%, #0a0d16 55%, #05070e 100%);
    color: #d0d8ee; font-family: "Segoe UI", system-ui, sans-serif;
    height: 100vh; overflow: hidden;
  }
  #bgCanvas { position: fixed; top: 0; left: 0; width: 100%; height: 100%; z-index: 0; pointer-events: none; }
  #graph { position: relative; width: 100vw; height: 100vh; z-index: 1; }
  #toolbar {
    position: fixed; top: 12px; right: 12px; z-index: 30;
    display: flex; gap: 6px; align-items: center;
    padding: 6px 8px;
    border: 1px solid rgba(70, 90, 130, 0.5);
    border-radius: 12px;
    background: rgba(8, 12, 22, 0.78);
    backdrop-filter: blur(6px);
    box-shadow: 0 4px 24px rgba(0,0,0,0.4);
  }
  #toolbar button {
    border: 1px solid rgba(80, 110, 160, 0.5);
    background: rgba(20, 28, 46, 0.85);
    color: #b8c8e8;
    font-size: 12px; font-family: inherit;
    padding: 4px 9px; border-radius: 8px; cursor: pointer;
  }
  #toolbar button:hover { background: rgba(40, 56, 88, 0.95); color: #e0eaff; }
  #toolbar button.active { background: #2f5db0; color: #ffffff; border-color: #5b8ae0; }
  #toolbar .hint { color: #6a7a98; font-size: 11px; margin-right: 2px; }
  #legend {
    position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%);
    z-index: 20;
    padding: 8px 18px;
    border: 1px solid rgba(70, 90, 130, 0.5);
    border-radius: 14px;
    background: rgba(8, 12, 22, 0.78);
    backdrop-filter: blur(6px);
    font-size: 12px; color: #a8b8d8;
    white-space: nowrap;
    box-shadow: 0 4px 24px rgba(0,0,0,0.4);
  }
  #legend b { color: #d0e0ff; }
  #legend .sep { color: #3a4860; margin: 0 6px; }
</style>
</head>
<body>
<canvas id="bgCanvas"></canvas>
<div id="graph"></div>
  <div id="toolbar">
    <span class="hint">layout</span>
    <button data-layout="force">Force</button>
    <button data-layout="tree">Tree</button>
    <span class="hint">|</span>
    <button id="btnHoverFocus" class="active" title="Zoom to the node you rest the pointer on. Press Escape to zoom back out.">Hover focus</button>
    <span class="hint">|</span>
    <button id="btnFit">Fit</button>
    <button id="btnZoomOut">−</button>
    <button id="btnZoomIn">+</button>
    <button id="btnRelayout">Re-layout</button>
  </div>
  <div id="legend"><b>Graph</b><span class="sep">·</span>click: select<span class="sep">·</span>double-click: enter folder<span class="sep">·</span>right-click: more<span class="sep">·</span><span style="color:#6a7a98;">planet = folder</span><span class="sep">·</span><span style="color:#6a7a98;">◆ = file</span></div>
<script>
__GRAPH_DATA__

// galaxy particle system
(function() {
  const c = document.getElementById('bgCanvas');
  const ctx = c.getContext('2d');
  let W = 1, H = 1, dpr = 1;

  const particles = [];
  const colors = ['80,180,255','160,200,255','180,160,255','200,220,255','120,200,255'];

  function seedParticles() {
    particles.length = 0;
    // 90% static specks
    for (let i = 0; i < 270; ++i) {
      particles.push({
        x: Math.random() * W, y: Math.random() * H,
        size: 1 + Math.random() * 1.5,
        alpha: 0.08 + Math.random() * 0.2,
        color: colors[Math.floor(Math.random() * colors.length)],
        isStatic: true,
      });
    }
    // 10% drifting specks
    for (let i = 0; i < 30; ++i) {
      const angle = Math.random() * Math.PI * 2;
      particles.push({
        x: Math.random() * W, y: Math.random() * H,
        vx: Math.cos(angle) * (0.2 + Math.random() * 0.5),
        vy: Math.sin(angle) * (0.2 + Math.random() * 0.5),
        size: 1.5 + Math.random() * 2,
        alpha: 0.1 + Math.random() * 0.3,
        color: colors[Math.floor(Math.random() * colors.length)],
        isStatic: false,
      });
    }
    // bigger star particles that pulse
    for (let i = 0; i < 12; ++i) {
      const angle = Math.random() * Math.PI * 2;
      particles.push({
        x: Math.random() * W, y: Math.random() * H,
        vx: Math.cos(angle) * (0.05 + Math.random() * 0.1),
        vy: Math.sin(angle) * (0.05 + Math.random() * 0.1),
        size: 4 + Math.random() * 3,
        alpha: 0.25 + Math.random() * 0.25,
        color: '200,220,255',
        isStatic: false,
        pulse: 0.015 + Math.random() * 0.025,
        pulsePhase: Math.random() * Math.PI * 2,
      });
    }
  }

  function resize() {
    W = Math.max(1, window.innerWidth);
    H = Math.max(1, window.innerHeight);
    dpr = window.devicePixelRatio || 1;
    c.width = Math.max(1, Math.floor(W * dpr));
    c.height = Math.max(1, Math.floor(H * dpr));
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    seedParticles();
  }
  resize();
  window.addEventListener('resize', resize);

  function frame() {
    // Full clear every frame: a low-alpha fade left permanent motion-blur streaks
    // behind the drifting specks, which cluttered the graph.
    ctx.clearRect(0, 0, W, H);
    for (const p of particles) {
      if (!p.isStatic) {
        p.x += p.vx;
        p.y += p.vy;
        if (p.x < -20) p.x = W + 20; if (p.x > W + 20) p.x = -20;
        if (p.y < -20) p.y = H + 20; if (p.y > H + 20) p.y = -20;
      }
      let a = p.alpha;
      if (p.pulse) a = p.alpha * (0.5 + 0.5 * Math.sin(p.pulsePhase + Date.now() * p.pulse));
      if (!isFinite(a) || a <= 0) continue;
      ctx.beginPath();
      ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(' + p.color + ',' + a.toFixed(3) + ')';
      ctx.fill();
    }
    requestAnimationFrame(frame);
  }
  frame();
})();

let graphBridge = null;
new QWebChannel(qt.webChannelTransport, function(channel) {
  graphBridge = channel.objects.graphBridge;
});
const container = document.getElementById('graph');
const nodes = new vis.DataSet(GRAPH_DATA.nodes);
const edges = new vis.DataSet(GRAPH_DATA.edges);

const nodeMeta = {};
GRAPH_DATA.nodes.forEach(n => {
  nodeMeta[n.id] = {
    shown: n.label || '',
    name: n.name || n.label || '',
    baseColor: (n.color && n.color.background) ? n.color.background : '#59A14F',
    baseBorder: (n.color && n.color.border) ? n.color.border : '#207030',
    baseWidth: typeof n.borderWidth === 'number' ? n.borderWidth : 1.5,
  };
});

function isAncestorPath(ancestor, path) {
  if (!ancestor || !path) return false;
  if (ancestor.toLowerCase() === path.toLowerCase()) return true;
  const lowerPath = path.toLowerCase();
  const lowerAncestor = ancestor.toLowerCase();
  if (!lowerPath.startsWith(lowerAncestor)) return false;
  if (lowerAncestor.endsWith('/') || lowerAncestor.endsWith('\\')) return true;
  const next = lowerPath.charAt(lowerAncestor.length);
  return next === '/' || next === '\\';
}

// Recolours the selection/ancestor chain in place, so selecting a node never needs a
// full page reload (which used to reset the camera and re-run the layout on every click).
function applySelectionState(selectedPath) {
  const updates = [];
  for (const id in nodeMeta) {
    const meta = nodeMeta[id];
    let background = meta.baseColor;
    let border = meta.baseBorder;
    let width = meta.baseWidth;
    if (selectedPath && id === selectedPath) {
      background = '#FFD700';
      border = '#FFE066';
      width = 3;
    } else if (selectedPath && isAncestorPath(id, selectedPath)) {
      background = '#B388FF';
      border = '#CCAAFF';
      width = 2;
    }
    updates.push({ id, color: { background, border }, borderWidth: width });
  }
  if (updates.length) nodes.update(updates);
  if (selectedPath && nodes.get(selectedPath)) {
    network.selectNodes([selectedPath]);
  } else {
    network.unselectAll();
  }
}
window.applySelectionState = applySelectionState;

const forcePhysics = {
  enabled: true,
  solver: 'forceAtlas2Based',
  forceAtlas2Based: {
    gravitationalConstant: -60,
    centralGravity: 0.02,
    springLength: 160,
    springConstant: 0.05,
    damping: 0.65,
    avoidOverlap: 0.7,
  },
  stabilization: { iterations: 260, fit: true },
  minVelocity: 0.75,
};
const treeLayout = {
  hierarchical: {
    enabled: true,
    direction: 'UD',
    sortMethod: 'directed',
    levelSeparation: 170,
    nodeSpacing: 190,
    treeSpacing: 240,
    blockShifting: true,
    edgeMinimization: true,
    parentCentralization: true,
  },
};
const forceEdges = {
  smooth: { type: 'dynamic', roundness: 0.5 },
  arrows: { to: { enabled: false } },
};
const treeEdges = {
  smooth: { type: 'cubicBezier', forceDirection: 'vertical', roundness: 0.4 },
  arrows: { to: { enabled: true, scaleFactor: 0.4 } },
};

let currentLayout = GRAPH_DATA.nodes.length > 10 ? 'tree' : 'force';
const isTree = () => currentLayout === 'tree';

// In force mode the root is pinned at the origin so children radiate outwards around it
// instead of drifting into a hairball. The hierarchical layout owns positions in tree mode.
function applyRootPin() {
  const rootId = GRAPH_DATA.root;
  if (!rootId || !nodes.get(rootId)) return;
  if (isTree()) {
    nodes.update({ id: rootId, fixed: false });
  } else {
    nodes.update({ id: rootId, fixed: { x: 0, y: 0 } });
  }
}

const network = new vis.Network(container, { nodes, edges }, {
  interaction: { hover: true, tooltipDelay: 60, hoverConnectedEdges: true, navigationButtons: false },
  nodes: {
    borderWidth: 1.8,
    font: { color: '#d0d8ee', size: 13, face: 'Segoe UI', strokeWidth: 0 },
    shadow: { enabled: true, color: 'rgba(80,200,255,0.25)', size: 28, x: 0, y: 0 },
  },
  edges: Object.assign({
    color: { inherit: false, color: 'rgba(60,160,240,0.25)', highlight: 'rgba(80,220,255,0.6)', hover: 'rgba(80,220,255,0.4)' },
    width: 0.9,
    selectionWidth: 2.2,
  }, isTree() ? treeEdges : forceEdges),
  layout: isTree() ? treeLayout : { hierarchical: { enabled: false } },
  physics: isTree() ? { enabled: false } : forcePhysics,
});

function setActiveLayoutButton() {
  document.querySelectorAll('#toolbar button[data-layout]').forEach(b => {
    b.classList.toggle('active', b.dataset.layout === currentLayout);
  });
}

function settleView() {
  network.fit({ animation: { duration: 250 } });
  // Fitting hundreds of nodes makes them unreadable; clamp how far out we zoom and
  // re-center on the selected node (or the graph root) when that happens.
  window.setTimeout(() => {
    if (network.getScale() < 0.5) {
      const target = (GRAPH_DATA.selected && nodes.get(GRAPH_DATA.selected)) ? GRAPH_DATA.selected : GRAPH_DATA.root;
      if (target && nodes.get(target)) {
        network.focus(target, { scale: 0.5, animation: { duration: 300 } });
      }
    }
  }, 320);
  if (GRAPH_DATA.selected && nodes.get(GRAPH_DATA.selected)) {
    network.selectNodes([GRAPH_DATA.selected]);
  }
}

function applyLayout(mode) {
  currentLayout = mode;
  setActiveLayoutButton();
  applyRootPin();
  if (mode === 'tree') {
    network.setOptions({ layout: treeLayout, physics: { enabled: false }, edges: treeEdges });
    window.setTimeout(settleView, 60);
  } else {
    network.setOptions({ layout: { hierarchical: { enabled: false } }, physics: Object.assign({}, forcePhysics), edges: forceEdges });
    network.stabilize(260);
  }
}

document.getElementById('btnFit').addEventListener('click', settleView);
document.getElementById('btnZoomIn').addEventListener('click', () => network.moveTo({ scale: network.getScale() * 1.25, animation: { duration: 160 } }));
document.getElementById('btnZoomOut').addEventListener('click', () => network.moveTo({ scale: network.getScale() * 0.8, animation: { duration: 160 } }));
document.getElementById('btnRelayout').addEventListener('click', () => applyLayout(currentLayout));
document.querySelectorAll('#toolbar button[data-layout]').forEach(button => {
  button.addEventListener('click', () => applyLayout(button.dataset.layout));
});
)HTML"
        R"HTML(
setActiveLayoutButton();
applyRootPin();
if (isTree()) {
  window.setTimeout(settleView, 80);
}
// Freeze once settled so nodes stay clickable instead of drifting forever.
network.on('stabilizationIterationsDone', () => {
  if (!isTree()) {
    network.setOptions({ physics: { enabled: false } });
  }
  settleView();
});

// Hidden labels reveal themselves on hover, and hovering a node zooms/focuses it.
let hoverFocus = true;
let hoverHome = null;
let hoverIntentTimer = null;
let lastMouse = { x: -1000, y: -1000 };
let lastFocusMouse = { x: -1000, y: -1000 };

function cancelHoverIntent() {
  if (hoverIntentTimer) {
    clearTimeout(hoverIntentTimer);
    hoverIntentTimer = null;
  }
}

function restoreHoverHome() {
  if (hoverHome) {
    network.moveTo({
      position: hoverHome.position,
      scale: hoverHome.scale,
      animation: { duration: 550, easingFunction: 'easeInOutCubic' },
    });
    hoverHome = null;
  }
}

container.addEventListener('mousemove', e => { lastMouse = { x: e.clientX, y: e.clientY }; }, true);

// Escape undoes the hover zoom (and fits the graph when there was nothing to undo).
window.addEventListener('keydown', e => {
  if (e.key !== 'Escape') return;
  cancelHoverIntent();
  if (hoverHome) {
    restoreHoverHome();
  } else {
    network.fit({ animation: { duration: 300, easingFunction: 'easeInOutCubic' } });
  }
});

const hoverFocusButton = document.getElementById('btnHoverFocus');
if (hoverFocusButton) {
  hoverFocusButton.addEventListener('click', function() {
    hoverFocus = !hoverFocus;
    this.classList.toggle('active', hoverFocus);
    cancelHoverIntent();
    if (!hoverFocus) {
      restoreHoverHome();
    }
  });
}

network.on('hoverNode', p => {
  const meta = nodeMeta[p.node];
  if (meta && !meta.shown && meta.name) nodes.update({ id: p.node, label: meta.name });

  if (!hoverFocus) return;

  // If the pointer has not actually moved since the last camera move, this hover was
  // caused by the camera sliding a node under a stationary cursor. Ignoring those is what
  // stops the zoom-in/zoom-out feedback loop.
  if (Math.abs(lastMouse.x - lastFocusMouse.x) < 12 && Math.abs(lastMouse.y - lastFocusMouse.y) < 12) {
    return;
  }

  cancelHoverIntent();
  hoverIntentTimer = window.setTimeout(() => {
    hoverIntentTimer = null;
    if (!hoverHome) hoverHome = { position: network.getViewPosition(), scale: network.getScale() };
    lastFocusMouse = { x: lastMouse.x, y: lastMouse.y };
    const targetScale = Math.min(Math.max(network.getScale(), 0.9), 1.25);
    network.focus(p.node, {
      scale: targetScale,
      animation: { duration: 550, easingFunction: 'easeInOutCubic' },
    });
  }, 300);
});

network.on('blurNode', p => {
  const meta = nodeMeta[p.node];
  if (meta && !meta.shown) nodes.update({ id: p.node, label: '' });

  cancelHoverIntent();
  // No automatic zoom-out: it fought the user because the cursor stays put while the
  // camera moves. Use Escape or Fit to go back.
});

// Any press cancels a pending hover move so clicks always land where the user aimed.
container.addEventListener('mousedown', () => { cancelHoverIntent(); }, true);

let lastDragTimestamp = 0;
network.on('dragStart', () => { lastDragTimestamp = performance.now(); cancelHoverIntent(); });
network.on('dragEnd', () => { lastDragTimestamp = performance.now(); });

network.on('click', params => {
  // Ignore the click that ends a pan/drag.
  if (performance.now() - lastDragTimestamp < 200) return;

  if (!params.nodes.length) {
    applySelectionState(null);
    return;
  }

  // Act immediately on a single click. The native click detail tells us a second click of
  // a double-click is coming, so no artificial delay is needed.
  const source = params.event && params.event.srcEvent ? params.event.srcEvent : null;
  const detail = source && typeof source.detail === 'number' ? source.detail : 1;
  if (detail >= 2) return;

  const node = nodes.get(params.nodes[0]);
  if (graphBridge && node) {
    graphBridge.activateNode(node.id);
  }
});

network.on('doubleClick', params => {
  if (!params.nodes.length) { return; }
  const node = nodes.get(params.nodes[0]);
  if (!node) { return; }
  network.focus(node.id, {
    scale: Math.max(network.getScale(), 1.1),
    animation: { duration: 300, easingFunction: 'easeInOutQuad' }
  });
  if (graphBridge) {
    window.setTimeout(() => graphBridge.openNode(node.id), 200);
  }
});

container.addEventListener('contextmenu', function(e) {
  // Always swallow the browser menu; only nodes open ours.
  e.preventDefault();
  if (!graphBridge) return;
  const rect = container.getBoundingClientRect();
  const nodeId = network.getNodeAt({ x: e.clientX - rect.left, y: e.clientY - rect.top });
  if (nodeId) {
    graphBridge.contextMenuNode(nodeId, e.screenX, e.screenY);
  }
});
</script>
</body>
</html>
)HTML"
    );
}

}
