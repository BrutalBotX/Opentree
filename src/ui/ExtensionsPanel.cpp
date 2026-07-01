#include "ui/ExtensionsPanel.h"

#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

#include "utils/SizeFormatter.h"

namespace opentree {

namespace {

struct ExtensionRow {
    QString extension;
    qint64 size = 0;
    int count = 0;
};

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

QString normalizedExtension(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().trimmed().toLower();
    return suffix.isEmpty() ? QStringLiteral("[no extension]") : QStringLiteral(".") + suffix;
}

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

ExtensionsPanel::ExtensionsPanel(QWidget *parent)
    : QWidget(parent)
    , m_summaryLabel(new QLabel(this))
    , m_table(new QTableWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    m_summaryLabel->setWordWrap(true);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({"Extension", "Size", "Files", "%"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSortingEnabled(true);

    layout->addWidget(m_summaryLabel);
    layout->addWidget(m_table, 1);

    rebuild();
}

void ExtensionsPanel::setScanResult(const ScanResult &result)
{
    m_result = result;
    if (m_activeFolderPath.isEmpty()) {
        m_activeFolderPath = result.rootPath;
    }
    rebuild();
}

void ExtensionsPanel::setActiveFolderPath(const QString &path)
{
    if (m_activeFolderPath.compare(path, Qt::CaseInsensitive) == 0) {
        return;
    }

    m_activeFolderPath = path;
    rebuild();
}

void ExtensionsPanel::setViewMetric(ViewMetric metric)
{
    m_viewMetric = metric;
    rebuild();
}

void ExtensionsPanel::rebuild()
{
    m_table->setSortingEnabled(false);
    m_table->clearContents();

    if (m_result.files.isEmpty() || m_activeFolderPath.isEmpty()) {
        m_summaryLabel->setText("Extensions: scan a folder to group files by extension.");
        m_table->setRowCount(0);
        return;
    }

    QHash<QString, ExtensionRow> byExtension;
    qint64 totalSize = 0;
    int totalFiles = 0;
    for (const FileEntry &file : m_result.files) {
        if (!isSameOrDescendant(file.path, m_activeFolderPath)) {
            continue;
        }

        const QString extension = normalizedExtension(file.path);
        ExtensionRow row = byExtension.value(extension);
        row.extension = extension;
        row.size += file.size;
        row.count += 1;
        byExtension.insert(extension, row);
        totalSize += file.size;
        totalFiles += 1;
    }

    QVector<ExtensionRow> rows = byExtension.values().toVector();
    std::sort(rows.begin(), rows.end(), [](const ExtensionRow &left, const ExtensionRow &right) {
        return left.size > right.size;
    });
    if (rows.size() > 100) {
        rows.resize(100);
    }

    m_table->setRowCount(rows.size());
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const ExtensionRow &row = rows[rowIndex];
        const double percent = totalSize <= 0 ? 0.0 : (100.0 * double(row.size) / double(totalSize));
        m_table->setItem(rowIndex, 0, new QTableWidgetItem(row.extension));
        m_table->setItem(rowIndex, 1, new NumericTableWidgetItem(double(row.size), SizeFormatter::formatBytes(row.size)));
        m_table->setItem(rowIndex, 2, new NumericTableWidgetItem(double(row.count), QString::number(row.count)));
        m_table->setItem(rowIndex, 3, new NumericTableWidgetItem(percent, QString::number(percent, 'f', 1) + "%"));
    }

    m_table->setSortingEnabled(true);
    if (m_viewMetric == ViewMetric::Files) {
        m_table->sortByColumn(2, Qt::DescendingOrder);
    } else if (m_viewMetric == ViewMetric::Size) {
        m_table->sortByColumn(1, Qt::DescendingOrder);
    } else {
        m_table->sortByColumn(3, Qt::DescendingOrder);
    }
    m_summaryLabel->setText(QStringLiteral("Extensions: %1 files under %2 | Total: %3 | Groups: %4")
                                .arg(totalFiles)
                                .arg(m_activeFolderPath)
                                .arg(SizeFormatter::formatBytes(totalSize))
                                .arg(rows.size()));
}

}
