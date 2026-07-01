#include "ui/SnapshotSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTimeEdit>
#include <QVBoxLayout>

#include "services/ConfigService.h"

namespace opentree {

SnapshotSettingsDialog::SnapshotSettingsDialog(ConfigService *configService, QWidget *parent)
    : QDialog(parent)
    , m_configService(configService)
    , m_enabledCheckBox(new QCheckBox("Enable background snapshots", this))
    , m_scheduleModeComboBox(new QComboBox(this))
    , m_thresholdSpinBox(new QSpinBox(this))
    , m_retentionSpinBox(new QSpinBox(this))
    , m_timeEdit(new QTimeEdit(this))
    , m_whitelistEdit(new QPlainTextEdit(this))
{
    setWindowTitle("Snapshot Settings");
    resize(520, 420);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_scheduleModeComboBox->addItem("Daily", static_cast<int>(SnapshotScheduleMode::Daily));
    m_scheduleModeComboBox->addItem("Weekly", static_cast<int>(SnapshotScheduleMode::Weekly));
    m_scheduleModeComboBox->addItem("Monthly", static_cast<int>(SnapshotScheduleMode::Monthly));

    m_thresholdSpinBox->setRange(1, 1024 * 100);
    m_thresholdSpinBox->setSuffix(" MB");
    m_thresholdSpinBox->setValue(static_cast<int>(m_configService->snapshotThresholdBytes() / (1024 * 1024)));
    m_retentionSpinBox->setRange(7, 90);
    m_retentionSpinBox->setSuffix(" days");
    m_retentionSpinBox->setValue(m_configService->snapshotRetentionDays());
    m_timeEdit->setDisplayFormat("HH:mm");
    m_timeEdit->setTime(m_configService->snapshotScheduleTime());
    m_enabledCheckBox->setChecked(m_configService->snapshotScheduleEnabled());
    m_scheduleModeComboBox->setCurrentIndex(m_scheduleModeComboBox->findData(static_cast<int>(m_configService->snapshotScheduleMode())));
    m_whitelistEdit->setPlainText(m_configService->snapshotWhitelist().join("\n"));
    m_whitelistEdit->setPlaceholderText("One directory per line\nExample:\nE:/Projects\nE:/Media");

    form->addRow(QString(), m_enabledCheckBox);
    form->addRow("Minimum delta threshold :", m_thresholdSpinBox);
    form->addRow("Retention window :", m_retentionSpinBox);
    form->addRow("Run cadence :", m_scheduleModeComboBox);
    form->addRow("Run at :", m_timeEdit);
    form->addRow("Snapshot whitelist :", m_whitelistEdit);
    layout->addLayout(form);

    auto *noteLabel = new QLabel("Only whitelisted roots are processed by background snapshots.", this);
    noteLabel->setWordWrap(true);
    layout->addWidget(noteLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SnapshotSettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SnapshotSettingsDialog::reject);
    layout->addWidget(buttons);
}

void SnapshotSettingsDialog::accept()
{
    QStringList whitelist;
    for (const QString &line : m_whitelistEdit->toPlainText().split('\n')) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            whitelist.push_back(trimmed);
        }
    }

    m_configService->setSnapshotScheduleEnabled(m_enabledCheckBox->isChecked());
    m_configService->setSnapshotScheduleMode(static_cast<SnapshotScheduleMode>(m_scheduleModeComboBox->currentData().toInt()));
    m_configService->setSnapshotScheduleTime(m_timeEdit->time());
    m_configService->setSnapshotWhitelist(whitelist);
    m_configService->setSnapshotThresholdBytes(static_cast<qint64>(m_thresholdSpinBox->value()) * 1024 * 1024);
    m_configService->setSnapshotRetentionDays(m_retentionSpinBox->value());

    QDialog::accept();
}

}
