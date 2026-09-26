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
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "services/AnalysisService.h"
#include "services/VirtualTrashService.h"
#include "utils/SizeFormatter.h"

namespace opentree {

InsightsPanel::InsightsPanel(AnalysisService *analysisService, VirtualTrashService *trashService,
                             QWidget *parent)
    : QWidget(parent)
    , m_analysisService(analysisService)
    , m_trashService(trashService)
    , m_forecastLabel(new QLabel(this))
    , m_refreshButton(new QPushButton(QStringLiteral("Refresh analysis"), this))
    , m_staleDaysSpin(new QSpinBox(this))
    , m_staleTable(new QTableWidget(this))
    , m_junkTable(new QTableWidget(this))
    , m_stageJunkButton(new QPushButton(QStringLiteral("Stage junk in Trash"), this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    m_forecastLabel->setWordWrap(true);

    // Forecast row.
    auto *forecastRow = new QHBoxLayout;
    forecastRow->setContentsMargins(0, 0, 0, 0);
    forecastRow->addWidget(m_forecastLabel, 1);
    forecastRow->addWidget(m_refreshButton, 0);
    layout->addLayout(forecastRow);

    // Stale files.
    auto *staleBox = new QGroupBox(QStringLiteral("Stale files"), this);
    auto *staleLayout = new QVBoxLayout(staleBox);
    auto *staleControls = new QHBoxLayout;
    staleControls->addWidget(new QLabel(QStringLiteral("Untouched for at least"), staleBox));
    m_staleDaysSpin->setRange(30, 3650);
    m_staleDaysSpin->setValue(365);
    m_staleDaysSpin->setSuffix(QStringLiteral(" days"));
    staleControls->addWidget(m_staleDaysSpin);
    staleControls->addStretch(1);
    staleLayout->addLayout(staleControls);

    m_staleTable->setColumnCount(4);
    m_staleTable->setHorizontalHeaderLabels({QStringLiteral("File"), QStringLiteral("Size"),
                                             QStringLiteral("Last modified"), QStringLiteral("Age")});
    m_staleTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 4; ++column) {
        m_staleTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    m_staleTable->setColumnWidth(1, 110);
    m_staleTable->setColumnWidth(2, 150);
    m_staleTable->setColumnWidth(3, 90);
    m_staleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_staleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    staleLayout->addWidget(m_staleTable);
    layout->addWidget(staleBox, 1);

    // Junk.
    auto *junkBox = new QGroupBox(QStringLiteral("Junk candidates"), this);
    auto *junkLayout = new QVBoxLayout(junkBox);
    auto *junkControls = new QHBoxLayout;
    m_stageJunkButton->setToolTip(QStringLiteral("Adds every junk file to the virtual trash list.\nNothing is deleted: use the Trash tab to review, then move items to the Recycle Bin."));
    junkControls->addStretch(1);
    junkControls->addWidget(m_stageJunkButton);
    junkLayout->addLayout(junkControls);

    m_junkTable->setColumnCount(4);
    m_junkTable->setHorizontalHeaderLabels({QStringLiteral("Category"), QStringLiteral("Size"),
                                            QStringLiteral("Files"), QStringLiteral("Examples")});
    m_junkTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_junkTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_junkTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_junkTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_junkTable->setColumnWidth(0, 160);
    m_junkTable->setColumnWidth(1, 110);
    m_junkTable->setColumnWidth(2, 80);
    m_junkTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_junkTable->setSelectionBehavior(QAbstractItemView::SelectRows);
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
    m_staleTable->setRowCount(stale.size());
    qint64 staleBytes = 0;
    for (int row = 0; row < stale.size(); ++row) {
        const StaleFile &file = stale[row];
        staleBytes += file.size;
        m_staleTable->setItem(row, 0, new QTableWidgetItem(file.path));
        m_staleTable->setItem(row, 1, new QTableWidgetItem(SizeFormatter::formatBytes(file.size)));
        m_staleTable->setItem(row, 2, new QTableWidgetItem(file.lastModified.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        m_staleTable->setItem(row, 3, new QTableWidgetItem(QStringLiteral("%1 d").arg(file.daysOld)));
    }

    // Junk
    const QVector<JunkGroup> junk = m_analysisService->junkFiles(m_result);
    m_junkBytes = 0;
    m_junkTable->setRowCount(junk.size());
    for (int row = 0; row < junk.size(); ++row) {
        const JunkGroup &group = junk[row];
        m_junkBytes += group.size;
        auto *categoryItem = new QTableWidgetItem(group.category);
        categoryItem->setToolTip(group.description);
        m_junkTable->setItem(row, 0, categoryItem);
        m_junkTable->setItem(row, 1, new QTableWidgetItem(SizeFormatter::formatBytes(group.size)));
        m_junkTable->setItem(row, 2, new QTableWidgetItem(QString::number(group.count)));
        m_junkTable->setItem(row, 3, new QTableWidgetItem(group.examples.join(QStringLiteral(", "))));
    }

    if (!stale.isEmpty() || !junk.isEmpty()) {
        m_forecastLabel->setText(m_forecastLabel->text()
                                 + QStringLiteral(" | %1 stale, %2 junk")
                                       .arg(SizeFormatter::formatBytes(staleBytes),
                                            SizeFormatter::formatBytes(m_junkBytes)));
    }
    m_stageJunkButton->setEnabled(m_trashService != nullptr && m_junkBytes > 0);
}

void InsightsPanel::stageJunk()
{
    if (!m_trashService || !m_analysisService || m_result.rootPath.isEmpty()) {
        return;
    }

    const QVector<JunkGroup> junk = m_analysisService->junkFiles(m_result);
    if (junk.isEmpty()) {
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("Stage junk"),
        QStringLiteral("Add every junk file (%1 across %2 categories) to the virtual trash list?\n\n"
                       "Nothing is deleted now. Review the Trash tab, then move items to the Recycle Bin there.")
            .arg(SizeFormatter::formatBytes(m_junkBytes))
            .arg(junk.size()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        return;
    }

    int staged = 0;
    const QVector<JunkGroup> groups = junk;
    for (const JunkGroup &group : groups) {
        Q_UNUSED(group);
    }

    // Stage each junk file individually so the trash list stays reviewable.
    for (const FileEntry &file : m_result.files) {
        const QString path = file.path.toLower();
        const QString name = file.name.toLower();
        const QString suffix = QFileInfo(name).suffix();
        bool isJunk = suffix == QStringLiteral("tmp") || suffix == QStringLiteral("temp")
            || suffix == QStringLiteral("log") || suffix == QStringLiteral("dmp")
            || suffix == QStringLiteral("bak") || suffix == QStringLiteral("mdmp")
            || name == QStringLiteral("thumbs.db") || name == QStringLiteral("desktop.ini")
            || path.contains(QStringLiteral("/temp/")) || path.contains(QStringLiteral("/cache/"));
        if (!isJunk) {
            continue;
        }
        if (m_trashService->stage(file.path, file.size, false, m_result.rootPath,
                                  QStringLiteral("Junk candidate"), nullptr)) {
            ++staged;
        }
    }

    QMessageBox::information(this, QStringLiteral("Stage junk"),
                             QStringLiteral("Staged %1 item(s). Open the Trash tab to review them.").arg(staged));
}

}
