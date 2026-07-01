#pragma once

#include <QWidget>

#include "domain/ScanTypes.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QTableWidget)

namespace opentree {

class ExtensionsPanel : public QWidget {
    Q_OBJECT

public:
    explicit ExtensionsPanel(QWidget *parent = nullptr);

    void setScanResult(const ScanResult &result);
    void setActiveFolderPath(const QString &path);
    void setViewMetric(ViewMetric metric);

private:
    void rebuild();

    QLabel *m_summaryLabel;
    QTableWidget *m_table;
    ScanResult m_result;
    QString m_activeFolderPath;
    ViewMetric m_viewMetric = ViewMetric::Percentage;
};

}
