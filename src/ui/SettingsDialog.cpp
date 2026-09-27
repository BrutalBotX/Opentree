#include "ui/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimeEdit>
#include <QUrl>
#include <QVBoxLayout>

#include "integrations/EverythingClient.h"
#include "services/ConfigService.h"
#include "utils/SizeFormatter.h"

namespace opentree {

SettingsDialog::SettingsDialog(ConfigService *configService, const QMap<QString, QString> &themes,
                               QWidget *parent)
    : QDialog(parent)
    , m_configService(configService)
    , m_themes(themes)
    , m_tabs(new QTabWidget(this))
    , m_themeCombo(new QComboBox(this))
    , m_metricCombo(new QComboBox(this))
    , m_othersThresholdSpin(new QDoubleSpinBox(this))
    , m_closeToTrayCheck(new QCheckBox(QStringLiteral("Closing the window keeps OpenTree in the tray"), this))
    , m_useEverythingCheck(new QCheckBox(QStringLiteral("Use the Everything index when available"), this))
    , m_exclusionsEdit(new QLineEdit(this))
    , m_everythingPathEdit(new QLineEdit(this))
    , m_scheduleEnabledCheck(new QCheckBox(QStringLiteral("Create snapshots automatically"), this))
    , m_cadenceCombo(new QComboBox(this))
    , m_scheduleTimeEdit(new QTimeEdit(this))
    , m_thresholdSpin(new QSpinBox(this))
    , m_retentionSpin(new QSpinBox(this))
    , m_whitelistEdit(new QPlainTextEdit(this))
    , m_graphMaxNodesSpin(new QSpinBox(this))
    , m_followTreeCheck(new QCheckBox(QStringLiteral("Graph follows the tree expansion"), this))
    , m_dedupMinSizeSpin(new QSpinBox(this))
    , m_dedupSkipSystemCheck(new QCheckBox(QStringLiteral("Skip system folders (Windows, Program Files, WinSxS, ...)"), this))
{
    setWindowTitle(QStringLiteral("OpenTree Settings"));
    resize(560, 520);

    // ---- General ----
    auto *generalTab = new QWidget(this);
    auto *generalForm = new QFormLayout(generalTab);
    for (auto it = m_themes.cbegin(); it != m_themes.cend(); ++it) {
        m_themeCombo->addItem(it.value(), it.key());
    }
    m_metricCombo->addItem(QStringLiteral("Size"), int(ViewMetric::Size));
    m_metricCombo->addItem(QStringLiteral("Percentage"), int(ViewMetric::Percentage));
    m_metricCombo->addItem(QStringLiteral("File count"), int(ViewMetric::Files));
    m_othersThresholdSpin->setRange(0.0, 100.0);
    m_othersThresholdSpin->setDecimals(1);
    m_othersThresholdSpin->setSuffix(QStringLiteral(" %"));
    generalForm->addRow(QStringLiteral("Theme:"), m_themeCombo);
    generalForm->addRow(QStringLiteral("Default view metric:"), m_metricCombo);
    generalForm->addRow(QStringLiteral("Others cutoff:"), m_othersThresholdSpin);
    generalForm->addRow(QString(), m_closeToTrayCheck);
    m_tabs->addTab(generalTab, QStringLiteral("General"));

    // ---- Scanning ----
    auto *scanTab = new QWidget(this);
    auto *scanForm = new QFormLayout(scanTab);
    m_exclusionsEdit->setToolTip(QStringLiteral("Semicolon separated path fragments the scanner ignores"));
    auto *everythingRow = new QWidget(scanTab);
    auto *everythingLayout = new QHBoxLayout(everythingRow);
    everythingLayout->setContentsMargins(0, 0, 0, 0);
    everythingLayout->addWidget(m_everythingPathEdit, 1);
    auto *browseButton = new QPushButton(QStringLiteral("Browse..."), everythingRow);
    auto *testButton = new QPushButton(QStringLiteral("Test"), everythingRow);
    everythingLayout->addWidget(browseButton, 0);
    everythingLayout->addWidget(testButton, 0);
    scanForm->addRow(QString(), m_useEverythingCheck);
    scanForm->addRow(QStringLiteral("Everything.exe:"), everythingRow);
    scanForm->addRow(QStringLiteral("Exclusions:"), m_exclusionsEdit);
    scanForm->addRow(QString(), new QLabel(QStringLiteral("Filesystem scanning is used automatically when Everything is unavailable."), scanTab));
    m_tabs->addTab(scanTab, QStringLiteral("Scanning"));

    // ---- Snapshots ----
    auto *snapshotTab = new QWidget(this);
    auto *snapshotForm = new QFormLayout(snapshotTab);
    m_cadenceCombo->addItem(QStringLiteral("Daily"), QStringLiteral("daily"));
    m_cadenceCombo->addItem(QStringLiteral("Weekly"), QStringLiteral("weekly"));
    m_cadenceCombo->addItem(QStringLiteral("Monthly"), QStringLiteral("monthly"));
    m_scheduleTimeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    m_thresholdSpin->setRange(1, 100000);
    m_thresholdSpin->setSuffix(QStringLiteral(" MB"));
    m_retentionSpin->setRange(7, 90);
    m_retentionSpin->setSuffix(QStringLiteral(" days"));
    m_whitelistEdit->setPlaceholderText(QStringLiteral("One folder per line"));
    m_whitelistEdit->setMaximumHeight(120);
    snapshotForm->addRow(QString(), m_scheduleEnabledCheck);
    snapshotForm->addRow(QStringLiteral("Cadence:"), m_cadenceCombo);
    snapshotForm->addRow(QStringLiteral("Run at:"), m_scheduleTimeEdit);
    snapshotForm->addRow(QStringLiteral("Change threshold:"), m_thresholdSpin);
    snapshotForm->addRow(QStringLiteral("Retention:"), m_retentionSpin);
    snapshotForm->addRow(QStringLiteral("Tracked folders:"), m_whitelistEdit);
    m_tabs->addTab(snapshotTab, QStringLiteral("Snapshots"));

    // ---- Graph ----
    auto *graphTab = new QWidget(this);
    auto *graphForm = new QFormLayout(graphTab);
    m_graphMaxNodesSpin->setRange(20, 600);
    m_graphMaxNodesSpin->setSuffix(QStringLiteral(" nodes"));
    graphForm->addRow(QStringLiteral("Maximum nodes:"), m_graphMaxNodesSpin);
    graphForm->addRow(QString(), m_followTreeCheck);
    m_tabs->addTab(graphTab, QStringLiteral("Graph"));

    // ---- Deduplication ----
    auto *dedupTab = new QWidget(this);
    auto *dedupForm = new QFormLayout(dedupTab);
    m_dedupMinSizeSpin->setRange(1, 4096);
    m_dedupMinSizeSpin->setSuffix(QStringLiteral(" MB"));
    dedupForm->addRow(QStringLiteral("Minimum file size:"), m_dedupMinSizeSpin);
    dedupForm->addRow(QString(), m_dedupSkipSystemCheck);
    m_tabs->addTab(dedupTab, QStringLiteral("Deduplication"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        saveToConfig();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(browseButton, &QPushButton::clicked, this, &SettingsDialog::browseEverythingExecutable);
    connect(testButton, &QPushButton::clicked, this, &SettingsDialog::testEverythingConnection);

    loadFromConfig();
    m_originalThemeId = m_themeCombo->currentData().toString();
}

void SettingsDialog::loadFromConfig()
{
    if (!m_configService) {
        return;
    }

    const int themeIndex = m_themeCombo->findData(m_configService->themeId());
    if (themeIndex >= 0) {
        m_themeCombo->setCurrentIndex(themeIndex);
    }
    m_metricCombo->setCurrentIndex(m_metricCombo->findData(int(m_configService->viewMetric())));
    m_othersThresholdSpin->setValue(m_configService->othersThresholdPercent());
    m_closeToTrayCheck->setChecked(m_configService->closeToTray());

    m_useEverythingCheck->setChecked(m_configService->useEverything());
    m_everythingPathEdit->setText(m_configService->everythingExecutablePath());
    m_exclusionsEdit->setText(m_configService->excludedPatterns().join(QLatin1Char(';')));

    m_scheduleEnabledCheck->setChecked(m_configService->snapshotScheduleEnabled());
    const QString cadence = m_configService->snapshotScheduleMode() == SnapshotScheduleMode::Weekly
        ? QStringLiteral("weekly")
        : (m_configService->snapshotScheduleMode() == SnapshotScheduleMode::Monthly ? QStringLiteral("monthly") : QStringLiteral("daily"));
    m_cadenceCombo->setCurrentIndex(m_cadenceCombo->findData(cadence));
    m_scheduleTimeEdit->setTime(m_configService->snapshotScheduleTime());
    m_thresholdSpin->setValue(int(std::max<qint64>(1, m_configService->snapshotThresholdBytes() / (1024 * 1024))));
    m_retentionSpin->setValue(m_configService->snapshotRetentionDays());
    m_whitelistEdit->setPlainText(m_configService->snapshotWhitelist().join(QLatin1Char('\n')));

    m_graphMaxNodesSpin->setValue(m_configService->graphMaxNodes());
    m_followTreeCheck->setChecked(m_configService->graphFollowTreeExpansion());

    m_dedupMinSizeSpin->setValue(int(std::max<qint64>(1, m_configService->dedupMinimumBytes() / (1024 * 1024))));
    m_dedupSkipSystemCheck->setChecked(m_configService->dedupSkipSystemFolders());
}

void SettingsDialog::saveToConfig()
{
    if (!m_configService) {
        return;
    }

    m_configService->setThemeId(m_themeCombo->currentData().toString());
    m_configService->setViewMetric(ViewMetric(m_metricCombo->currentData().toInt()));
    m_configService->setOthersThresholdPercent(m_othersThresholdSpin->value());
    m_configService->setCloseToTray(m_closeToTrayCheck->isChecked());

    m_configService->setUseEverything(m_useEverythingCheck->isChecked());
    m_configService->setEverythingExecutablePath(m_everythingPathEdit->text().trimmed());
    m_configService->setExcludedPatterns(m_exclusionsEdit->text().split(QLatin1Char(';'), Qt::SkipEmptyParts));

    m_configService->setSnapshotScheduleEnabled(m_scheduleEnabledCheck->isChecked());
    const QString cadence = m_cadenceCombo->currentData().toString();
    m_configService->setSnapshotScheduleMode(cadence == QStringLiteral("weekly")
                                                 ? SnapshotScheduleMode::Weekly
                                                 : (cadence == QStringLiteral("monthly") ? SnapshotScheduleMode::Monthly : SnapshotScheduleMode::Daily));
    m_configService->setSnapshotScheduleTime(m_scheduleTimeEdit->time());
    m_configService->setSnapshotThresholdBytes(qint64(m_thresholdSpin->value()) * 1024LL * 1024LL);
    m_configService->setSnapshotRetentionDays(m_retentionSpin->value());
    m_configService->setSnapshotWhitelist(m_whitelistEdit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts));

    m_configService->setGraphMaxNodes(m_graphMaxNodesSpin->value());
    m_configService->setGraphFollowTreeExpansion(m_followTreeCheck->isChecked());

    m_configService->setDedupMinimumBytes(qint64(m_dedupMinSizeSpin->value()) * 1024LL * 1024LL);
    m_configService->setDedupSkipSystemFolders(m_dedupSkipSystemCheck->isChecked());
}

void SettingsDialog::selectTab(int index)
{
    m_tabs->setCurrentIndex(index);
}

QString SettingsDialog::selectedThemeId() const
{
    return m_themeCombo->currentData().toString();
}

bool SettingsDialog::themeChanged() const
{
    return selectedThemeId() != m_originalThemeId;
}

void SettingsDialog::browseEverythingExecutable()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("Locate Everything.exe"), m_everythingPathEdit->text(),
        QStringLiteral("Everything executable (Everything.exe);;All files (*.*)"));
    if (!filePath.isEmpty()) {
        m_everythingPathEdit->setText(filePath);
    }
}

void SettingsDialog::testEverythingConnection()
{
    EverythingClient client;
    if (!client.isLibraryLoaded()) {
        QMessageBox::warning(this, QStringLiteral("Everything"),
                             QStringLiteral("The Everything SDK library could not be loaded:\n%1").arg(client.libraryPath()));
        return;
    }

    QString error;
    if (client.testConnection(&error)) {
        QMessageBox::information(this, QStringLiteral("Everything"), QStringLiteral("Everything is available."));
        return;
    }

    // Installed but not running: offer to start it, otherwise offer the download page.
    const QString installed = EverythingClient::detectInstalledExecutable(m_everythingPathEdit->text().trimmed());
    if (!installed.isEmpty()
        && QMessageBox::question(this, QStringLiteral("Everything"),
                                 QStringLiteral("Everything is installed at %1 but is not running.\n\nStart it now?")
                                     .arg(installed),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) {
        QString startError;
        if (EverythingClient::startEverything(installed, &startError)) {
            QMessageBox::information(this, QStringLiteral("Everything"),
                                     QStringLiteral("Everything is starting. Scans will use its index once it is up."));
            return;
        }
        QMessageBox::warning(this, QStringLiteral("Everything"), startError);
        return;
    }

    if (QMessageBox::question(this, QStringLiteral("Everything"),
                              QStringLiteral("Everything is not running, so OpenTree uses its filesystem scan.\n\n"
                                             "Open the Everything download page?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) {
        QDesktopServices::openUrl(QUrl(EverythingClient::downloadUrl()));
    }
}

}
