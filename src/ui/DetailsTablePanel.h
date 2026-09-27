#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QVector>
#include <QWidget>

#include "domain/ScanTypes.h"

QT_FORWARD_DECLARE_CLASS(QCheckBox)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QTableView)

namespace opentree {

// Sortable table of the items under the active folder (or the whole subtree when the
// panel is switched to flat mode). Percent-of-parent is drawn as an inline bar.
class DetailsTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        NameColumn = 0,
        TypeColumn,
        SizeColumn,
        PercentColumn,
        FilesColumn,
        FoldersColumn,
        ModifiedColumn,
        ColumnCount,
    };

    static constexpr int PercentRole = Qt::UserRole + 41;

    struct Row {
        TreeEntry entry;
        double percent = 0.0;
        QDateTime modified;
    };

    explicit DetailsTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::DescendingOrder) override;

    void setRows(QVector<Row> rows);
    const TreeEntry &entryAt(int row) const;

private:
    QVector<Row> m_rows;
};

class DetailsTablePanel : public QWidget {
    Q_OBJECT

public:
    explicit DetailsTablePanel(QWidget *parent = nullptr);

    void setScanResult(const ScanResultPtr &result);
    void setActiveFolderPath(const QString &path);
    void setViewMetric(ViewMetric metric);
    bool isFlatMode() const;
    void setFlatMode(bool flat);
    bool exportCsv(const QString &filePath, QString *errorMessage = nullptr) const;

signals:
    void entryActivated(const TreeEntry &entry);
    void exportRequested();

private:
    void rebuild();
    void handleRowActivated(const QModelIndex &index);

    QLabel *m_summaryLabel;
    QCheckBox *m_flatCheck;
    QPushButton *m_exportButton;
    QTableView *m_table;
    DetailsTableModel *m_model;
    ScanResultPtr m_result = std::make_shared<const ScanResult>();
    QString m_activeFolderPath;
    ViewMetric m_viewMetric = ViewMetric::Size;
};

}
