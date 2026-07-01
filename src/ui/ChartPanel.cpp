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
            if (const auto *slice = m_owner->sliceAt(m_mode, event->pos(), rect()); slice && slice->activatable) {
                emit m_owner->entryActivated(slice->entry);
            }
        }
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_owner) {
            if (const auto *slice = m_owner->sliceAt(m_mode, event->pos(), rect()); slice && slice->activatable) {
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

        const auto *slice = m_owner->sliceAt(m_mode, event->pos(), rect());
        if (!slice || !slice->activatable) {
            QWidget::contextMenuEvent(event);
            return;
        }

        QMenu menu(this);
        QAction *openGraphAction = menu.addAction("Open in Graph View");
        QAction *selectedAction = menu.exec(event->globalPos());
        if (selectedAction == openGraphAction) {
            emit m_owner->entryOpenInGraphRequested(slice->entry);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_owner) {
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
    , m_pathModel(new QFileSystemModel(this))
    , m_pathCompleter(new QCompleter(m_pathModel, this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);
    m_addressBar->setPlaceholderText("Enter folder path...");
    m_pathModel->setFilter(QDir::AllDirs | QDir::Drives | QDir::NoDotAndDotDot);
    m_pathModel->setRootPath(QString());
    m_pathCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_pathCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_addressBar->setCompleter(m_pathCompleter);
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

    m_summaryLabel->setText(QStringLiteral("Charts | Total: %1 | Other cutoff: %2%")
                                .arg(SizeFormatter::formatBytes(activeEntry.size))
                                .arg(QString::number(m_otherThresholdPercent, 'f', 1)));
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
    const QRectF area = QRectF(rect).adjusted(20, 18, -20, -18);
    const double legendWidth = std::min(280.0, area.width() * 0.40);
    const QRectF pieArea(area.left(), area.top(), area.width() - legendWidth - 16.0, area.height());
    const QRectF legendArea(pieArea.right() + 16.0, area.top(), legendWidth, area.height());
    const double pieSide = std::max(160.0, std::min(pieArea.width() - 12.0, pieArea.height() - 12.0));
    const QRectF pie(pieArea.center().x() - (pieSide / 2.0), pieArea.center().y() - (pieSide / 2.0), pieSide, pieSide);

    painter.setPen(QPen(QColor(70, 80, 100), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(pie);
    for (const ChartSlice &slice : m_slices) {
        painter.setBrush(slice.color);
        painter.setPen(Qt::NoPen);
        painter.drawPie(pie, int(-slice.startAngle * 16.0), int(-slice.spanAngle * 16.0));
    }

    for (ChartSlice &slice : const_cast<QVector<ChartSlice> &>(m_slices)) {
        slice.pieLabelVisible = false;
        slice.pieAnchor = {};
        slice.pieElbow = {};
        slice.pieLabelRect = {};
    }

    painter.setPen(QPen(palette().mid().color(), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(legendArea);

    const double rowHeight = 28.0;
    const int maxRows = std::max(1, int((legendArea.height() - 12.0) / rowHeight));
    const int visibleCount = std::min(maxRows, int(m_slices.size()));
    for (int index = 0; index < visibleCount; ++index) {
        ChartSlice &slice = const_cast<QVector<ChartSlice> &>(m_slices)[index];
        const double top = legendArea.top() + 8.0 + (index * rowHeight);
        const QRectF rowRect(legendArea.left() + 6.0, top, legendArea.width() - 12.0, rowHeight - 4.0);
        const QRectF swatchRect(rowRect.left() + 6.0, rowRect.top() + 6.0, 14.0, 14.0);
        const QRectF textRect(swatchRect.right() + 10.0, rowRect.top(), rowRect.width() - 34.0, rowRect.height());

        slice.pieLabelVisible = true;
        slice.pieLabelRect = rowRect;

        painter.setBrush(slice.color);
        painter.setPen(Qt::NoPen);
        painter.drawRect(swatchRect);
        painter.setPen(palette().text().color());
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
            QStringLiteral("%1 (%2%)")
                .arg(slice.label,
                     QString::number(m_activeFolderSize <= 0 ? 0.0 : (100.0 * double(slice.size) / double(m_activeFolderSize)), 'f', 1)));
    }
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
    QRectF remaining = QRectF(rect).adjusted(16, 16, -16, -16);
    qint64 remainingTotal = 0;
    for (const ChartSlice &slice : m_slices) {
        remainingTotal += slice.size;
    }

    for (int index = 0; index < m_slices.size(); ++index) {
        const ChartSlice &slice = m_slices[index];
        QRectF sliceRect;
        if (index == m_slices.size() - 1 || remainingTotal <= 0) {
            sliceRect = remaining;
        } else {
            const double ratio = double(slice.size) / double(remainingTotal);
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

        slice.treemapRect = sliceRect.adjusted(2, 2, -2, -2);
        remainingTotal -= slice.size;

        painter.setPen(QPen(QColor(25, 30, 40), 1));
        painter.setBrush(slice.color);
        painter.drawRect(slice.treemapRect);
        if (slice.treemapRect.width() > 80 && slice.treemapRect.height() > 30) {
            painter.setPen(Qt::white);
            painter.drawText(slice.treemapRect.adjusted(6, 4, -6, -4), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                QStringLiteral("%1\n%2\n%3%")
                    .arg(slice.label,
                         m_viewMetric == ViewMetric::Files
                             ? QString::number(slice.entry.kind == TreeEntryKind::Folder ? slice.entry.fileCount : 1) + QStringLiteral(" files")
                             : SizeFormatter::formatBytes(slice.size),
                         QString::number(m_activeFolderSize <= 0 ? 0.0 : (100.0 * double(slice.size) / double(m_activeFolderSize)), 'f', 1)));
        }
    }
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
        for (const ChartSlice &slice : m_slices) {
            if (slice.activatable && slice.treemapRect.contains(point)) {
                return &slice;
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

}
