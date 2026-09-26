#pragma once

#include <QWidget>

#include "domain/ScanTypes.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QSpinBox)
QT_FORWARD_DECLARE_CLASS(QTableWidget)

namespace opentree {

class AnalysisService;
class VirtualTrashService;

// Timeline/cleanup analytics: disk-full forecast, stale files and junk groups.
class InsightsPanel : public QWidget {
    Q_OBJECT

public:
    explicit InsightsPanel(AnalysisService *analysisService, VirtualTrashService *trashService,
                           QWidget *parent = nullptr);

    void setScanResult(const ScanResult &result);
    void refresh();

private:
    void stageJunk();

    AnalysisService *m_analysisService;
    VirtualTrashService *m_trashService;
    QLabel *m_forecastLabel;
    QPushButton *m_refreshButton;
    QSpinBox *m_staleDaysSpin;
    QTableWidget *m_staleTable;
    QTableWidget *m_junkTable;
    QPushButton *m_stageJunkButton;
    ScanResult m_result;
    qint64 m_junkBytes = 0;
};

}
