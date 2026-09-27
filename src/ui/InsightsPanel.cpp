#include "ui/InsightsPanel.h"

#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "services/AnalysisService.h"
#include "ui/EntryActions.h"
#include "ui/StagingReviewDialog.h"
#include "ui/TableItems.h"
#include "utils/SizeFormatter.h"

namespace opentree {

InsightsPanel::InsightsPanel(AnalysisService *analysisService, QWidget *parent)
    : QWidget(parent)
    , m_analysisService(analysisService)
    , m_forecastLabel(new QLabel(this))
    , m_statusLabel(new QLabel(this))
    , m_refreshButton(new QPushButton(QStringLiteral("Refresh Analysis"), this))
    , m_staleDaysSpin(new QSpinBox(this))
    , m_staleTable(new QTableWidget(this))
    , m_junkTable(new QTableWidget(this))
    , m_stageJunkButton(new QPushButton(QStringLiteral("Stage Junk in Trash..."), this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    m_forecastLabel->setWordWrap(true);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setVisible(false);

    // Forecast row.
    auto *forecastRow = new QHBoxLayout;
    forecastRow->setContentsMargins(0, 0, 0, 0);
    forecastRow->addWidget(m_forecastLabel, 1);
    forecastRow->addWidget(m_refreshButton, 0);
    layout->addLayout(forecastRow);
    layout->addWidget(m_statusLabel);

    // Stale files.
    auto *staleBox = new QGroupBox(QStringLiteral("Stale Files"), this);
    auto *staleLayout = new QVBoxLayout(staleBox);
    auto *staleControls = new QHBoxLayout;
    staleControls->setContentsMargins(0, 0, 0, 0);
    staleControls->addWidget(new QLabel(QStringLiteral("Untouched for at least"), staleBox));
    m_staleDaysSpin->setRange(30, 3650);
    m_staleDaysSpin->setValue(365);
    m_staleDaysSpin->setSuffix(QStringLiteral(" days"));
    staleControls->addWidget(m_staleDaysSpin);
    staleControls->addStretch(1);
    staleLayout->addLayout(staleControls);

    m_staleTable->setColumnCount(4);
    m_staleTable->setHorizontalHeaderLabels({QStringLiteral("File"), QStringLiteral("Size"),
                                             QStringLiteral("Last Modified"), QStringLiteral("Age")});
    configureStandardTable(m_staleTable);
    m_staleTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 4; ++column) {
        m_staleTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    m_staleTable->setColumnWidth(1, 120);
    m_staleTable->setColumnWidth(2, 160);
    m_staleTable->setColumnWidth(3, 90);
    m_staleTable->sortItems(1, Qt::DescendingOrder); // largest first by default
    staleLayout->addWidget(m_staleTable);
    layout->addWidget(staleBox, 1);

    // Junk.
    auto *junkBox = new QGroupBox(QStringLiteral("Junk Candidates"), this);
    auto *junkLayout = new QVBoxLayout(junkBox);
    auto *junkControls = new QHBoxLayout;
    junkControls->setContentsMargins(0, 0, 0, 0);
    m_stageJunkButton->setToolTip(QStringLiteral("Review the junk files, then add them to the virtual trash list.\n"
                                                 "Nothing is deleted there: the Trash tab moves items to the Recycle Bin."));
    junkControls->addStretch(1);
    junkControls->addWidget(m_stageJunkButton);
    junkLayout->addLayout(junkControls);

    m_junkTable->setColumnCount(4);
    m_junkTable->setHorizontalHeaderLabels({QStringLiteral("Category"), QStringLiteral("Size"),
                                            QStringLiteral("Files"), QStringLiteral("Examples")});
    configureStandardTable(m_junkTable);
    m_junkTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_junkTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_junkTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_junkTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_junkTable->setColumnWidth(0, 160);
    m_junkTable->setColumnWidth(1, 120);
    m_junkTable->setColumnWidth(2, 80);
    m_junkTable->sortItems(1, Qt::DescendingOrder); // largest category first by default
    junkLayout->addWidget(m_junkTable);
    layout->addWidget(junkBox, 1);

    connect(m_refreshButton, &QPushButton::clicked, this, &InsightsPanel::refresh);
    connect(m_staleDaysSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { refresh(); });
    connect(m_stageJunkButton, &QPushButton::clicked, this, &InsightsPanel::stageJunk);

    refresh();
}

void InsightsPanel::setScanResult(const ScanResult &result)
{
    m_result = result;
    m_statusLabel->setVisible(false);
    refresh();
}

void InsightsPanel::refresh()
{
    if (!m_analysisService) {
        m_forecastLabel->setText(QStringLiteral("Insights: database unavailable."));
        m_staleTable->setRowCount(0);
        m_junkTable->setRowCount(0);
        return;
    }

    if (m_result.rootPath.isEmpty()) {
        m_forecastLabel->setText(QStringLiteral("Insights: scan a folder to forecast disk usage and find stale or junk files."));
        m_staleTable->setRowCount(0);
        m_junkTable->setRowCount(0);
        return;
    }

    // Forecast
    QString error;
    const DiskForecast forecast = m_analysisService->forecastForRoot(m_result.rootPath, &error);
    if (!forecast.available) {
        m_forecastLabel->setText(QStringLiteral("Disk forecast: %1").arg(forecast.basis));
    } else {
        const QString trend = forecast.growthBytesPerDay > 0.0
            ? QStringLiteral("growing %1/day").arg(SizeFormatter::formatBytes(qint64(forecast.growthBytesPerDay)))
            : QStringLiteral("shrinking or flat");
        const QString eta = forecast.daysUntilFull > 0
            ? QStringLiteral("&#8776; %1 days until the volume is full").arg(forecast.daysUntilFull)
            : QStringLiteral("no full-disk date projected");
        m_forecastLabel->setText(QStringLiteral("Disk forecast: %1 free of %2 | %3 | %4 | based on %5")
                                     .arg(SizeFormatter::formatBytes(forecast.volumeFree),
                                          SizeFormatter::formatBytes(forecast.volumeTotal),
                                          trend,
                                          eta,
                                          forecast.basis));
    }

    // Stale files
    const QVector<StaleFile> stale = m_analysisService->staleFiles(m_result, m_staleDaysSpin->value(), 200, &error);
    qint64 staleBytes = 0;
    {
        TableSortGuard guard(m_staleTable);
        m_staleTable->setRowCount(stale.size());
        for (int row = 0; row < stale.size(); ++row) {
            const StaleFile &file = stale[row];
            staleBytes += file.size;
            m_staleTable->setItem(row, 0, makeTextItem(file.path, file.path));
            m_staleTable->setItem(row, 1, makeNumberItem(SizeFormatter::formatBytes(file.size), file.size));
            m_staleTable->setItem(row, 2, makeTextItem(file.lastModified.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
            m_staleTable->setItem(row, 3, makeNumberItem(QStringLiteral("%1 days").arg(file.daysOld), file.daysOld));
        }
    }

    // Junk
    const QVector<JunkGroup> junk = m_analysisService->junkFiles(m_result);
    m_junkBytes = 0;
    {
        TableSortGuard guard(m_junkTable);
        m_junkTable->setRowCount(junk.size());
        for (int row = 0; row < junk.size(); ++row) {
            const JunkGroup &group = junk[row];
            m_junkBytes += group.size;
            m_junkTable->setItem(row, 0, makeTextItem(group.category, group.description));
            m_junkTable->setItem(row, 1, makeNumberItem(SizeFormatter::formatBytes(group.size), group.size));
            m_junkTable->setItem(row, 2, makeNumberItem(QString::number(group.count), group.count));
            m_junkTable->setItem(row, 3, makeTextItem(group.examples.join(QStringLiteral(", "))));
        }
    }

    if (!stale.isEmpty() || !junk.isEmpty()) {
        m_forecastLabel->setText(m_forecastLabel->text()
                                 + QStringLiteral(" | %1 stale, %2 junk")
                                       .arg(SizeFormatter::formatBytes(staleBytes),
                                            SizeFormatter::formatBytes(m_junkBytes)));
    }
    m_stageJunkButton->setEnabled(EntryActionHub::instance()->isAvailable() && m_junkBytes > 0);
}

void InsightsPanel::stageJunk()
{
    if (!m_analysisService || m_result.rootPath.isEmpty()) {
        return;
    }

    const QVector<JunkFile> junkFiles = m_analysisService->junkFileList(m_result);
    if (junkFiles.isEmpty()) {
        return;
    }

    QVector<StageRequest> candidates;
    candidates.reserve(junkFiles.size());
    for (const JunkFile &file : junkFiles) {
        StageRequest candidate;
        candidate.path = file.path;
        candidate.size = file.size;
        candidate.isFolder = false;
        candidates.push_back(candidate);
    }

    StagingReviewDialog dialog(StagingReviewDialog::Action::Stage, candidates, {}, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    int staged = 0;
    qint64 stagedBytes = 0;
    QString error;
    EntryActionHub::instance()->stage(candidates, m_result.rootPath, QStringLiteral("Junk candidate"),
                                      &staged, &stagedBytes, &error);
    if (staged == 0) {
        QMessageBox::warning(this, QStringLiteral("Stage junk"),
                             error.isEmpty() ? QStringLiteral("Nothing could be staged.") : error);
        return;
    }

    m_statusLabel->setText(QStringLiteral("Staged %1 junk %2 (%3). Review them in the Trash tab "
                                          "and move them to the Recycle Bin there.")
                               .arg(staged)
                               .arg(staged == 1 ? QStringLiteral("item") : QStringLiteral("items"))
                               .arg(SizeFormatter::formatBytes(stagedBytes)));
    m_statusLabel->setVisible(true);
}

} // namespace opentree
