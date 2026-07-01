#pragma once

#include <QDialog>

QT_FORWARD_DECLARE_CLASS(QCheckBox)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QPlainTextEdit)
QT_FORWARD_DECLARE_CLASS(QSpinBox)
QT_FORWARD_DECLARE_CLASS(QTimeEdit)

namespace opentree {

class ConfigService;

class SnapshotSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SnapshotSettingsDialog(ConfigService *configService, QWidget *parent = nullptr);

private slots:
    void accept() override;

private:
    ConfigService *m_configService;
    QCheckBox *m_enabledCheckBox;
    QComboBox *m_scheduleModeComboBox;
    QSpinBox *m_thresholdSpinBox;
    QSpinBox *m_retentionSpinBox;
    QTimeEdit *m_timeEdit;
    QPlainTextEdit *m_whitelistEdit;
};

}
