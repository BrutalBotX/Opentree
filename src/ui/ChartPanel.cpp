#include "ui/ChartPanel.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCompleter>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QStorageInfo>
#include <QToolTip>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

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

// Worst aspect ratio of a row of areas laid along a strip of the given length; the
// squarified treemap keeps this as close to 1 as possible so tiles stay rectangular.
double worstAspectRatio(const QVector<double> &areas, double side)
{
    if (areas.isEmpty() || side <= 0.0) {
        return std::numeric_limits<double>::max();
    }

    double total = 0.0;
    double maxArea = 0.0;
    double minArea = std::numeric_limits<double>::max();
    for (double area : areas) {
        total += area;
        maxArea = std::max(maxArea, area);
        minArea = std::min(minArea, area);
    }
    if (total <= 0.0 || minArea <= 0.0) {
        return std::numeric_limits<double>::max();
    }

    const double sideSquared = side * side;
    const double totalSquared = total * total;
    return std::max(sideSquared * maxArea / totalSquared, totalSquared / (sideSquared * minArea));
}

// Pick a text color that stays readable on the given tile color.
QColor readableTextColor(const QColor &background)
{
    const double luminance = 0.299 * background.red() + 0.587 * background.green() + 0.114 * background.blue();
    return luminance > 150.0 ? QColor(24, 28, 36) : QColor(255, 255, 255);
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
    , m_freeSpaceCheck(new QCheckBox(QStringLiteral("Include free space"), this))
    , m_treemapDepthCombo(new QComboBox(this))
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
    m_freeSpaceCheck->setToolTip(QStringLiteral("When scanning a drive root, add the volume's free space to the charts"));
    layout->addWidget(m_summaryLabel);

    auto *addressRow = new QHBoxLayout;
    addressRow->setContentsMargins(0, 0, 0, 0);
    addressRow->addWidget(m_addressBar, 1);
    addressRow->addWidget(m_freeSpaceCheck, 0);
    addressRow->addWidget(m_treemapDepthCombo, 0);
    layout->addLayout(addressRow);

    m_addressBar->setPlaceholderText("Enter folder path...");
    m_subtabs->addTab(m_pieView, "Pie");
    m_subtabs->addTab(m_barView, "Bars");
    m_subtabs->addTab(m_treemapView, "Treemap");
    layout->addWidget(m_subtabs, 1);
    connect(m_addressBar, &QLineEdit::returnPressed, this, &ChartPanel::handleAddressSubmitted);
    connect(m_freeSpaceCheck, &QCheckBox::toggled, this, [this](bool) { rebuild(); });

    // Treemap depth lives in the Treemap subtab so it is discoverable there.
    m_treemapDepthCombo->addItem(QStringLiteral("Depth 1"), 1);
    m_treemapDepthCombo->addItem(QStringLiteral("Depth 2"), 2);
    m_treemapDepthCombo->addItem(QStringLiteral("Depth 3"), 3);
    m_treemapDepthCombo->setToolTip(QStringLiteral("How many levels of subfolders the treemap subdivides"));
    connect(m_treemapDepthCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        setTreemapDepth(m_treemapDepthCombo->currentData().toInt());
    });
    connect(m_subtabs, &QTabWidget::currentChanged, this, [this](int index) {
        m_treemapDepthCombo->setVisible(index == 2);
    });
    m_treemapDepthCombo->setVisible(m_subtabs->currentIndex() == 2);
    setTreemapDepth(m_treemapDepth);

    rebuild();
}

void ChartPanel::setIncludeFreeSpace(bool enabled)
{
    if (m_freeSpaceCheck->isChecked() == enabled) {
        return;
    }
    m_freeSpaceCheck->setChecked(enabled);
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

    if (m_treemapDepthCombo) {
        const QSignalBlocker blocker(m_treemapDepthCombo);
        const int index = m_treemapDepthCombo->findData(clamped);
        if (index >= 0) {
            m_treemapDepthCombo->setCurrentIndex(index);
        }
    }

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

    // Optionally show the volume's free space as its own slice (drive roots only).
    qint64 freeSpace = 0;
    if (m_freeSpaceCheck->isChecked()) {
        const QString rootPath = QDir(m_activeFolderPath).rootPath();
        if (rootPath.compare(m_activeFolderPath, Qt::CaseInsensitive) == 0) {
            const QStorageInfo info(m_activeFolderPath);
            if (info.isValid() && info.isReady()) {
                freeSpace = info.bytesFree();
            }
        }
    }
    if (freeSpace > 0) {
        m_activeFolderSize += freeSpace;
    }

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
        const double fraction = m_activeFolderSize <= 0 ? 0.0 : double(child.size) / double(m_activeFolderSize);
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
        const double fraction = m_activeFolderSize <= 0 ? 0.0 : double(file.size) / double(m_activeFolderSize);
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
        const double fraction = m_activeFolderSize <= 0 ? 0.0 : double(otherFolderBytes) / double(m_activeFolderSize);
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
        const double fraction = m_activeFolderSize <= 0 ? 0.0 : double(otherFileBytes) / double(m_activeFolderSize);
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

    if (freeSpace > 0) {
        const double fraction = m_activeFolderSize <= 0 ? 0.0 : double(freeSpace) / double(m_activeFolderSize);
        ChartSlice slice;
        slice.label = QStringLiteral("Free space");
        slice.size = freeSpace;
        slice.activatable = false;
        slice.startAngle = currentAngle;
        slice.spanAngle = fraction * 360.0;
        slice.color = QColor(84, 90, 104);
        m_slices.push_back(slice);
        currentAngle += slice.spanAngle;
    }

    m_treemapNodes = buildTreemapNodes(m_activeFolderPath, m_treemapDepth, 0);

    m_summaryLabel->setText(QStringLiteral("Charts | Total: %1 | Other cutoff: %2% | Treemap depth: %3%4")
                                .arg(SizeFormatter::formatBytes(m_activeFolderSize))
                                .arg(QString::number(m_otherThresholdPercent, 'f', 1))
                                .arg(m_treemapDepth)
                                .arg(freeSpace > 0
                                         ? QStringLiteral(" | includes %1 free").arg(SizeFormatter::formatBytes(freeSpace))
                                         : QString()));
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
    const QRectF fullRect(rect);
    if (fullRect.width() < 60.0 || fullRect.height() < 60.0) {
        return;
    }

    const QRectF area = fullRect.adjusted(12, 12, -12, -12);
    QFontMetrics fm(painter.font());

    // Measure the labels first so the gutter only reserves the room actually needed;
    // a fixed fraction of the width needlessly shrank the pie.
    struct PieLabel {
        int index = 0;
        QPointF anchor;
        double cosA = 0.0;
        double sinA = 0.0;
        QString text;
        double width = 0.0;
    };

    QVector<PieLabel> leftLabels;
    QVector<PieLabel> rightLabels;
    double widest = 0.0;

    for (int i = 0; i < m_slices.size(); ++i) {
        const ChartSlice &slice = m_slices[i];
        const double pct = m_activeFolderSize <= 0 ? 0.0 : (100.0 * double(slice.size) / double(m_activeFolderSize));
        if (pct < 0.05) {
            continue;
        }

        const double midRad = (slice.startAngle + slice.spanAngle / 2.0) * M_PI / 180.0;
        PieLabel label;
        label.index = i;
        label.cosA = std::cos(midRad);
        label.sinA = std::sin(midRad);
        label.text = QStringLiteral("%1 (%2%)")
                         .arg(slice.label, QString::number(pct, 'f', pct < 10.0 ? 1 : 0));
        label.width = fm.horizontalAdvance(label.text);
        widest = std::max(widest, label.width);
        (label.cosA >= 0.0 ? rightLabels : leftLabels).push_back(label);
    }

    // Reserve side gutters so labels always sit outside the pie instead of on top of it.
    // Cap the measured width so one long name cannot shrink the whole pie: such labels
    // are elided to the gutter instead.
    const double labelWidthCap = std::max(96.0, area.width() * 0.22);
    const double gutter = std::clamp(std::min(widest, labelWidthCap) + 26.0, 96.0, area.width() * 0.34);
    const double availablePieW = std::max(90.0, area.width() - (gutter + 20.0) * 2.0);
    const double availablePieH = std::max(90.0, area.height() - 16.0);
    const double pieSide = std::max(80.0, std::min(availablePieW, availablePieH));
    const QRectF pie(area.center().x() - pieSide / 2.0, area.center().y() - pieSide / 2.0, pieSide, pieSide);
    const double pieRadius = pieSide / 2.0;

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

    painter.save();
    painter.setClipRect(fullRect.adjusted(2, 2, -2, -2));

    const double labelH = std::max(16.0, double(fm.height()) + 2.0);
    const double minStep = labelH + 2.0;
    const double topY = area.top() + labelH / 2.0;
    const double bottomY = area.bottom() - labelH / 2.0;

    for (PieLabel &label : leftLabels) {
        label.anchor = QPointF(pie.center().x() + pieRadius * label.cosA,
                               pie.center().y() + pieRadius * label.sinA);
    }
    for (PieLabel &label : rightLabels) {
        label.anchor = QPointF(pie.center().x() + pieRadius * label.cosA,
                               pie.center().y() + pieRadius * label.sinA);
    }

    struct Placement {
        int index = 0;
        QPointF anchor;
        QPointF elbow;
        QPointF lineEnd;
        QRectF box;
        QString text;
        bool rightSide = true;
    };
    QVector<Placement> placements;

    auto layoutGroup = [&](QVector<PieLabel> &group, bool rightSide) {
        if (group.isEmpty()) {
            return;
        }

        // If the column cannot fit every label, keep the largest slices.
        const int maxLabels = std::max(1, int((bottomY - topY) / minStep) + 1);
        if (group.size() > maxLabels) {
            std::sort(group.begin(), group.end(), [this](const PieLabel &a, const PieLabel &b) {
                return m_slices[a.index].size > m_slices[b.index].size;
            });
            group.resize(maxLabels);
        }

        std::sort(group.begin(), group.end(), [](const PieLabel &a, const PieLabel &b) {
            return a.anchor.y() < b.anchor.y();
        });

        QVector<double> ys;
        ys.reserve(group.size());

        // Elide long names to the gutter width; mark unrenderable ones for skipping.
        QVector<PieLabel> visible;
        visible.reserve(group.size());
        for (PieLabel &label : group) {
            label.text = fm.elidedText(label.text, Qt::ElideMiddle, int(gutter));
            if (label.text.isEmpty()) {
                continue;
            }
            label.width = fm.horizontalAdvance(label.text);
            visible.push_back(label);
            ys.push_back(std::clamp(label.anchor.y(), topY, bottomY));
        }
        if (visible.isEmpty()) {
            return;
        }

        // Spread vertically so labels never overlap.
        for (int i = 1; i < ys.size(); ++i) {
            ys[i] = std::max(ys[i], ys[i - 1] + minStep);
        }
        for (int i = int(ys.size()) - 2; i >= 0; --i) {
            ys[i] = std::min(ys[i], ys[i + 1] - minStep);
        }
        if (ys.first() < topY) {
            const double shift = topY - ys.first();
            for (double &y : ys) {
                y += shift;
            }
        }
        if (ys.last() > bottomY) {
            const double shift = ys.last() - bottomY;
            for (double &y : ys) {
                y -= shift;
            }
        }

        for (int i = 0; i < visible.size(); ++i) {
            const PieLabel &label = visible[i];
            const double labelY = ys[i];
            const QPointF elbow(label.anchor.x() + 14.0 * label.cosA,
                                label.anchor.y() + 14.0 * label.sinA);

            double textX = 0.0;
            if (rightSide) {
                textX = std::min(pie.right() + 18.0, area.right() - label.width);
                textX = std::max(textX, pie.right() + 6.0);
            } else {
                textX = std::max(pie.left() - 18.0 - label.width, area.left());
                textX = std::min(textX, pie.left() - 6.0 - label.width);
            }

            Placement placement;
            placement.index = label.index;
            placement.anchor = label.anchor;
            placement.elbow = elbow;
            placement.box = QRectF(textX, labelY - labelH / 2.0, label.width + 2.0, labelH);
            placement.lineEnd = QPointF(rightSide ? placement.box.left() - 4.0 : placement.box.right() + 4.0, labelY);
            placement.text = label.text;
            placement.rightSide = rightSide;
            placements.push_back(placement);
        }
    };

    layoutGroup(rightLabels, true);
    layoutGroup(leftLabels, false);

    // Leader lines first so the text stays readable on top of them.
    for (const Placement &placement : placements) {
        const ChartSlice &slice = m_slices[placement.index];
        painter.setPen(QPen(slice.color, 1.2));
        painter.drawPolyline(QPolygonF({placement.anchor, placement.elbow, placement.lineEnd}));
    }

    painter.setPen(palette().text().color());
    for (const Placement &placement : placements) {
        painter.drawText(placement.box,
                         placement.rightSide ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignRight | Qt::AlignVCenter,
                         placement.text);

        ChartSlice &slice = const_cast<QVector<ChartSlice> &>(m_slices)[placement.index];
        slice.pieLabelVisible = true;
        slice.pieAnchor = placement.anchor;
        slice.pieElbow = placement.elbow;
        slice.pieLabelRect = placement.box;
    }

    painter.restore();
}

void ChartPanel::paintBars(QPainter &painter, const QRect &rect) const
{
    const QRectF area = QRectF(rect).adjusted(20, 16, -20, -16);
    if (area.width() <= 40.0 || area.height() <= 20.0) {
        return;
    }

    for (const ChartSlice &slice : const_cast<QVector<ChartSlice> &>(m_slices)) {
        slice.barRect = {};
    }

    const double gap = 10.0;
    const double valueWidth = std::clamp(area.width() * 0.22, 90.0, 165.0);
    const double labelWidth = std::clamp(area.width() * 0.30, 110.0, 260.0);
    const double chartLeft = area.left() + labelWidth + gap;
    const double chartRight = area.right() - valueWidth - gap;
    const double chartWidth = std::max(30.0, chartRight - chartLeft);

    // Only draw the rows that actually fit, so nothing is silently clipped away.
    const double rowHeight = 26.0;
    const int capacity = std::max(1, int(std::floor(area.height() / rowHeight)));
    const int shown = std::min(int(m_slices.size()), capacity);
    const int hidden = int(m_slices.size()) - shown;

    double totalFileValue = 1.0;
    if (m_viewMetric == ViewMetric::Files) {
        totalFileValue = 0.0;
        for (const ChartSlice &slice : m_slices) {
            totalFileValue += (slice.entry.kind == TreeEntryKind::Folder ? double(slice.entry.fileCount) : 1.0);
        }
        totalFileValue = std::max(1.0, totalFileValue);
    }

    QFontMetrics fm(painter.font());

    // Bars are ordered by the active metric; the slice vector itself stays in pie order.
    QVector<int> order;
    order.reserve(m_slices.size());
    for (int i = 0; i < m_slices.size(); ++i) {
        order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [this](int left, int right) {
        const auto valueOf = [this](int index) -> double {
            const ChartSlice &slice = m_slices[index];
            if (m_viewMetric == ViewMetric::Files) {
                return slice.entry.kind == TreeEntryKind::Folder ? double(slice.entry.fileCount) : 1.0;
            }
            return double(slice.size);
        };
        return valueOf(left) > valueOf(right);
    });

    for (int position = 0; position < shown; ++position) {
        const int index = order[position];
        const ChartSlice &slice = m_slices[index];
        const double top = area.top() + position * rowHeight;
        const double barHeight = std::min(16.0, rowHeight - 9.0);
        const double barTop = top + (rowHeight - barHeight) / 2.0;

        double ratio = m_activeFolderSize <= 0 ? 0.0 : double(slice.size) / double(m_activeFolderSize);
        if (m_viewMetric == ViewMetric::Files) {
            const double fileValue = slice.entry.kind == TreeEntryKind::Folder ? double(slice.entry.fileCount) : 1.0;
            ratio = fileValue / totalFileValue;
        }
        ratio = std::clamp(ratio, 0.0, 1.0);

        slice.barRect = QRectF(chartLeft, barTop, std::max(1.5, chartWidth * ratio), barHeight);

        painter.setPen(palette().text().color());
        painter.drawText(QRectF(area.left(), top, labelWidth, rowHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         fm.elidedText(slice.label, Qt::ElideMiddle, int(labelWidth)));

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(55, 62, 78));
        painter.drawRoundedRect(QRectF(chartLeft, barTop, chartWidth, barHeight), 4, 4);
        painter.setBrush(slice.color);
        painter.drawRoundedRect(slice.barRect, 4, 4);

        const QString valueText = (m_viewMetric == ViewMetric::Files
                                      ? QString::number(slice.entry.kind == TreeEntryKind::Folder ? slice.entry.fileCount : 1) + QStringLiteral(" files")
                                      : SizeFormatter::formatBytes(slice.size))
            + QStringLiteral("  (%1%)").arg(ratio * 100.0, 0, 'f', 1);
        painter.setPen(palette().text().color());
        painter.drawText(QRectF(chartRight + gap, top, valueWidth, rowHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         fm.elidedText(valueText, Qt::ElideRight, int(valueWidth)));
    }

    if (hidden > 0) {
        painter.setPen(QColor(140, 150, 170));
        painter.drawText(QRectF(area.left(), area.bottom() - rowHeight, area.width(), rowHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("... and %1 more").arg(hidden));
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

    // The volume's free space only makes sense as a sibling of the top-level children.
    if (depth == 0 && m_freeSpaceCheck->isChecked()) {
        const QString volumeRoot = QDir(m_activeFolderPath).rootPath();
        if (volumeRoot.compare(m_activeFolderPath, Qt::CaseInsensitive) == 0) {
            const QStorageInfo info(m_activeFolderPath);
            if (info.isValid() && info.isReady() && info.bytesFree() > 0) {
                TreemapNode free;
                free.label = QStringLiteral("Free space");
                free.size = info.bytesFree();
                free.color = QColor(84, 90, 104);
                free.activatable = false;
                nodes.push_back(free);
            }
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const TreemapNode &left, const TreemapNode &right) {
        return left.size > right.size;
    });
    return nodes;
}

void ChartPanel::layoutTreemapNodes(QVector<TreemapNode> &nodes, const QRectF &bounds, int depth) const
{
    Q_UNUSED(depth);
    if (nodes.isEmpty() || bounds.width() <= 4.0 || bounds.height() <= 4.0) {
        return;
    }

    qint64 total = 0;
    for (const TreemapNode &node : nodes) {
        total += std::max<qint64>(0, node.size);
    }
    if (total <= 0) {
        return;
    }

    const double areaPerByte = (bounds.width() * bounds.height()) / double(total);

    // Squarified layout: grow a row while it keeps the tiles close to square, then lay
    // the row along the short side of the remaining space.
    QRectF free = bounds;
    int index = 0;
    while (index < nodes.size()) {
        const double shortSide = std::max(1.0, std::min(free.width(), free.height()));
        QVector<int> row;
        QVector<double> rowAreas;
        double rowArea = 0.0;
        double bestWorst = std::numeric_limits<double>::max();

        while (index < nodes.size()) {
            const double area = std::max(1.0, nodes[index].size * areaPerByte);
            QVector<double> candidate = rowAreas;
            candidate.append(area);
            const double worst = worstAspectRatio(candidate, shortSide);
            if (row.isEmpty() || worst <= bestWorst) {
                row.append(index);
                rowAreas.append(area);
                rowArea += area;
                bestWorst = worst;
                ++index;
            } else {
                break;
            }
        }

        if (free.width() >= free.height()) {
            const double rowWidth = std::min(free.width(), std::max(1.0, rowArea / std::max(1.0, free.height())));
            double y = free.top();
            for (int k = 0; k < row.size(); ++k) {
                const double height = (k == row.size() - 1)
                    ? std::max(1.0, free.bottom() - y)
                    : std::max(1.0, rowAreas[k] / rowWidth);
                nodes[row[k]].rect = QRectF(free.left(), y, rowWidth, height);
                y += height;
            }
            free.adjust(rowWidth, 0, 0, 0);
        } else {
            const double rowHeight = std::min(free.height(), std::max(1.0, rowArea / std::max(1.0, free.width())));
            double x = free.left();
            for (int k = 0; k < row.size(); ++k) {
                const double width = (k == row.size() - 1)
                    ? std::max(1.0, free.right() - x)
                    : std::max(1.0, rowAreas[k] / rowHeight);
                nodes[row[k]].rect = QRectF(x, free.top(), width, rowHeight);
                x += width;
            }
            free.adjust(0, rowHeight, 0, 0);
        }
    }

    // Once the parents are placed, subdivide the tiles that are large enough.
    for (TreemapNode &node : nodes) {
        if (node.children.isEmpty()) {
            continue;
        }

        QRectF childRect = node.rect.adjusted(2, 2, -2, -2);
        if (childRect.width() >= 34.0 && childRect.height() >= 40.0 && childRect.width() * childRect.height() >= 900.0) {
            childRect.adjust(1, 15, -1, -1); // reserve the header strip
            layoutTreemapNodes(node.children, childRect, depth + 1);
        } else {
            node.children.clear();
        }
    }
}

void ChartPanel::paintTreemapNodes(QPainter &painter, const QVector<TreemapNode> &nodes, int depth) const
{
    Q_UNUSED(depth);
    const QFont baseFont = painter.font();

    for (const TreemapNode &node : nodes) {
        if (node.rect.width() < 2.0 || node.rect.height() < 2.0) {
            continue;
        }

        painter.setPen(QPen(QColor(18, 22, 30), 1));
        painter.setBrush(node.color);
        painter.drawRect(node.rect);

        const bool hasChildren = !node.children.isEmpty();

        // Always clip to the tile: labels used to wrap and spill into their neighbours.
        painter.save();
        painter.setClipRect(node.rect.adjusted(1, 1, -1, -1));
        painter.setPen(readableTextColor(node.color));

        if (hasChildren) {
            QFont headerFont = baseFont;
            headerFont.setBold(true);
            painter.setFont(headerFont);
            const QFontMetrics metrics(headerFont);
            const QRectF header(node.rect.left() + 4.0, node.rect.top() + 1.0,
                                std::max(0.0, node.rect.width() - 8.0), 14.0);
            if (header.width() >= 24.0) {
                painter.drawText(header, Qt::AlignLeft | Qt::AlignVCenter,
                                 metrics.elidedText(node.label, Qt::ElideMiddle, int(header.width())));
            }
        } else if (node.rect.width() >= 34.0 && node.rect.height() >= 20.0) {
            const QFontMetrics metrics(baseFont);
            const QRectF inner = node.rect.adjusted(4, 3, -4, -3);
            const int lineHeight = metrics.height();
            const int lineCount = inner.height() >= lineHeight * 2.6 ? 3
                                 : (inner.height() >= lineHeight * 1.6 ? 2 : 1);

            QVector<QString> lines;
            lines << metrics.elidedText(node.label, Qt::ElideMiddle, int(inner.width()));
            if (lineCount >= 2) {
                lines << (m_viewMetric == ViewMetric::Files
                              ? QString::number(node.entry.kind == TreeEntryKind::Folder ? node.entry.fileCount : 1) + QStringLiteral(" files")
                              : SizeFormatter::formatBytes(node.size));
            }
            if (lineCount >= 3) {
                lines << QStringLiteral("%1%").arg(QString::number(
                    m_activeFolderSize <= 0 ? 0.0 : (100.0 * double(node.size) / double(m_activeFolderSize)), 'f', 1));
            }

            double y = inner.top();
            for (const QString &line : lines) {
                painter.drawText(QRectF(inner.left(), y, inner.width(), lineHeight),
                                 Qt::AlignLeft | Qt::AlignVCenter, line);
                y += lineHeight;
            }
        }
        painter.restore();

        if (hasChildren) {
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
