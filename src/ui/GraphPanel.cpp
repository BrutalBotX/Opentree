#include "ui/GraphPanel.h"

#include <QHash>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "utils/SizeFormatter.h"

#if defined(OPENTREE_HAVE_WEBENGINE)
#include <QWebChannel>
#include <QWebEngineView>
#endif

namespace opentree {

namespace {

bool isSameOrDescendant(const QString &path, const QString &rootPath)
{
    if (rootPath.isEmpty()) {
        return true;
    }

    if (path.compare(rootPath, Qt::CaseInsensitive) == 0) {
        return true;
    }

    return path.startsWith(rootPath + QLatin1Char('/'), Qt::CaseInsensitive)
        || path.startsWith(rootPath + QLatin1Char('\\'), Qt::CaseInsensitive);
}

bool pathIsAncestorOf(const QString &ancestorPath, const QString &path)
{
    if (ancestorPath.isEmpty() || path.isEmpty()) {
        return false;
    }

    if (ancestorPath.compare(path, Qt::CaseInsensitive) == 0) {
        return true;
    }

    return path.startsWith(ancestorPath + QLatin1Char('/'), Qt::CaseInsensitive)
        || path.startsWith(ancestorPath + QLatin1Char('\\'), Qt::CaseInsensitive);
}

QString escapeJsString(QString value)
{
    value.replace("\\", "\\\\");
    value.replace("'", "\\'");
    value.replace("\r", "");
    value.replace("\n", "\\n");
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
    , m_summaryLabel(new QLabel(this))
#if defined(OPENTREE_HAVE_WEBENGINE)
    , m_bridge(new GraphBridge(this))
    , m_channel(new QWebChannel(this))
    , m_view(new QWebEngineView(this))
#else
    , m_view(new QTextBrowser(this))
#endif
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    m_addressBar->setPlaceholderText("Enter folder path...");
    connect(m_addressBar, &QLineEdit::returnPressed, this, &GraphPanel::handleAddressSubmitted);
    m_summaryLabel->setWordWrap(true);
#if !defined(OPENTREE_HAVE_WEBENGINE)
    m_view->setReadOnly(true);
#else
    m_bridge->onActivate = [this](const QString &path) { activateNode(path); };
    m_bridge->onOpen = [this](const QString &path) { openNode(path); };
    m_channel->registerObject(QStringLiteral("graphBridge"), m_bridge);
    m_view->page()->setWebChannel(m_channel);
#endif
    layout->addWidget(m_addressBar);
    layout->addWidget(m_summaryLabel);
    layout->addWidget(m_view, 1);

    setGraphData(QString(), {}, {});
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
    renderGraph();
}

void GraphPanel::setNodeSizeMode(NodeSizeMode mode)
{
    if (m_nodeSizeMode == mode) {
        return;
    }

    m_nodeSizeMode = mode;
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
    if (!m_suspendRender) {
        renderGraph();
    }
}

void GraphPanel::setVisiblePaths(const QStringList &paths)
{
    m_visiblePaths = paths;
    renderGraph();
}

void GraphPanel::setOtherThresholdPercent(double percent)
{
    const double clamped = std::max(0.0, percent);
    if (std::abs(m_otherThresholdPercent - clamped) < 0.0001) {
        return;
    }

    m_otherThresholdPercent = clamped;
    renderGraph();
}

void GraphPanel::activateNode(const QString &path)
{
    const TreeEntry *entry = findEntryByPath(path);
    if (!entry) {
        return;
    }

    m_suspendRender = true;
    m_selectedPath = entry->path;
    emit entryActivated(*entry);
    m_suspendRender = false;
    renderGraph();
}

void GraphPanel::openNode(const QString &path)
{
    if (path == QStringLiteral("__up__")) {
        const TreeEntry *entry = findEntryByPath(m_graphRootPath);
        const TreeEntry *parentEntry = entry ? findEntryByPath(entry->parentPath) : nullptr;
        if (parentEntry) {
            m_suspendRender = true;
            m_selectedPath = parentEntry->path;
            emit entryOpened(*parentEntry);
            m_suspendRender = false;
            renderGraph();
        }
        return;
    }

    const TreeEntry *entry = findEntryByPath(path);
    if (!entry) {
        return;
    }

    m_suspendRender = true;
    m_selectedPath = entry->path;
    if (entry->kind == TreeEntryKind::Folder) {
        emit entryOpened(*entry);
    } else {
        emit entryActivated(*entry);
    }
    m_suspendRender = false;
    renderGraph();
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
    if (m_addressBar->text().compare(m_graphRootPath, Qt::CaseInsensitive) != 0) {
        m_addressBar->setText(m_graphRootPath);
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
    QStringList lines;
    lines << QStringLiteral("Top items for %1").arg(m_graphRootPath);
    int shown = 0;
    for (const TreeEntry &entry : m_currentEntries) {
        if (!isSameOrDescendant(entry.path, m_graphRootPath)) {
            continue;
        }
        lines << QStringLiteral("- %1 (%2)").arg(entry.path, SizeFormatter::formatBytes(entry.size));
        if (++shown >= 40) {
            break;
        }
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
    m_summaryLabel->setText(QStringLiteral("Graph: %1 folders shown for %2%3%4 | top direct files included")
                                .arg(folderCount)
                                .arg(m_graphRootPath)
                                .arg(m_currentCompareRows.isEmpty() ? QString() : QStringLiteral(" | delta colors active"))
                                .arg(isDrilledIn ? QStringLiteral(" | drilled in") : QString()));
}

QString GraphPanel::buildEmptyHtml() const
{
    return QStringLiteral(R"HTML(
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
)HTML");
}

QString GraphPanel::buildGraphPayload(const QString &rootPath, const QVector<TreeEntry> &entries, const QVector<SnapshotCompareRow> &compareRows) const
{
    QHash<QString, qint64> deltaByPath;
    for (const SnapshotCompareRow &row : compareRows) {
        deltaByPath.insert(row.path, row.deltaBytes);
    }

    QVector<TreeEntry> folders;
    QVector<TreeEntry> directFolders;
    QVector<TreeEntry> directFiles;
    TreeEntry rootEntry;
    bool hasRootEntry = false;
    bool hasUpNode = false;
    QString upTargetPath;
    QVector<TreeEntry> selectedAncestors;
    for (const TreeEntry &entry : entries) {
        if (!isSameOrDescendant(entry.path, rootPath)) {
            continue;
        }

        if (entry.kind == TreeEntryKind::Folder && !m_visiblePaths.isEmpty() && !m_visiblePaths.contains(entry.path, Qt::CaseInsensitive)) {
            continue;
        }

        if (entry.kind == TreeEntryKind::Folder && entry.path.compare(rootPath, Qt::CaseInsensitive) == 0) {
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
    if (folders.size() > (hasRootEntry ? 119 : 120)) {
        folders.resize(hasRootEntry ? 119 : 120);
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

    allowedPaths.insert(rootPath);
    for (const TreeEntry &entry : folders) {
        bool keep = entry.path.compare(rootPath, Qt::CaseInsensitive) == 0;
        for (const TreeEntry &directFolder : keptDirectFolders) {
            if (isSameOrDescendant(entry.path, directFolder.path)) {
                keep = true;
                break;
            }
        }
        if (keep || (!m_selectedPath.isEmpty() && pathIsAncestorOf(entry.path, m_selectedPath))) {
            allowedPaths.insert(entry.path);
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

    if (hasUpNode) {
        const QString upLabel = rootPath == m_currentRootPath ? QStringLiteral("Root") : QStringLiteral("Up");
        const QString upTitle = escapeJsString(QStringLiteral("Go to %1").arg(upTargetPath));
        nodeJson << QStringLiteral("{id:'__up__',label:%1,title:%2,size:20,shape:'box',margin:10,color:{background:'#232938',border:'#8EA3C0'},font:{color:'#DCE6F2'}}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(upLabel)))
                        .arg(QStringLiteral("'%1'").arg(upTitle));
    }

    for (const TreeEntry &entry : folders) {
        if (!allowedPaths.contains(entry.path)) {
            continue;
        }
        const qint64 delta = deltaByPath.value(entry.path, 0);
        QString color = "#4E79A7";
        QString borderColor = color;
        if (delta > 0) {
            color = "#E15759";
        } else if (delta < 0) {
            color = "#59A14F";
        }

        if (!m_selectedPath.isEmpty() && entry.path.compare(m_selectedPath, Qt::CaseInsensitive) == 0) {
            color = "#F28E2B";
            borderColor = "#FFD166";
        } else if (!m_selectedPath.isEmpty() && pathIsAncestorOf(entry.path, m_selectedPath)) {
            color = "#7B61FF";
            borderColor = "#B8A1FF";
        }

        const double size = normalizedNodeSize(nodeMetric(entry, m_nodeSizeMode), minMetric, maxMetric);
        const QString escapedPath = escapeJsString(entry.path);
        const QString escapedName = escapeJsString(entry.name);
        const QString escapedTitle = escapeJsString(QStringLiteral("%1\nSize: %2\nDelta: %3")
                                                        .arg(entry.path, SizeFormatter::formatBytes(entry.size), QString::number(delta)));
        nodeJson << QStringLiteral("{id:%1,label:%2,title:%3,size:%4,borderWidth:%5,color:{background:%6,border:%7}}")
                        .arg(QStringLiteral("'%1'").arg(escapedPath))
                        .arg(QStringLiteral("'%1'").arg(escapedName))
                        .arg(QStringLiteral("'%1'").arg(escapedTitle))
                        .arg(size)
                        .arg(entry.path.compare(m_selectedPath, Qt::CaseInsensitive) == 0 ? QStringLiteral("3") : QStringLiteral("1.5"))
                        .arg(QStringLiteral("'%1'").arg(color))
                        .arg(QStringLiteral("'%1'").arg(borderColor));

        if (!entry.parentPath.isEmpty() && allowedPaths.contains(entry.parentPath)) {
            edgeJson << QStringLiteral("{from:%1,to:%2}")
                            .arg(QStringLiteral("'%1'").arg(escapeJsString(entry.parentPath)))
                            .arg(QStringLiteral("'%1'").arg(escapedPath));
        }
    }

    for (const TreeEntry &entry : keptDirectFiles) {
        const QString escapedPath = escapeJsString(entry.path);
        const QString escapedName = escapeJsString(entry.name);
        const QString escapedTitle = escapeJsString(QStringLiteral("%1\nSize: %2")
                                                        .arg(entry.path, SizeFormatter::formatBytes(entry.size)));
        const double size = normalizedNodeSize(nodeMetric(entry, NodeSizeMode::Size), 0.0, std::max(1.0, nodeMetric(rootEntry, NodeSizeMode::Size)));
        const bool isSelected = !m_selectedPath.isEmpty() && entry.path.compare(m_selectedPath, Qt::CaseInsensitive) == 0;
        nodeJson << QStringLiteral("{id:%1,label:%2,title:%3,size:%4,shape:'diamond',borderWidth:%5,color:{background:%6,border:%7}}")
                        .arg(QStringLiteral("'%1'").arg(escapedPath))
                        .arg(QStringLiteral("'%1'").arg(escapedName))
                        .arg(QStringLiteral("'%1'").arg(escapedTitle))
                        .arg(size)
                        .arg(isSelected ? QStringLiteral("3") : QStringLiteral("1.5"))
                        .arg(QStringLiteral("'%1'").arg(isSelected ? QStringLiteral("#F28E2B") : QStringLiteral("#9AA5B1")))
                        .arg(QStringLiteral("'%1'").arg(isSelected ? QStringLiteral("#FFD166") : QStringLiteral("#C7D0D9")));
        edgeJson << QStringLiteral("{from:%1,to:%2,dashes:true}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(rootPath)))
                        .arg(QStringLiteral("'%1'").arg(escapedPath));
    }

    if (otherFolderBytes > 0) {
        nodeJson << QStringLiteral("{id:'__other_folders__',label:'Other folders',title:%1,size:20,shape:'box',borderWidth:1.5,color:{background:'#4B5563',border:'#93A2B8'}}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(QStringLiteral("Other direct folders under %1\nSize: %2").arg(rootPath, SizeFormatter::formatBytes(otherFolderBytes)))));
        edgeJson << QStringLiteral("{from:%1,to:'__other_folders__',dashes:true}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(rootPath)));
    }

    if (otherFileBytes > 0) {
        const QString aggregateId = QStringLiteral("__other_files__");
        nodeJson << QStringLiteral("{id:'__other_files__',label:'Other files',title:%1,size:18,shape:'diamond',borderWidth:1.5,color:{background:'#6B7280',border:'#A5B4C7'}}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(QStringLiteral("Other direct files under %1\nSize: %2").arg(rootPath, SizeFormatter::formatBytes(otherFileBytes)))));
        edgeJson << QStringLiteral("{from:%1,to:'__other_files__',dashes:true}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(rootPath)));
    }

    return QStringLiteral("const GRAPH_DATA={nodes:[%1],edges:[%2],root:%3,selected:%4};")
        .arg(nodeJson.join(','),
             edgeJson.join(','),
             QStringLiteral("'%1'").arg(escapeJsString(rootPath)),
             m_selectedPath.isEmpty() ? QStringLiteral("null") : QStringLiteral("'%1'").arg(escapeJsString(m_selectedPath)));
}

QString GraphPanel::buildHtml() const
{
    return QStringLiteral(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>OpenTree Graph</title>
<script src="qrc:///js/vis-network.min.js"></script>
<script src="qrc:///qtwebchannel/qwebchannel.js"></script>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { background: radial-gradient(circle at top, #1a2030 0%, #11131a 58%, #0b0d12 100%); color: #e0e0e0; font-family: "Segoe UI", sans-serif; height: 100vh; overflow: hidden; }
  #graph { width: 100vw; height: 100vh; }
  #legend {
    position: fixed; top: 12px; left: 12px; z-index: 20;
    padding: 8px 10px; border: 1px solid #2f3950; border-radius: 10px;
    background: rgba(16, 19, 27, 0.86); backdrop-filter: blur(4px);
    font-size: 12px; color: #d8e0ef;
  }
  #legend b { color: #ffffff; }
</style>
</head>
<body>
<div id="graph"></div>
  <div id="legend"><b>Graph</b> click: select, double-click: enter folder, diamonds: files, box: go up</div>
<script>
__GRAPH_DATA__
let graphBridge = null;
new QWebChannel(qt.webChannelTransport, function(channel) {
  graphBridge = channel.objects.graphBridge;
});
const container = document.getElementById('graph');
const nodes = new vis.DataSet(GRAPH_DATA.nodes);
const edges = new vis.DataSet(GRAPH_DATA.edges);
const network = new vis.Network(container, { nodes, edges }, {
  physics: {
    enabled: true,
    solver: 'forceAtlas2Based',
    forceAtlas2Based: {
      gravitationalConstant: -45,
      centralGravity: 0.01,
      springLength: 120,
      springConstant: 0.08,
      damping: 0.4,
      avoidOverlap: 0.8,
    },
    stabilization: { iterations: 200, fit: true },
  },
  interaction: { hover: true, tooltipDelay: 80 },
  nodes: { shape: 'dot', borderWidth: 1.5, font: { color: '#ffffff', size: 15, face: 'Segoe UI' }, shadow: { enabled: true, color: 'rgba(0,0,0,0.28)', size: 10, x: 0, y: 3 } },
  edges: { color: { opacity: 0.4 }, width: 1.2, smooth: { type: 'continuous' } }
});
network.once('stabilizationIterationsDone', () => {
  network.setOptions({ physics: { enabled: false } });
  if (GRAPH_DATA.selected && nodes.get(GRAPH_DATA.selected)) {
    network.selectNodes([GRAPH_DATA.selected]);
  }
});
network.on('click', params => {
  if (!params.nodes.length) {
    return;
  }
  const node = nodes.get(params.nodes[0]);
  if (graphBridge && node) {
    graphBridge.activateNode(node.id);
  }
});
network.on('doubleClick', params => {
  if (!params.nodes.length) {
    return;
  }
  const node = nodes.get(params.nodes[0]);
  if (!node) {
    return;
  }
  network.focus(node.id, {
    scale: 1.2,
    animation: { duration: 260, easingFunction: 'easeInOutQuad' }
  });
  if (graphBridge) {
    window.setTimeout(() => graphBridge.openNode(node.id), 150);
  }
});
</script>
</body>
</html>
)HTML");
}

}
