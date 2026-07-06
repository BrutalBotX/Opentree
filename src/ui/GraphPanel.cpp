#include "ui/GraphPanel.h"

#include <QHash>
#include <QDir>
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

    if (!path.startsWith(rootPath, Qt::CaseInsensitive)) {
        return false;
    }

    if (path.length() <= rootPath.length()) {
        return false;
    }

    // rootPath (e.g. C:/) already ends with separator → path is automatically a descendant
    if (rootPath.endsWith(QLatin1Char('/')) || rootPath.endsWith(QLatin1Char('\\'))) {
        return true;
    }

    // Otherwise the next character after rootPath must be a separator
    const QChar next = path.at(rootPath.length());
    return next == QLatin1Char('/') || next == QLatin1Char('\\');
}

bool pathIsAncestorOf(const QString &ancestorPath, const QString &path)
{
    if (ancestorPath.isEmpty() || path.isEmpty()) {
        return false;
    }

    if (ancestorPath.compare(path, Qt::CaseInsensitive) == 0) {
        return true;
    }

    if (!path.startsWith(ancestorPath, Qt::CaseInsensitive)) {
        return false;
    }

    if (path.length() <= ancestorPath.length()) {
        return false;
    }

    if (ancestorPath.endsWith(QLatin1Char('/')) || ancestorPath.endsWith(QLatin1Char('\\'))) {
        return true;
    }

    const QChar next = path.at(ancestorPath.length());
    return next == QLatin1Char('/') || next == QLatin1Char('\\');
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
    m_bridge->onContextMenu = [this](const QString &path, int x, int y) { showNodeContextMenu(path, x, y); };
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
    QAction *selected = menu.exec(QPoint(screenX, screenY));
    if (!selected) {
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
    m_summaryLabel->setText(QStringLiteral("Graph: %1 folders shown for %2%3%4")
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
            "<defs><radialGradient id='g' cx='35%%' cy='30%%' r='60%%'>"
            "<stop offset='0%%' stop-color='%2'/>"
            "<stop offset='100%%' stop-color='%3'/>"
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
        nodeJson << QStringLiteral("{id:'__up__',label:%1,title:%2,size:20,shape:'circularImage',image:'%3',borderWidth:1,color:{background:'#1E2740',border:'#5A7AD6'},font:{color:'#D0E0FF'}}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(upLabel)))
                        .arg(QStringLiteral("'%1'").arg(upTitle))
                        .arg(moonImg);
    }

    // Size percentile → planet style: 0=moon,1=rocky,2=earth,3=giant,4=ringed
    QVector<double> sizeMetrics;
    for (const TreeEntry &entry : folders) {
        if (allowedPaths.contains(entry.path))
            sizeMetrics.append(nodeMetric(entry, m_nodeSizeMode));
    }
    std::sort(sizeMetrics.begin(), sizeMetrics.end());
    auto planetStyleForSize = [&](double m) -> int {
        if (sizeMetrics.isEmpty()) return 2;
        int n = 0; for (double s : sizeMetrics) { if (s <= m) ++n; }
        double p = double(n) / double(sizeMetrics.size());
        if (p >= 0.80) return 4; if (p >= 0.60) return 3;
        if (p >= 0.40) return 2; if (p >= 0.20) return 1;
        return 0;
    };

    static const QString styleLight[] = { QStringLiteral("#8890A0"), QStringLiteral("#E07060"), QStringLiteral("#59A14F"), QStringLiteral("#00D4FF"), QStringLiteral("#FFD700") };
    static const QString styleDark[]  = { QStringLiteral("#505868"), QStringLiteral("#802030"), QStringLiteral("#207030"), QStringLiteral("#0066AA"), QStringLiteral("#E87D00") };
    static const bool styleRing[]     = { false, false, false, false, true };

    for (const TreeEntry &entry : folders) {
        if (!allowedPaths.contains(entry.path)) {
            continue;
        }
        const qint64 delta = deltaByPath.value(entry.path, 0);
        bool sel = !m_selectedPath.isEmpty() && entry.path.compare(m_selectedPath, Qt::CaseInsensitive) == 0;
        bool anc = !sel && !m_selectedPath.isEmpty() && pathIsAncestorOf(entry.path, m_selectedPath);

        double metric = nodeMetric(entry, m_nodeSizeMode);
        int pStyle = planetStyleForSize(metric);
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
        nodeJson << QStringLiteral("{id:%1,label:%2,title:%3,size:%4,borderWidth:%5,shape:'circularImage',image:'%6',color:{background:%7,border:%8}}")
                        .arg(QStringLiteral("'%1'").arg(escapedPath))
                        .arg(QStringLiteral("'%1'").arg(escapedName))
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
        nodeJson << QStringLiteral("{id:'__other_folders__',label:'Other folders',title:%1,size:20,shape:'circularImage',image:'%2',borderWidth:1.5,color:{background:'#3A4A5A',border:'#6A8AAA'}}")
                        .arg(QStringLiteral("'%1'").arg(escapeJsString(QStringLiteral("Other direct folders under %1\nSize: %2").arg(rootPath, SizeFormatter::formatBytes(otherFolderBytes)))))
                        .arg(asteroidImg);
        edgeJson << QStringLiteral("{from:%1,to:'__other_folders__',dashes:true}")
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
  body {
    background: radial-gradient(ellipse 140% 90% at 30% 20%, #101624 0%, #0a0d16 55%, #05070e 100%);
    color: #d0d8ee; font-family: "Segoe UI", system-ui, sans-serif;
    height: 100vh; overflow: hidden;
  }
  #bgCanvas { position: fixed; top: 0; left: 0; width: 100%; height: 100%; z-index: 0; pointer-events: none; }
  #graph { position: relative; width: 100vw; height: 100vh; z-index: 1; }
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
  <div id="legend"><b>Graph</b><span class="sep">·</span>click: select<span class="sep">·</span>double-click: enter folder<span class="sep">·</span>right-click: view in other tabs<span class="sep">·</span><span style="color:#6a7a98;">planets: folders</span></div>
<script>
__GRAPH_DATA__

// galaxy particle system
(function() {
  const c = document.getElementById('bgCanvas');
  const ctx = c.getContext('2d');
  let W, H, cx, cy;
  function resize() {
    W = c.width = window.innerWidth;
    H = c.height = window.innerHeight;
    cx = W / 2; cy = H / 2;
  }
  resize();
  window.addEventListener('resize', resize);

  const particles = [];
  const colors = ['80,180,255','160,200,255','180,160,255','200,220,255','120,200,255'];
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

  function frame() {
    ctx.fillStyle = 'rgba(5,7,14,0.04)';
    ctx.fillRect(0, 0, W, H);
    for (const p of particles) {
      if (!p.isStatic) {
        p.x += p.vx;
        p.y += p.vy;
        if (p.x < -30) p.x = W + 30; if (p.x > W + 30) p.x = -30;
        if (p.y < -30) p.y = H + 30; if (p.y > H + 30) p.y = -30;
      }
      let a = p.alpha;
      if (p.pulse) a = p.alpha * (0.5 + 0.5 * Math.sin(p.pulsePhase + Date.now() * p.pulse));
      ctx.beginPath();
      ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(' + p.color + ',' + a + ')';
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
const network = new vis.Network(container, { nodes, edges }, {
  physics: {
    enabled: true,
    solver: 'forceAtlas2Based',
    forceAtlas2Based: {
      gravitationalConstant: -22,
      centralGravity: 0.003,
      springLength: 220,
      springConstant: 0.025,
      damping: 0.7,
      avoidOverlap: 0.3,
    },
    stabilization: { iterations: 150, fit: true },
  },
  interaction: { hover: true, tooltipDelay: 60, navigationButtons: false },
  nodes: {
    borderWidth: 1.8,
    font: { color: '#d0d8ee', size: 13, face: 'Segoe UI', strokeWidth: 0 },
    shadow: { enabled: true, color: 'rgba(80,200,255,0.25)', size: 28, x: 0, y: 0 },
  },
  edges: {
    color: { inherit: false, color: 'rgba(60,160,240,0.25)', highlight: 'rgba(80,220,255,0.5)', hover: 'rgba(80,220,255,0.35)' },
    width: 0.8,
    smooth: { type: 'dynamic', roundness: 0.5 },
    selectionWidth: 2.0,
  },
});
network.once('stabilizationIterationsDone', () => {
  network.setOptions({
    physics: {
      enabled: true,
      solver: 'forceAtlas2Based',
      forceAtlas2Based: {
        gravitationalConstant: -1.5,
        centralGravity: 0.002,
        springLength: 100,
        springConstant: 0.004,
        damping: 0.93,
        avoidOverlap: 0.05,
      },
      stabilization: false,
    },
  });
  if (GRAPH_DATA.selected && nodes.get(GRAPH_DATA.selected)) {
    network.selectNodes([GRAPH_DATA.selected]);
    network.focus(GRAPH_DATA.selected, { scale: 1.0, animation: false });
  }
});
let clickTimer = null;
network.on('click', params => {
  if (!params.nodes.length) { return; }
  if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; return; }
  clickTimer = setTimeout(() => {
    clickTimer = null;
    const node = nodes.get(params.nodes[0]);
    if (graphBridge && node) {
      graphBridge.activateNode(node.id);
    }
  }, 250);
});
network.on('doubleClick', params => {
  if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; }
  if (!params.nodes.length) { return; }
  const node = nodes.get(params.nodes[0]);
  if (!node) { return; }
  network.focus(node.id, {
    scale: 1.3,
    animation: { duration: 300, easingFunction: 'easeInOutQuad' }
  });
  if (graphBridge) {
    window.setTimeout(() => graphBridge.openNode(node.id), 200);
  }
});
container.addEventListener('contextmenu', function(e) {
  const rect = container.getBoundingClientRect();
  const nodeId = network.getNodeAt({ x: e.clientX - rect.left, y: e.clientY - rect.top });
  if (nodeId && graphBridge) {
    e.preventDefault();
    graphBridge.contextMenuNode(nodeId, e.screenX, e.screenY);
  }
});
</script>
</body>
</html>
)HTML");
}

}
