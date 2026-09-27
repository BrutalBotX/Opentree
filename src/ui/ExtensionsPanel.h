#pragma once

#include <QWidget>

#include "domain/ScanTypes.h"

QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QTableWidget)

namespace opentree {

class ExtensionsPanel : public QWidget {
    Q_OBJECT

public:
    enum class GroupMode {
        Extension,
        Category,
    };

    explicit ExtensionsPanel(QWidget *parent = nullptr);

    void setScanResult(const ScanResultPtr &result);
    void setActiveFolderPath(const QString &path);
    void setViewMetric(ViewMetric metric);

private:
    void rebuild();
    static QString categoryForExtension(const QString &extension);

    QLabel *m_summaryLabel;
    QComboBox *m_modeCombo;
    QTableWidget *m_table;
    ScanResultPtr m_result = std::make_shared<const ScanResult>();
    QString m_activeFolderPath;
    ViewMetric m_viewMetric = ViewMetric::Percentage;
    GroupMode m_groupMode = GroupMode::Extension;
};

}
