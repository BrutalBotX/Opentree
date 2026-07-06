#include "ui/ChartPanel.h"

#include <QColor>
#include <QContextMenuEvent>
#include <QCompleter>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QToolTip>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "utils/SizeFormatter.h"

namespace opentree {

namespace {

bool samePath(const QString &left, const QString &right)
{
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

QColor chartColor(int index)
{
    static const QColor colors[] = {
        QColor(76, 132, 255),
        QColor(242, 142, 43),
        QColor(91, 192, 120),
        QColor(224, 92, 92),
        QColor(164, 116, 255),
        QColor(74, 201, 189),
        QColor(230, 196, 70),
        QColor(120, 143, 156),
    };
    return colors[index % (sizeof(colors) / sizeof(colors[0]))];
}
 
} // namespace

class ChartCanvas : public QWidget {
public:
    ChartCanvas(ChartPanel::ViewMode mode, ChartPanel *owner)
        : QWidget(owner)
        , m_mode(mode)
        , m_owner(owner)
    {
        setMinimumHeight(360);
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), palette().window());
        if (m_owner) {
            m_owner->paintView(m_mode, painter, rect());
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_owner) {
            if (m_mode == ChartPanel::ViewMode::Treemap) {
                if (const auto *node = m_owner->treemapNodeAt(event->pos(), m_owner->m_treemapNodes); node && node->activatable) {
                    emit m_owner->entryActivated(node->entry);
                }
            } else if (const auto *slice = m_owner->sliceAt(m_mode, event->pos(), rect()); slice && slice->activatable) {
                emit m_owner->entryActivated(slice->entry);
            }
        }
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_owner) {
            if (m_mode == ChartPanel::ViewMode::Treemap) {
                if (const auto *node = m_owner->treemapNodeAt(event->pos(), m_owner->m_treemapNodes); node && node->activatable) {
                    emit m_owner->entryActivated(node->entry);
                }
            } else if (const auto *slice = m_owner->sliceAt(m_mode, event->pos(), rect()); slice && slice->activatable) {
                emit m_owner->entryActivated(slice->entry);
            }
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        if (!m_owner) {
            QWidget::contextMenuEvent(event);
            return;
        }

        if (m_mode == ChartPanel::ViewMode::Treemap) {
            const auto *node = m_owner->treemapNodeAt(event->pos(), m_owner->m_treemapNodes);
            if (!node || !node->activatable) {
                QWidget::contextMenuEvent(event);
                return;
            }

            QMenu menu(this);
            QAction *openAction = menu.addAction("Open");
            QAction *copyPathAction = menu.addAction("Copy Path");
            menu.addSeparator();
            QAction *openGraphAction = menu.addAction("Open in Graph View");
            QAction *selectedAction = menu.exec(event->globalPos());
            if (selectedAction == openAction) {
                emit m_owner->entryOpenRequested(node->entry);
            } else if (selectedAction == copyPathAction) {
                emit m_owner->entryCopyPathRequested(node->entry);
            } else if (selectedAction == openGraphAction) {
                emit m_owner->entryOpenInGraphRequested(node->entry);
            }
            return;
        }

        const auto *slice = m_owner->sliceAt(m_mode, event->pos(), rect());
        if (!slice || !slice->activatable) {
            QWidget::contextMenuEvent(event);
            return;
        }

        QMenu menu(this);
        QAction *openAction = menu.addAction("Open");
        QAction *copyPathAction = menu.addAction("Copy Path");
        menu.addSeparator();
        QAction *openGraphAction = menu.addAction("Open in Graph View");
        QAction *selectedAction = menu.exec(event->globalPos());
        if (selectedAction == openAction) {
            emit m_owner->entryOpenRequested(slice->entry);
        } else if (selectedAction == copyPathAction) {
            emit m_owner->entryCopyPathRequested(slice->entry);
        } else if (selectedAction == openGraphAction) {
            emit m_owner->entryOpenInGraphRequested(slice->entry);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_owner) {
            QWidget::mouseMoveEvent(event);
            return;
        }

        if (m_mode == ChartPanel::ViewMode::Treemap) {
            if (const auto *node = m_owner->treemapNodeAt(event->pos(), m_owner->m_treemapNodes)) {
                const double percent = m_owner->m_activeFolderSize <= 0 ? 0.0 : (100.0 * double(node->size) / double(m_owner->m_activeFolderSize));
                const QString metricText = m_owner->m_viewMetric == ViewMetric::Files
                    ? QStringLiteral("Files: %1").arg(node->entry.kind == TreeEntryKind::Folder ? node->entry.fileCount : 1)
                    : QStringLiteral("Size: %1").arg(SizeFormatter::formatBytes(node->size));
                QToolTip::showText(mapToGlobal(event->pos()), QStringLiteral("%1\n%2\nPercent: %3%")
                    .arg(node->label, metricText, QString::number(percent, 'f', 1)), this);
            } else {
                QToolTip::hideText();
            }
            QWidget::mouseMoveEvent(event);
            return;
        }

        if (const auto *slice = m_owner->hoverSliceAt(m_mode, event->pos(), rect())) {
            const double percent = m_owner->m_activeFolderSize <= 0 ? 0.0 : (100.0 * double(slice->size) / double(m_owner->m_activeFolderSize));
            const QString metricText = m_owner->m_viewMetric == ViewMetric::Files
                ? QStringLiteral("Files: %1").arg(slice->entry.kind == TreeEntryKind::Folder ? slice->entry.fileCount : 1)
                : QStringLiteral("Size: %1").arg(SizeFormatter::formatBytes(slice->size));
            QToolTip::showText(mapToGlobal(event->pos()), QStringLiteral("%1\n%2\nPercent: %3%")
                .arg(slice->label, metricText, QString::number(percent, 'f', 1)), this);
        } else {
            QToolTip::hideText();
        }

        QWidget::mouseMoveEvent(event);
    }

private:
    ChartPanel::ViewMode m_mode;
    ChartPanel *m_owner;
};

ChartPanel::ChartPanel(QWidget *parent)
    : QWidget(parent)
    , m_summaryLabel(new QLabel(this))
    , m_addressBar(new QLineEdit(this))
    , m_subtabs(new QTabWidget(this))
    , m_pieView(new ::opentree::ChartCanvas(ViewMode::Pie, this))
    , m_barView(new ::opentree::ChartCanvas(ViewMode::Bars, this))
    , m_treemapView(new ::opentree::ChartCanvas(ViewMode::Treemap, this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);
    m_addressBar->setPlaceholderText("Enter folder path...");
    layout->addWidget(m_addressBar);
    m_subtabs->addTab(m_pieView, "Pie");
    m_subtabs->addTab(m_barView, "Bars");
    m_subtabs->addTab(m_treemapView, "Treemap");
    layout->addWidget(m_subtabs, 1);
    connect(m_addressBar, &QLineEdit::returnPressed, this, &ChartPanel::handleAddressSubmitted);
    rebuild();
}

void ChartPanel::setScanResult(const ScanResult &result)
{
    m_result = result;
    if (m_activeFolderPath.isEmpty()) {
        m_activeFolderPath = result.rootPath;
    }
    rebuild();
}

void ChartPanel::setActiveFolderPath(const QString &path)
{
    if (samePath(m_activeFolderPath, path)) {
        return;
    }

    m_activeFolderPath = path;
    m_addressBar->setText(path);
    rebuild();
}

void ChartPanel::setOtherThresholdPercent(double percent)
{
    const double clamped = std::max(0.0, percent);
    if (std::abs(m_otherThresholdPercent - clamped) < 0.0001) {
        return;
    }

    m_otherThresholdPercent = clamped;
    rebuild();
}

void ChartPanel::setViewMetric(ViewMetric metric)
{
    if (m_viewMetric == metric) {
        return;
    }

    m_viewMetric = metric;
    m_pieView->update();
    m_barView->update();
    m_treemapView->update();
}

void ChartPanel::setTreemapDepth(int depth)
{
    const int clamped = std::clamp(depth, 1, 3);
    if (m_treemapDepth == clamped) {
        return;
    }

    m_treemapDepth = clamped;
    rebuild();
}

void ChartPanel::setActiveViewMode(ViewMode mode)
{
    int index = 0;
    switch (mode) {
    case ViewMode::Pie: index = 0; break;
    case ViewMode::Bars: index = 1; break;
    case ViewMode::Treemap: index = 2; break;
    }
    m_subtabs->setCurrentIndex(index);
}

void ChartPanel::rebuild()
{
    m_slices.clear();
    m_activeFolderSize = 0;

    if (m_result.treeEntries.isEmpty() || m_activeFolderPath.isEmpty()) {
        m_summaryLabel->setText("Chart: scan a folder to see pie, bar, and treemap charts for top folders and files.");
        m_pieView->update();
        m_barView->update();
        m_treemapView->update();
        return;
    }

    TreeEntry activeEntry;
    bool foundActive = false;
    for (const TreeEntry &entry : m_result.treeEntries) {
        if (entry.kind == TreeEntryKind::Folder && samePath(entry.path, m_activeFolderPath)) {
            activeEntry = entry;
            foundActive = true;
            break;
        }
    }

    if (!foundActive) {
        m_summaryLabel->setText(QStringLiteral("Chart: %1 is not available in the current scan.").arg(m_activeFolderPath));
        m_pieView->update();
        m_barView->update();
        m_treemapView->update();
        return;
    }

    m_activeFolderSize = activeEntry.size;

    QVector<TreeEntry> childFolders;
    QVector<FileEntry> childFiles;
    for (const TreeEntry &entry : m_result.treeEntries) {
        if (entry.kind == TreeEntryKind::Folder && samePath(entry.parentPath, activeEntry.path)) {
            childFolders.push_back(entry);
        }
    }
    for (const FileEntry &file : m_result.files) {
        if (samePath(file.parentPath, activeEntry.path)) {
            childFiles.push_back(file);
        }
    }

    std::sort(childFolders.begin(), childFolders.end(), [](const TreeEntry &left, const TreeEntry &right) {
        return left.size > right.size;
    });
    std::sort(childFiles.begin(), childFiles.end(), [](const FileEntry &left, const FileEntry &right) {
        return left.size > right.size;
    });

    QVector<TreeEntry> visibleFolders;
    qint64 otherFolderBytes = 0;
    for (const TreeEntry &folder : childFolders) {
        const double percent = activeEntry.size <= 0 ? 0.0 : (100.0 * double(folder.size) / double(activeEntry.size));
        if (visibleFolders.size() < 8 && percent >= m_otherThresholdPercent) {
            visibleFolders.push_back(folder);
        } else {
            otherFolderBytes += folder.size;
        }
    }

    QVector<FileEntry> visibleFiles;
    qint64 otherFileBytes = 0;
    for (const FileEntry &file : childFiles) {
        const double percent = activeEntry.size <= 0 ? 0.0 : (100.0 * double(file.size) / double(activeEntry.size));
        if (visibleFiles.size() < 8 && percent >= m_otherThresholdPercent) {
            visibleFiles.push_back(file);
        } else {
            otherFileBytes += file.size;
        }
    }

    double currentAngle = 0.0;
    int colorIndex = 0;
    for (const TreeEntry &child : visibleFolders) {
        const double fraction = activeEntry.size <= 0 ? 0.0 : double(child.size) / double(activeEntry.size);
        ChartSlice slice;
        slice.entry = child;
        slice.label = displayNameForPath(child.path);
        slice.size = child.size;
        slice.activatable = true;
        slice.startAngle = currentAngle;
        slice.spanAngle = fraction * 360.0;
        slice.color = chartColor(colorIndex++);
        m_slices.push_back(slice);
        currentAngle += slice.spanAngle;
    }

    for (const FileEntry &file : visibleFiles) {
        const double fraction = activeEntry.size <= 0 ? 0.0 : double(file.size) / double(activeEntry.size);
        ChartSlice slice;
        slice.entry.kind = TreeEntryKind::File;
        slice.entry.path = file.path;
        slice.entry.parentPath = file.parentPath;
        slice.entry.name = file.name;
        slice.entry.size = file.size;
        slice.entry.parentSize = activeEntry.size;
        slice.label = file.name;
        slice.size = file.size;
        slice.activatable = true;
        slice.startAngle = currentAngle;
        slice.spanAngle = fraction * 360.0;
        slice.color = chartColor(colorIndex++);
        m_slices.push_back(slice);
        currentAngle += slice.spanAngle;
    }

    if (otherFolderBytes > 0) {
        const double fraction = activeEntry.size <= 0 ? 0.0 : double(otherFolderBytes) / double(activeEntry.size);
        ChartSlice slice;
        slice.label = QStringLiteral("Other folders");
        slice.size = otherFolderBytes;
        slice.activatable = false;
        slice.startAngle = currentAngle;
        slice.spanAngle = fraction * 360.0;
        slice.color = QColor(110, 120, 140);
        m_slices.push_back(slice);
        currentAngle += slice.spanAngle;
    }

    if (otherFileBytes > 0) {
        const double fraction = activeEntry.size <= 0 ? 0.0 : double(otherFileBytes) / double(activeEntry.size);
        ChartSlice slice;
        slice.label = QStringLiteral("Other files");
        slice.size = otherFileBytes;
        slice.activatable = false;
        slice.startAngle = currentAngle;
        slice.spanAngle = fraction * 360.0;
        slice.color = QColor(180, 180, 190);
        m_slices.push_back(slice);
        currentAngle += slice.spanAngle;
    }

    m_treemapNodes = buildTreemapNodes(m_activeFolderPath, m_treemapDepth, 0);

    m_summaryLabel->setText(QStringLiteral("Charts | Total: %1 | Other cutoff: %2% | Treemap depth: %3")
                                .arg(SizeFormatter::formatBytes(activeEntry.size))
                                .arg(QString::number(m_otherThresholdPercent, 'f', 1))
                                .arg(m_treemapDepth));
    if (m_addressBar->text().compare(activeEntry.path, Qt::CaseInsensitive) != 0) {
        m_addressBar->setText(activeEntry.path);
    }
    m_pieView->update();
    m_barView->update();
    m_treemapView->update();
}

void ChartPanel::handleAddressSubmitted()
{
    const QString path = m_addressBar->text().trimmed();
    if (!path.isEmpty()) {
        emit pathEntered(path);
    }
}

QString ChartPanel::displayNameForPath(const QString &path) const
{
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}

void ChartPanel::paintView(ViewMode mode, QPainter &painter, const QRect &rect) const
{
    if (m_slices.isEmpty()) {
        painter.setPen(QColor(140, 150, 170));
        painter.drawText(QRectF(rect).adjusted(20, 20, -20, -20), Qt::AlignCenter, "No child folders or files to chart yet");
        return;
    }

    switch (mode) {
    case ViewMode::Pie:
        paintPie(painter, rect);
        break;
    case ViewMode::Bars:
        paintBars(painter, rect);
        break;
    case ViewMode::Treemap:
        paintTreemap(painter, rect);
        break;
    }
}

void ChartPanel::paintPie(QPainter &painter, const QRect &rect) const
{
    const QRectF area = QRectF(rect).adjusted(28, 24, -28, -24);
    const double pieSide = std::max(160.0, std::min(area.width(), area.height()) - 50.0);
    const QRectF pie(area.center().x() - (pieSide / 2.0), area.center().y() - (pieSide / 2.0), pieSide, pieSide);
    const double pieRadius = pie.width() / 2.0;

    for (ChartSlice &slice : const_cast<QVector<ChartSlice> &>(m_slices)) {
        slice.pieLabelVisible = false;
        slice.pieAnchor = {};
        slice.pieElbow = {};
        slice.pieLabelRect = {};
    }

    painter.setPen(QPen(QColor(55, 65, 85), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(pie);
    for (const ChartSlice &slice : m_slices) {
        painter.setBrush(slice.color);
        painter.setPen(Qt::NoPen);
        painter.drawPie(pie, int(-slice.startAngle * 16.0), int(-slice.spanAngle * 16.0));
    }

    const int showCount = std::min(int(m_slices.size()), 15);
    const QRectF clipRect = QRectF(rect).adjusted(8, 8, -8, -8);
    QFontMetrics fm(painter.font());

    struct PieLabel {
        int index; QPointF anchor; double cosA, sinA; QString text; double tw;
    };
    QVector<PieLabel> leftLabels, rightLabels;

    for (int i = 0; i < showCount; ++i) {
        const ChartSlice &slice = m_slices[i];
        const double midRad = (slice.startAngle + slice.spanAngle / 2.0) * M_PI / 180.0;
        const double c = cos(midRad), s = sin(midRad);
        const double pct = m_activeFolderSize <= 0 ? 0.0 : (100.0 * double(slice.size) / double(m_activeFolderSize));
        const QString t = QStringLiteral("%1 (%2%)").arg(slice.label, QString::number(pct, 'f', 1));
        const double tw = fm.horizontalAdvance(t);
        const QPointF a(pie.center().x() + pieRadius * c, pie.center().y() + pieRadius * s);
        (c >= 0 ? rightLabels : leftLabels).append({i, a, c, s, t, tw});
    }

    // Sort each side by anchor Y
    auto sortByY = [](QVector<PieLabel> &v) { std::sort(v.begin(), v.end(), [](auto &a, auto &b) { return a.anchor.y() < b.anchor.y(); }); };
    sortByY(leftLabels);
    sortByY(rightLabels);

    const double labelH = 20.0;
    const double topY = clipRect.top() + 4.0;
    const double botY = clipRect.bottom() - 4.0;
    const double availY = botY - topY;
    const double rightEdge = clipRect.right() - 8.0;
    const double leftEdge = clipRect.left() + 8.0;

    auto placeGroup = [&](QVector<PieLabel> &group, bool rightSide) {
        if (group.isEmpty()) return;
        const double spacing = std::min(availY / static_cast<double>(std::max<int>(1, static_cast<int>(group.size()))), labelH + 4.0);
        const double startY = topY + (availY - spacing * (group.size() - 1)) / 2.0;
        for (int idx = 0; idx < group.size(); ++idx) {
            auto &info = group[idx];
            const double labelY = startY + idx * spacing;
            const double bendX = info.anchor.x() + 18.0 * info.cosA;

            double lx;
            if (rightSide) {
                lx = qMax(bendX + 10.0, pie.right() + 24.0);
                lx = qMin(lx, rightEdge - info.tw - 4.0);
            } else {
                lx = qMin(bendX - info.tw - 14.0, pie.left() - info.tw - 24.0);
                lx = qMax(lx, leftEdge);
            }
            const QRectF labelBox(lx, labelY - labelH / 2.0, info.tw + 4.0, labelH);

            const ChartSlice &slice = m_slices[info.index];
            painter.setPen(QPen(slice.color, 1.2));
            painter.drawLine(info.anchor, QPointF(rightSide ? lx - 4 : lx + info.tw + 8, labelY));

            painter.setPen(palette().text().color());
            painter.drawText(labelBox, rightSide ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignRight | Qt::AlignVCenter, info.text);

            ChartSlice &ms = const_cast<QVector<ChartSlice> &>(m_slices)[info.index];
            ms.pieLabelVisible = true;
            ms.pieLabelRect = labelBox;
        }
    };

    placeGroup(rightLabels, true);
    placeGroup(leftLabels, false);
}

void ChartPanel::paintBars(QPainter &painter, const QRect &rect) const
{
    const QRectF area = QRectF(rect).adjusted(20, 20, -20, -20);
    const double rowCount = std::max(1.0, double(m_slices.size()));
    const double rowHeight = std::max(28.0, area.height() / rowCount);
    const double labelWidth = std::min(260.0, area.width() * 0.34);
    const double valueWidth = 130.0;
    const double chartLeft = area.left() + labelWidth + 10.0;
    const double chartRight = area.right() - valueWidth - 10.0;
    const double chartWidth = std::max(40.0, chartRight - chartLeft);

    for (int index = 0; index < m_slices.size(); ++index) {
        const ChartSlice &slice = m_slices[index];
        const double top = area.top() + index * rowHeight + 4.0;
        const double barHeight = rowHeight - 8.0;
        double ratio = m_activeFolderSize <= 0 ? 0.0 : double(slice.size) / double(m_activeFolderSize);
        if (m_viewMetric == ViewMetric::Files) {
            const double fileValue = slice.entry.kind == TreeEntryKind::Folder ? double(slice.entry.fileCount) : 1.0;
            const double totalFileValue = std::max(1.0, std::accumulate(m_slices.begin(), m_slices.end(), 0.0, [](double total, const ChartSlice &current) {
                return total + (current.entry.kind == TreeEntryKind::Folder ? double(current.entry.fileCount) : 1.0);
            }));
            ratio = fileValue / totalFileValue;
        }
        slice.barRect = QRectF(chartLeft, top, chartWidth * ratio, barHeight);

        painter.setPen(palette().text().color());
        painter.drawText(QRectF(area.left(), top, labelWidth, barHeight), Qt::AlignLeft | Qt::AlignVCenter, slice.label);
        painter.setBrush(QColor(55, 62, 78));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(chartLeft, top, chartWidth, barHeight), 4, 4);
        painter.setBrush(slice.color);
        painter.drawRoundedRect(slice.barRect, 4, 4);
        painter.setPen(palette().text().color());
        painter.drawText(QRectF(chartRight + 8.0, top, valueWidth, barHeight), Qt::AlignLeft | Qt::AlignVCenter,
            QStringLiteral("%1 (%2%)")
                .arg(m_viewMetric == ViewMetric::Files
                        ? QString::number(slice.entry.kind == TreeEntryKind::Folder ? slice.entry.fileCount : 1) + QStringLiteral(" files")
                        : SizeFormatter::formatBytes(slice.size))
                .arg(QString::number(ratio * 100.0, 'f', 1)));
    }
}

void ChartPanel::paintTreemap(QPainter &painter, const QRect &rect) const
{
    layoutTreemapNodes(m_treemapNodes, QRectF(rect).adjusted(16, 16, -16, -16), 0);
    paintTreemapNodes(painter, m_treemapNodes, 0);
}

const ChartPanel::ChartSlice *ChartPanel::sliceAt(ViewMode mode, const QPoint &point, const QRect &rect) const
{
    if (mode == ViewMode::Pie) {
        for (const ChartSlice &slice : m_slices) {
            if (slice.activatable && slice.pieLabelVisible && slice.pieLabelRect.contains(point)) {
                return &slice;
            }
        }
        return nullptr;
    }

    switch (mode) {
    case ViewMode::Bars:
        for (const ChartSlice &slice : m_slices) {
            if (slice.activatable && slice.barRect.contains(point)) {
                return &slice;
            }
        }
        return nullptr;
    case ViewMode::Treemap:
        if (const TreemapNode *node = treemapNodeAt(point, m_treemapNodes)) {
            for (const ChartSlice &slice : m_slices) {
                if (slice.entry.path.compare(node->entry.path, Qt::CaseInsensitive) == 0) {
                    return &slice;
                }
            }
        }
        return nullptr;
    case ViewMode::Pie:
    default:
        break;
    }

    return nullptr;
}

const ChartPanel::ChartSlice *ChartPanel::hoverSliceAt(ViewMode mode, const QPoint &point, const QRect &rect) const
{
    if (mode == ViewMode::Pie) {
        return sliceAt(mode, point, rect);
    }
    return sliceAt(mode, point, rect);
}

QVector<ChartPanel::TreemapNode> ChartPanel::buildTreemapNodes(const QString &rootPath, int remainingDepth, int depth) const
{
    if (remainingDepth <= 0) {
        return {};
    }

    QVector<TreemapNode> nodes;
    int colorIndex = depth * 4;
    qint64 otherBytes = 0;
    qint64 parentSize = m_activeFolderSize;
    if (depth > 0) {
        for (const TreeEntry &candidate : m_result.treeEntries) {
            if (candidate.path.compare(rootPath, Qt::CaseInsensitive) == 0) {
                parentSize = candidate.size;
                break;
            }
        }
    }
    for (const TreeEntry &entry : m_result.treeEntries) {
        if (entry.parentPath.compare(rootPath, Qt::CaseInsensitive) != 0) {
            continue;
        }

        const double percent = parentSize <= 0 ? 0.0 : (100.0 * double(entry.size) / double(parentSize));
        if (percent < m_otherThresholdPercent) {
            otherBytes += entry.size;
            continue;
        }

        TreemapNode node;
        node.entry = entry;
        node.label = displayNameForPath(entry.path);
        node.size = entry.size;
        node.color = chartColor(colorIndex++);
        node.activatable = true;
        if (entry.kind == TreeEntryKind::Folder) {
            node.children = buildTreemapNodes(entry.path, remainingDepth - 1, depth + 1);
        }
        nodes.push_back(node);
    }

    if (otherBytes > 0) {
        TreemapNode other;
        other.label = QStringLiteral("Other");
        other.size = otherBytes;
        other.color = QColor(120, 120, 120);
        other.activatable = false;
        nodes.push_back(other);
    }

    std::sort(nodes.begin(), nodes.end(), [](const TreemapNode &left, const TreemapNode &right) {
        return left.size > right.size;
    });
    return nodes;
}

void ChartPanel::layoutTreemapNodes(QVector<TreemapNode> &nodes, const QRectF &rect, int depth) const
{
    Q_UNUSED(depth);
    QRectF remaining = rect;
    qint64 remainingTotal = 0;
    for (const TreemapNode &node : nodes) {
        remainingTotal += node.size;
    }

    for (int index = 0; index < nodes.size(); ++index) {
        TreemapNode &node = nodes[index];
        QRectF sliceRect;
        if (index == nodes.size() - 1 || remainingTotal <= 0) {
            sliceRect = remaining;
        } else {
            const double ratio = double(node.size) / double(remainingTotal);
            if (remaining.width() >= remaining.height()) {
                const double width = remaining.width() * ratio;
                sliceRect = QRectF(remaining.left(), remaining.top(), width, remaining.height());
                remaining.adjust(width, 0, 0, 0);
            } else {
                const double height = remaining.height() * ratio;
                sliceRect = QRectF(remaining.left(), remaining.top(), remaining.width(), height);
                remaining.adjust(0, height, 0, 0);
            }
        }

        node.rect = sliceRect.adjusted(2, 2, -2, -2);
        remainingTotal -= node.size;
        const double minArea = 300.0;
        if (!node.children.isEmpty() && node.rect.width() >= 28.0 && node.rect.height() >= 28.0
            && node.rect.width() * node.rect.height() >= minArea) {
            layoutTreemapNodes(node.children, node.rect.adjusted(4, 18, -4, -4), depth + 1);
        } else if (!node.children.isEmpty()) {
            node.children.clear();
        }
    }
}

void ChartPanel::paintTreemapNodes(QPainter &painter, const QVector<TreemapNode> &nodes, int depth) const
{
    Q_UNUSED(depth);
    for (const TreemapNode &node : nodes) {
        painter.setPen(QPen(QColor(25, 30, 40), 1));
        painter.setBrush(node.color);
        painter.drawRect(node.rect);
        if (node.rect.width() > 80 && node.rect.height() > 30) {
            painter.setPen(Qt::white);
            painter.drawText(node.rect.adjusted(6, 4, -6, -4), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                QStringLiteral("%1\n%2\n%3%")
                    .arg(node.label,
                         m_viewMetric == ViewMetric::Files
                             ? QString::number(node.entry.kind == TreeEntryKind::Folder ? node.entry.fileCount : 1) + QStringLiteral(" files")
                             : SizeFormatter::formatBytes(node.size),
                         QString::number(m_activeFolderSize <= 0 ? 0.0 : (100.0 * double(node.size) / double(m_activeFolderSize)), 'f', 1)));
        }
        if (!node.children.isEmpty()) {
            paintTreemapNodes(painter, node.children, depth + 1);
        }
    }
}

const ChartPanel::TreemapNode *ChartPanel::treemapNodeAt(const QPoint &point, const QVector<TreemapNode> &nodes) const
{
    for (const TreemapNode &node : nodes) {
        if (node.rect.contains(point)) {
            if (const TreemapNode *child = treemapNodeAt(point, node.children)) {
                return child;
            }
            return &node;
        }
    }
    return nullptr;
}

}
