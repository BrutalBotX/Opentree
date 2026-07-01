#include "ui/HeatmapPanel.h"

#include <QColor>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

#include "utils/SizeFormatter.h"

namespace opentree {

namespace {

class NumericTableWidgetItem : public QTableWidgetItem {
public:
    explicit NumericTableWidgetItem(double sortValue, const QString &text)
        : QTableWidgetItem(text)
        , m_sortValue(sortValue)
    {
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        const auto *otherItem = dynamic_cast<const NumericTableWidgetItem *>(&other);
        return otherItem ? (m_sortValue < otherItem->m_sortValue) : QTableWidgetItem::operator<(other);
    }

    double m_sortValue = 0.0;
};

}

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
    m_table->setHorizontalHeaderLabels({"Folder", "%", "Size", "Files", "Delta"});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSortingEnabled(true);
    layout->addWidget(m_summaryLabel);
    layout->addWidget(m_table, 1);

    setHeatmapData({}, {});
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
    if (entries.isEmpty()) {
        m_summaryLabel->setText("Heatmap: scan a folder to see the largest folders and deltas.");
        m_table->setRowCount(0);
        return;
    }

    QHash<QString, qint64> deltaByPath;
    for (const SnapshotCompareRow &row : compareRows) {
        deltaByPath.insert(row.path, row.deltaBytes);
    }

    QVector<TreeEntry> folders;
    for (const TreeEntry &entry : entries) {
        if (entry.kind == TreeEntryKind::Folder) {
            folders.push_back(entry);
        }
    }
    std::sort(folders.begin(), folders.end(), [](const TreeEntry &left, const TreeEntry &right) {
        return left.size > right.size;
    });
    if (folders.size() > 30) {
        folders.resize(30);
    }

    const qint64 totalSize = folders.isEmpty() ? 0 : folders.first().size;
    m_table->setSortingEnabled(false);
    m_table->setRowCount(folders.size());
    for (int rowIndex = 0; rowIndex < folders.size(); ++rowIndex) {
        const TreeEntry &entry = folders[rowIndex];
        const qint64 delta = deltaByPath.value(entry.path, 0);
        const double percent = totalSize <= 0 ? 0.0 : (100.0 * double(entry.size) / double(totalSize));

        auto *nameItem = new QTableWidgetItem(entry.path);
        auto *percentItem = new NumericTableWidgetItem(percent, QString::number(percent, 'f', 1) + "%");
        auto *sizeItem = new NumericTableWidgetItem(double(entry.size), SizeFormatter::formatBytes(entry.size));
        auto *filesItem = new NumericTableWidgetItem(double(entry.fileCount), QString::number(entry.fileCount));
        auto *deltaItem = new NumericTableWidgetItem(double(delta), QString::number(delta));

        QColor tint = QColor(70, 90, 130, 70);
        if (delta > 0) {
            tint = QColor(180, 60, 60, 110);
        } else if (delta < 0) {
            tint = QColor(60, 150, 90, 110);
        }
        nameItem->setBackground(tint);

        m_table->setItem(rowIndex, 0, nameItem);
        m_table->setItem(rowIndex, 1, percentItem);
        m_table->setItem(rowIndex, 2, sizeItem);
        m_table->setItem(rowIndex, 3, filesItem);
        m_table->setItem(rowIndex, 4, deltaItem);
    }

    m_table->setSortingEnabled(true);
    if (m_viewMetric == ViewMetric::Files) {
        m_table->sortByColumn(3, Qt::DescendingOrder);
    } else if (m_viewMetric == ViewMetric::Size) {
        m_table->sortByColumn(2, Qt::DescendingOrder);
    } else {
        m_table->sortByColumn(1, Qt::DescendingOrder);
    }

    m_summaryLabel->setText(QStringLiteral("Heatmap: top %1 folders%2")
                                .arg(folders.size())
                                .arg(compareRows.isEmpty() ? QString() : QStringLiteral(" with delta coloring")));
}

}
