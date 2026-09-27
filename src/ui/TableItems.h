#pragma once

#include <QAbstractItemView>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

namespace opentree {

// QTableWidgetItem that sorts by a numeric value instead of the formatted display text, so
// columns like "1.22 MB" or "400 d" order by size/age rather than alphabetically.
class NumericTableWidgetItem : public QTableWidgetItem {
public:
    NumericTableWidgetItem(qint64 value, const QString &text)
        : QTableWidgetItem(text)
        , m_value(value)
    {
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        if (const auto *numeric = dynamic_cast<const NumericTableWidgetItem *>(&other)) {
            return m_value < numeric->m_value;
        }
        return QTableWidgetItem::operator<(other);
    }

    qint64 value() const { return m_value; }

private:
    qint64 m_value = 0;
};

// Right-aligned numeric cell (used for size/count/age columns).
inline QTableWidgetItem *makeNumberItem(const QString &text, qint64 sortValue)
{
    auto *item = new NumericTableWidgetItem(sortValue, text);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

// Sorting item for fractional values (percentages, ratios).
class DoubleTableWidgetItem : public QTableWidgetItem {
public:
    DoubleTableWidgetItem(double value, const QString &text)
        : QTableWidgetItem(text)
        , m_value(value)
    {
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        if (const auto *numeric = dynamic_cast<const DoubleTableWidgetItem *>(&other)) {
            return m_value < numeric->m_value;
        }
        if (const auto *integer = dynamic_cast<const NumericTableWidgetItem *>(&other)) {
            return m_value < double(integer->value());
        }
        return QTableWidgetItem::operator<(other);
    }

    double doubleValue() const { return m_value; }

private:
    double m_value = 0.0;
};

// Right-aligned percentage cell ("42.7%").
inline QTableWidgetItem *makePercentItem(double percent)
{
    auto *item = new DoubleTableWidgetItem(percent, QStringLiteral("%1%").arg(QString::number(percent, 'f', 1)));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

// Text cell with the standard vertical alignment.
inline QTableWidgetItem *makeTextItem(const QString &text, const QString &toolTip = QString())
{
    auto *item = new QTableWidgetItem(text);
    if (!toolTip.isEmpty()) {
        item->setToolTip(toolTip);
    }
    return item;
}

// Applies the app-wide table conventions: alternating rows, no row-number gutter, read
// only, whole-row selection, single selection, no wrapping and sortable columns.
inline void configureStandardTable(QTableWidget *table)
{
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setWordWrap(false);
    table->setShowGrid(true);
    table->setSortingEnabled(true);
}

// Same conventions but multi-row selection (used where a selection is acted on as a batch).
inline void configureStandardTableMultiSelect(QTableWidget *table)
{
    configureStandardTable(table);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
}

// RAII helper for repopulating a sortable table: sorting is paused while rows are filled in
// (otherwise every insert re-sorts and items jump around), then the user's chosen sort
// column/order is restored.
class TableSortGuard {
public:
    explicit TableSortGuard(QTableWidget *table)
        : m_table(table)
        , m_column(table->horizontalHeader()->sortIndicatorSection())
        , m_order(table->horizontalHeader()->sortIndicatorOrder())
    {
        m_table->setSortingEnabled(false);
    }

    ~TableSortGuard()
    {
        m_table->setSortingEnabled(true);
        if (m_column >= 0) {
            m_table->sortItems(m_column, m_order);
        }
    }

    TableSortGuard(const TableSortGuard &) = delete;
    TableSortGuard &operator=(const TableSortGuard &) = delete;

private:
    QTableWidget *m_table;
    int m_column;
    Qt::SortOrder m_order;
};

} // namespace opentree
