#pragma once

#include <QDialog>
#include <QMap>

QT_FORWARD_DECLARE_CLASS(QCheckBox)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QDoubleSpinBox)
QT_FORWARD_DECLARE_CLASS(QLineEdit)
QT_FORWARD_DECLARE_CLASS(QPlainTextEdit)
QT_FORWARD_DECLARE_CLASS(QSpinBox)
QT_FORWARD_DECLARE_CLASS(QTabWidget)
QT_FORWARD_DECLARE_CLASS(QTimeEdit)

namespace opentree {

class ConfigService;

// Unified application settings. Consolidates the scanning, snapshot, graph, theme and
// deduplication options that previously lived in separate dialogs or the menu.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    enum Tab {
        GeneralTab = 0,
        ScanningTab,
        SnapshotsTab,
        GraphTab,
        DeduplicationTab,
    };

    SettingsDialog(ConfigService *configService, const QMap<QString, QString> &themes,
                   QWidget *parent = nullptr);

    void selectTab(int index);
    QString selectedThemeId() const;
    bool themeChanged() const;

private:
    void loadFromConfig();
    void saveToConfig();
    void browseEverythingExecutable();
    void testEverythingConnection();

    ConfigService *m_configService;
    QMap<QString, QString> m_themes;
    QString m_originalThemeId;

    QTabWidget *m_tabs;

    // General
    QComboBox *m_themeCombo;
    QComboBox *m_metricCombo;
    QDoubleSpinBox *m_othersThresholdSpin;
    QCheckBox *m_closeToTrayCheck;

    // Scanning
    QCheckBox *m_useEverythingCheck;
    QLineEdit *m_exclusionsEdit;
    QLineEdit *m_everythingPathEdit;

    // Snapshots
    QCheckBox *m_scheduleEnabledCheck;
    QComboBox *m_cadenceCombo;
    QTimeEdit *m_scheduleTimeEdit;
    QSpinBox *m_thresholdSpin;
    QSpinBox *m_retentionSpin;
    QPlainTextEdit *m_whitelistEdit;

    // Graph
    QSpinBox *m_graphMaxNodesSpin;
    QCheckBox *m_followTreeCheck;

    // Deduplication
    QSpinBox *m_dedupMinSizeSpin;
    QCheckBox *m_dedupSkipSystemCheck;
};

}
