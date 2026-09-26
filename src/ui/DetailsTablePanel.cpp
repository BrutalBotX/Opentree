#include "ui/DetailsTablePanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStringConverter>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

#include "utils/PathUtils.h"
#include "utils/SizeFormatter.h"

namespace opentree {

namespace {

bool samePath(const QString &left, const QString &right)
{
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

bool isSameOrDescendant(const QString &path, const QString &rootPath)
{
    return PathUtils::isSameOrDescendant(path, rootPath);
}

int fileCountOf(const DetailsTableModel::Row &row)
{
    return row.entry.kind == TreeEntryKind::File ? 1 : row.entry.fileCount;
}

class PercentBarDelegate : public QStyledItemDelegate {
public:
    explicit PercentBarDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString text = opt.text;
        opt.text.clear();

        // Draw only the background/selection here. QStyledItemDelegate::paint() would
        // re-initialise the option from the model and draw the value a second time (that
        // is what left a digit under the bar next to the right-aligned value).
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const double percent = index.data(DetailsTableModel::PercentRole).toDouble();
        const QRectF cell(opt.rect);
        const QFontMetrics metrics(opt.font);

        // Reserve the right side of the cell for the value so the bar never runs under it.
        const double valueWidth = std::min<double>(cell.width() * 0.45, metrics.horizontalAdvance(text) + 2.0);
        const QRectF textRect(cell.right() - 8.0 - valueWidth, cell.top(), valueWidth, cell.height());
        const double barLeft = cell.left() + 8.0;
        const double barRight = textRect.left() - 10.0;
        const double barWidth = std::max(0.0, barRight - barLeft);

        if (percent > 0.0 && barWidth > 6.0) {
            const double barHeight = std::min(10.0, std::max(4.0, cell.height() - 12.0));
            const QRectF track(barLeft, cell.center().y() - barHeight / 2.0, barWidth, barHeight);
            const double radius = barHeight / 2.0;

            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(55, 62, 78));
            painter->drawRoundedRect(track, radius, radius);
            QRectF fill = track;
            fill.setWidth(std::max(2.0, track.width() * std::clamp(percent / 100.0, 0.0, 1.0)));
            painter->setBrush(QColor(76, 132, 255));
            painter->drawRoundedRect(fill, radius, radius);
            painter->restore();
        }

        if (text.isEmpty()) {
            return;
        }

        painter->save();
        const bool selected = opt.state & QStyle::State_Selected;
        painter->setPen(opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter,
                          metrics.elidedText(text, Qt::ElideRight, int(textRect.width())));
        painter->restore();
    }
};

} // namespace

DetailsTableModel::DetailsTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int DetailsTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int DetailsTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DetailsTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }

    const Row &row = m_rows[index.row()];
    const TreeEntry &entry = row.entry;
    const bool folder = entry.kind == TreeEntryKind::Folder;

    switch (index.column()) {
    case NameColumn:
        if (role == Qt::DisplayRole) return entry.name;
        if (role == Qt::ToolTipRole) return entry.path;
        break;
    case TypeColumn:
        if (role == Qt::DisplayRole) {
            return folder ? QStringLiteral("Folder") : QStringLiteral("File");
        }
        break;
    case SizeColumn:
        if (role == Qt::DisplayRole) return SizeFormatter::formatBytes(entry.size);
        if (role == Qt::UserRole) return QVariant::fromValue(entry.size);
        if (role == Qt::TextAlignmentRole) return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case PercentColumn:
        if (role == Qt::DisplayRole) {
            return QString::number(row.percent, 'f', row.percent < 10.0 ? 1 : 0) + QStringLiteral("%");
        }
        if (role == PercentRole) return row.percent;
        break;
    case FilesColumn:
        if (role == Qt::DisplayRole) return QString::number(fileCountOf(row));
        if (role == Qt::TextAlignmentRole) return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case FoldersColumn:
        if (role == Qt::DisplayRole) return QString::number(folder ? entry.folderCount : 0);
        if (role == Qt::TextAlignmentRole) return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case ModifiedColumn:
        if (role == Qt::DisplayRole) {
            return row.modified.isValid() ? row.modified.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QStringLiteral("-");
        }
        break;
    default:
        break;
    }

    return {};
}

QVariant DetailsTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    if (role == Qt::TextAlignmentRole) {
        switch (section) {
        case SizeColumn:
        case PercentColumn:
        case FilesColumn:
        case FoldersColumn:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    if (role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    switch (section) {
    case NameColumn: return QStringLiteral("Name");
    case TypeColumn: return QStringLiteral("Type");
    case SizeColumn: return QStringLiteral("Size");
    case PercentColumn: return QStringLiteral("% of Parent");
    case FilesColumn: return QStringLiteral("Files");
    case FoldersColumn: return QStringLiteral("Folders");
    case ModifiedColumn: return QStringLiteral("Modified");
    default: return {};
    }
}

void DetailsTableModel::sort(int column, Qt::SortOrder order)
{
    const auto compare = [column](const Row &left, const Row &right) {
        switch (column) {
        case NameColumn:
            return left.entry.name.toLower() < right.entry.name.toLower() ? -1 : 1;
        case TypeColumn:
            return int(left.entry.kind) < int(right.entry.kind) ? -1 : 1;
        case SizeColumn:
            return left.entry.size < right.entry.size ? -1 : 1;
        case PercentColumn:
            return left.percent < right.percent ? -1 : 1;
        case FilesColumn:
            return fileCountOf(left) < fileCountOf(right) ? -1 : 1;
        case FoldersColumn:
            return left.entry.folderCount < right.entry.folderCount ? -1 : 1;
        case ModifiedColumn:
            return left.modified < right.modified ? -1 : 1;
        default:
            return 0;
        }
    };

    std::stable_sort(m_rows.begin(), m_rows.end(), [&compare, order](const Row &left, const Row &right) {
        const int result = compare(left, right);
        return order == Qt::AscendingOrder ? result < 0 : result > 0;
    });

    if (!m_rows.isEmpty()) {
        emit dataChanged(index(0, 0), index(int(m_rows.size()) - 1, ColumnCount - 1));
    }
}

void DetailsTableModel::setRows(QVector<Row> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

const TreeEntry &DetailsTableModel::entryAt(int row) const
{
    static const TreeEntry empty;
    if (row < 0 || row >= m_rows.size()) {
        return empty;
    }
    return m_rows[row].entry;
}

DetailsTablePanel::DetailsTablePanel(QWidget *parent)
    : QWidget(parent)
    , m_summaryLabel(new QLabel(this))
    , m_flatCheck(new QCheckBox(QStringLiteral("Include subfolders"), this))
    , m_exportButton(new QPushButton(QStringLiteral("Export CSV"), this))
    , m_table(new QTableView(this))
    , m_model(new DetailsTableModel(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    m_summaryLabel->setWordWrap(true);

    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setWordWrap(false);
    // Fixed widths: Qt's header size hint ignores stylesheet padding, which clipped the
    // header text once a sort indicator was added.
    m_table->horizontalHeader()->setMinimumSectionSize(56);
    for (int column = DetailsTableModel::TypeColumn; column < DetailsTableModel::ColumnCount; ++column) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    m_table->setColumnWidth(DetailsTableModel::TypeColumn, 80);
    m_table->setColumnWidth(DetailsTableModel::SizeColumn, 110);
    m_table->setColumnWidth(DetailsTableModel::PercentColumn, 130);
    m_table->setColumnWidth(DetailsTableModel::FilesColumn, 80);
    m_table->setColumnWidth(DetailsTableModel::FoldersColumn, 90);
    m_table->setColumnWidth(DetailsTableModel::ModifiedColumn, 150);
    m_table->setItemDelegateForColumn(DetailsTableModel::PercentColumn, new PercentBarDelegate(m_table));

    connect(m_table, &QTableView::clicked, this, &DetailsTablePanel::handleRowActivated);
    connect(m_flatCheck, &QCheckBox::toggled, this, [this](bool) { rebuild(); });
    m_exportButton->setToolTip(QStringLiteral("Save the current table as a CSV file"));
    connect(m_exportButton, &QPushButton::clicked, this, [this]() { emit exportRequested(); });

    auto *topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addWidget(m_summaryLabel, 1);
    topRow->addWidget(m_flatCheck, 0);
    topRow->addWidget(m_exportButton, 0);
    layout->addLayout(topRow);
    layout->addWidget(m_table, 1);

    rebuild();
}

void DetailsTablePanel::setScanResult(const ScanResult &result)
{
    m_result = result;
    if (m_activeFolderPath.isEmpty()) {
        m_activeFolderPath = result.rootPath;
    }
    rebuild();
}

void DetailsTablePanel::setActiveFolderPath(const QString &path)
{
    if (m_activeFolderPath.compare(path, Qt::CaseInsensitive) == 0) {
        return;
    }
    m_activeFolderPath = path;
    rebuild();
}

void DetailsTablePanel::setViewMetric(ViewMetric metric)
{
    if (m_viewMetric == metric) {
        return;
    }
    m_viewMetric = metric;
    rebuild();
}

bool DetailsTablePanel::isFlatMode() const
{
    return m_flatCheck->isChecked();
}

void DetailsTablePanel::setFlatMode(bool flat)
{
    if (m_flatCheck->isChecked() == flat) {
        return;
    }
    m_flatCheck->setChecked(flat);
}

void DetailsTablePanel::rebuild()
{
    if (m_result.treeEntries.isEmpty() || m_activeFolderPath.isEmpty()) {
        m_model->setRows({});
        m_summaryLabel->setText(QStringLiteral("Details: scan a folder to inspect its items."));
        return;
    }

    qint64 referenceSize = 0;
    for (const TreeEntry &entry : m_result.treeEntries) {
        if (entry.kind == TreeEntryKind::Folder && samePath(entry.path, m_activeFolderPath)) {
            referenceSize = entry.size;
            break;
        }
    }

    const bool flat = m_flatCheck->isChecked();
    QVector<DetailsTableModel::Row> rows;
    rows.reserve(256);
    for (const TreeEntry &entry : m_result.treeEntries) {
        const bool include = flat ? isSameOrDescendant(entry.path, m_activeFolderPath) && !samePath(entry.path, m_activeFolderPath)
                                  : samePath(entry.parentPath, m_activeFolderPath);
        if (include) {
            rows.push_back({entry, 0.0, {}});
        }
    }

    const int totalRows = int(rows.size());
    bool truncated = false;
    if (rows.size() > 5000) {
        rows.resize(5000);
        truncated = true;
    }

    for (DetailsTableModel::Row &row : rows) {
        // Prefer the item's real immediate parent; the active folder is only a fallback
        // (the two differ in flat mode, where using the active folder was wrong).
        const qint64 parentSize = row.entry.parentSize > 0 ? row.entry.parentSize : referenceSize;
        row.percent = parentSize <= 0 ? 0.0 : (100.0 * double(row.entry.size) / double(parentSize));
    }

    // Only stat timestamps when the row count stays small enough to keep this snappy.
    const bool statDates = rows.size() <= 2000;
    m_table->setColumnHidden(DetailsTableModel::ModifiedColumn, !statDates);
    if (statDates) {
        for (DetailsTableModel::Row &row : rows) {
            const QFileInfo info(row.entry.path);
            if (info.exists()) {
                row.modified = info.lastModified();
            }
        }
    }

    m_model->setRows(std::move(rows));
    m_table->horizontalHeader()->setSectionResizeMode(DetailsTableModel::NameColumn, QHeaderView::Stretch);

    if (m_viewMetric == ViewMetric::Files) {
        m_table->sortByColumn(DetailsTableModel::FilesColumn, Qt::DescendingOrder);
    } else if (m_viewMetric == ViewMetric::Percentage) {
        m_table->sortByColumn(DetailsTableModel::PercentColumn, Qt::DescendingOrder);
    } else {
        m_table->sortByColumn(DetailsTableModel::SizeColumn, Qt::DescendingOrder);
    }

    QString summary = QStringLiteral("Details: %1 item%2 under %3 | Reference size: %4 | %5")
                          .arg(totalRows)
                          .arg(totalRows == 1 ? QString() : QStringLiteral("s"))
                          .arg(m_activeFolderPath)
                          .arg(SizeFormatter::formatBytes(referenceSize))
                          .arg(flat ? QStringLiteral("whole subtree") : QStringLiteral("direct children"));
    if (truncated) {
        summary += QStringLiteral(" | showing first 5000");
    }
    m_summaryLabel->setText(summary);
}

bool DetailsTablePanel::exportCsv(const QString &filePath, QString *errorMessage) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not open %1 for writing.").arg(filePath);
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    const auto quote = [](const QString &value) {
        QString escaped = value;
        escaped.replace('"', QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(escaped);
    };

    stream << "Name,Type,Path,Size,% of Parent,Files,Folders,Modified\n";
    for (int rowIndex = 0; rowIndex < m_model->rowCount(); ++rowIndex) {
        const QModelIndex nameIndex = m_model->index(rowIndex, DetailsTableModel::NameColumn);
        const QModelIndex typeIndex = m_model->index(rowIndex, DetailsTableModel::TypeColumn);
        const QModelIndex sizeIndex = m_model->index(rowIndex, DetailsTableModel::SizeColumn);
        const QModelIndex percentIndex = m_model->index(rowIndex, DetailsTableModel::PercentColumn);
        const QModelIndex filesIndex = m_model->index(rowIndex, DetailsTableModel::FilesColumn);
        const QModelIndex foldersIndex = m_model->index(rowIndex, DetailsTableModel::FoldersColumn);
        const QModelIndex modifiedIndex = m_model->index(rowIndex, DetailsTableModel::ModifiedColumn);

        stream << quote(nameIndex.data().toString()) << ','
               << quote(typeIndex.data().toString()) << ','
               << quote(m_model->entryAt(rowIndex).path) << ','
               << m_model->data(sizeIndex, Qt::UserRole).toLongLong() << ','
               << QString::number(percentIndex.data().toString().remove('%').toDouble(), 'f', 2) << ','
               << filesIndex.data().toString() << ','
               << foldersIndex.data().toString() << ','
               << quote(modifiedIndex.data().toString()) << '\n';
    }

    stream.flush();
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void DetailsTablePanel::handleRowActivated(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    emit entryActivated(m_model->entryAt(index.row()));
}

}
