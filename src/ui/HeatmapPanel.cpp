#include "ui/HeatmapPanel.h"

#include <QColor>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>

#include "ui/TableItems.h"
#include "utils/SizeFormatter.h"

namespace opentree {

namespace {

bool samePath(const QString &left, const QString &right)
{
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

// Blue heat scale by share of the parent folder; growth/shrink override it so the
// delta is readable at a glance.
QColor heatColor(double percent, qint64 delta)
{
    if (delta > 0) {
        return QColor(176, 58, 58);
    }
    if (delta < 0) {
        return QColor(52, 142, 88);
    }

    const double t = std::clamp(percent / 100.0, 0.0, 1.0);
    const int red = int(34 + t * 70);
    const int green = int(52 + t * 110);
    const int blue = int(92 + t * 160);
    return QColor(red, green, blue);
}

} // namespace

HeatmapPanel::HeatmapPanel(QWidget *parent)
    : QWidget(parent)
    , m_summaryLabel(new QLabel(this))
    , m_table(new QTableWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    m_summaryLabel->setWordWrap(true);

    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({"Folder", "% of Parent", "Size", "Files", "Delta"});
    // Fixed widths: Qt's header size hint ignores stylesheet padding, which clipped the
    // header text once a sort indicator was added.
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 5; ++column) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    m_table->setColumnWidth(1, 130);
    m_table->setColumnWidth(2, 120);
    m_table->setColumnWidth(3, 80);
    m_table->setColumnWidth(4, 110);
    configureStandardTable(m_table);
    layout->addWidget(m_summaryLabel);
    layout->addWidget(m_table, 1);

    rebuild();
}

void HeatmapPanel::setViewMetric(ViewMetric metric)
{
    m_viewMetric = metric;
    if (metric == ViewMetric::Files) {
        m_table->sortByColumn(3, Qt::DescendingOrder);
    } else if (metric == ViewMetric::Size) {
        m_table->sortByColumn(2, Qt::DescendingOrder);
    } else {
        m_table->sortByColumn(1, Qt::DescendingOrder);
    }
}

void HeatmapPanel::setHeatmapData(const QVector<TreeEntry> &entries, const QVector<SnapshotCompareRow> &compareRows)
{
    m_entries = entries;
    m_compareRows = compareRows;
    rebuild();
}

void HeatmapPanel::setActiveFolderPath(const QString &path)
{
    if (samePath(m_activeFolderPath, path)) {
        return;
    }
    m_activeFolderPath = path;
    rebuild();
}

void HeatmapPanel::rebuild()
{
    if (m_entries.isEmpty() || m_activeFolderPath.isEmpty()) {
        m_summaryLabel->setText(QStringLiteral("Heatmap: scan a folder to see its immediate child folders."));
        m_table->setRowCount(0);
        return;
    }

    QHash<QString, qint64> deltaByPath;
    for (const SnapshotCompareRow &row : m_compareRows) {
        deltaByPath.insert(row.path, row.deltaBytes);
    }

    // Only the folders directly inside the selected folder.
    QVector<TreeEntry> children;
    qint64 childrenSize = 0;
    qint64 parentSize = 0;
    for (const TreeEntry &entry : m_entries) {
        if (entry.kind != TreeEntryKind::Folder) {
            continue;
        }
        if (samePath(entry.path, m_activeFolderPath)) {
            parentSize = entry.size;
            continue;
        }
        if (samePath(entry.parentPath, m_activeFolderPath)) {
            children.push_back(entry);
            childrenSize += entry.size;
        }
    }

    std::sort(children.begin(), children.end(), [](const TreeEntry &left, const TreeEntry &right) {
        return left.size > right.size;
    });
    if (children.size() > 40) {
        children.resize(40);
    }

    const qint64 denominator = parentSize > 0 ? parentSize : childrenSize;
    {
    TableSortGuard sortGuard(m_table);
    m_table->setRowCount(children.size());
    for (int rowIndex = 0; rowIndex < children.size(); ++rowIndex) {
        const TreeEntry &entry = children[rowIndex];
        const qint64 delta = deltaByPath.value(entry.path, 0);
        const double percent = denominator <= 0 ? 0.0 : (100.0 * double(entry.size) / double(denominator));

        QTableWidgetItem *nameItem = makeTextItem(entry.name.isEmpty() ? entry.path : entry.name, entry.path);
        QTableWidgetItem *percentItem = makePercentItem(percent);
        QTableWidgetItem *sizeItem = makeNumberItem(SizeFormatter::formatBytes(entry.size), entry.size);
        QTableWidgetItem *filesItem = makeNumberItem(QString::number(entry.fileCount), entry.fileCount);
        QTableWidgetItem *deltaItem = makeNumberItem(
            delta == 0 ? QStringLiteral("-")
                       : QStringLiteral("%1%2").arg(delta > 0 ? QStringLiteral("+") : QStringLiteral("-"),
                                                   SizeFormatter::formatBytes(std::abs(delta))),
            delta);

        // Shade the whole row so the table reads like a heat map.
        const QColor tint = heatColor(percent, delta);
        const QColor textColor = tint.lightness() < 120 ? QColor(255, 255, 255) : QColor(20, 26, 36);
        for (QTableWidgetItem *item : {static_cast<QTableWidgetItem *>(nameItem),
                                       static_cast<QTableWidgetItem *>(percentItem),
                                       static_cast<QTableWidgetItem *>(sizeItem),
                                       static_cast<QTableWidgetItem *>(filesItem),
                                       static_cast<QTableWidgetItem *>(deltaItem)}) {
            item->setBackground(tint);
            item->setForeground(textColor);
        }

        m_table->setItem(rowIndex, 0, nameItem);
        m_table->setItem(rowIndex, 1, percentItem);
        m_table->setItem(rowIndex, 2, sizeItem);
        m_table->setItem(rowIndex, 3, filesItem);
        m_table->setItem(rowIndex, 4, deltaItem);
    }

    }
    if (m_viewMetric == ViewMetric::Files) {
        m_table->sortByColumn(3, Qt::DescendingOrder);
    } else if (m_viewMetric == ViewMetric::Size) {
        m_table->sortByColumn(2, Qt::DescendingOrder);
    } else {
        m_table->sortByColumn(1, Qt::DescendingOrder);
    }

    if (children.isEmpty()) {
        m_summaryLabel->setText(QStringLiteral("Heatmap: %1 has no child folders in the current scan.").arg(m_activeFolderPath));
        return;
    }

    m_summaryLabel->setText(QStringLiteral("Heatmap: %1 child folder%2 of %3 | Total: %4%5")
                                .arg(children.size())
                                .arg(children.size() == 1 ? QString() : QStringLiteral("s"))
                                .arg(m_activeFolderPath)
                                .arg(SizeFormatter::formatBytes(childrenSize))
                                .arg(m_compareRows.isEmpty() ? QString() : QStringLiteral(" | delta coloring active")));
}

}
