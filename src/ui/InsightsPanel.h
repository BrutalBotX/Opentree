#pragma once

#include <QWidget>

#include "domain/ScanTypes.h"
#include "services/AnalysisService.h"

QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QSpinBox)
QT_FORWARD_DECLARE_CLASS(QTableWidget)

namespace opentree {

class AnalysisService;

// Timeline/cleanup analytics: disk-full forecast, stale files and junk candidates.
class InsightsPanel : public QWidget {
    Q_OBJECT

public:
    explicit InsightsPanel(AnalysisService *analysisService, QWidget *parent = nullptr);

    void setScanResult(const ScanResultPtr &result);
    void refresh();

private:
    void stageJunk();
    void stageStale();
    int staleDays() const;

    AnalysisService *m_analysisService;
    QLabel *m_forecastLabel;
    QLabel *m_statusLabel;
    QPushButton *m_refreshButton;
    QComboBox *m_stalePresetCombo;
    QSpinBox *m_staleCustomSpin;
    QLabel *m_staleSummaryLabel;
    QPushButton *m_stageStaleButton;
    QTableWidget *m_staleTable;
    QTableWidget *m_junkTable;
    QPushButton *m_stageJunkButton;
    ScanResultPtr m_result = std::make_shared<const ScanResult>();
    QVector<StaleFile> m_staleFiles;
    qint64 m_junkBytes = 0;
};

} // namespace opentree
