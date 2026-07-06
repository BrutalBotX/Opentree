#pragma once

#include <QColor>
#include <QWidget>

#include "domain/ScanTypes.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QLineEdit)
QT_FORWARD_DECLARE_CLASS(QTabWidget)

namespace opentree {

class ChartCanvas;

class ChartPanel : public QWidget {
    Q_OBJECT

public:
    enum class ViewMode {
        Pie,
        Bars,
        Treemap,
    };

    explicit ChartPanel(QWidget *parent = nullptr);

    void setScanResult(const ScanResult &result);
    void setActiveFolderPath(const QString &path);
    void setOtherThresholdPercent(double percent);
    void setViewMetric(ViewMetric metric);
    void setTreemapDepth(int depth);
    void setActiveViewMode(ViewMode mode);

signals:
    void entryActivated(const TreeEntry &entry);
    void entryOpenInGraphRequested(const TreeEntry &entry);
    void entryOpenRequested(const TreeEntry &entry);
    void entryShowInExplorerRequested(const TreeEntry &entry);
    void entryCopyPathRequested(const TreeEntry &entry);
    void pathEntered(const QString &path);

private:
    friend class ChartCanvas;

    struct ChartSlice {
        TreeEntry entry;
        QString label;
        qint64 size = 0;
        bool activatable = false;
        double startAngle = 0.0;
        double spanAngle = 0.0;
        QColor color;
        mutable bool pieLabelVisible = false;
        mutable QPointF pieAnchor;
        mutable QPointF pieElbow;
        mutable QRectF pieLabelRect;
        mutable QRectF barRect;
        mutable QRectF treemapRect;
    };

    struct TreemapNode {
        TreeEntry entry;
        QString label;
        qint64 size = 0;
        QColor color;
        bool activatable = false;
        QRectF rect;
        QVector<TreemapNode> children;
    };
private:
    void paintView(ViewMode mode, QPainter &painter, const QRect &rect) const;
    const ChartSlice *sliceAt(ViewMode mode, const QPoint &point, const QRect &rect) const;
    const TreemapNode *treemapNodeAt(const QPoint &point, const QVector<TreemapNode> &nodes) const;
    void layoutTreemapNodes(QVector<TreemapNode> &nodes, const QRectF &rect, int depth) const;
    void paintTreemapNodes(QPainter &painter, const QVector<TreemapNode> &nodes, int depth) const;
    QVector<TreemapNode> buildTreemapNodes(const QString &rootPath, int remainingDepth, int depth) const;

    void rebuild();
    QString displayNameForPath(const QString &path) const;
    const ChartSlice *hoverSliceAt(ViewMode mode, const QPoint &point, const QRect &rect) const;
    void handleAddressSubmitted();
    void paintPie(QPainter &painter, const QRect &rect) const;
    void paintBars(QPainter &painter, const QRect &rect) const;
    void paintTreemap(QPainter &painter, const QRect &rect) const;

    QLabel *m_summaryLabel;
    QLineEdit *m_addressBar;
    QTabWidget *m_subtabs;
    QWidget *m_pieView;
    QWidget *m_barView;
    QWidget *m_treemapView;
    ScanResult m_result;
    QString m_activeFolderPath;
    QVector<ChartSlice> m_slices;
    mutable QVector<TreemapNode> m_treemapNodes;
    qint64 m_activeFolderSize = 0;
    double m_otherThresholdPercent = 1.0;
    ViewMetric m_viewMetric = ViewMetric::Size;
    int m_treemapDepth = 1;
};

}
