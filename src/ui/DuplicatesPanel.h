#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "domain/ScanTypes.h"
#include "services/DedupService.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QCheckBox)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QSpinBox)
QT_FORWARD_DECLARE_CLASS(QTreeWidget)
QT_FORWARD_DECLARE_CLASS(QTreeWidgetItem)

namespace opentree {

class ConfigService;

// Finds identical files under the current root and lists them grouped by content.
class DuplicatesPanel : public QWidget {
    Q_OBJECT

public:
    explicit DuplicatesPanel(ConfigService *configService, QWidget *parent = nullptr);

    void setScanResult(const ScanResult &result);

public slots:
    void startScan();

signals:
    void entryActivated(const TreeEntry &entry);

private:
    void finishScan();
    void populate(const DedupResult &result);
    void handleItemActivated(QTreeWidgetItem *item, int column);

    ConfigService *m_configService;
    QLabel *m_summaryLabel;
    QSpinBox *m_minSizeSpin;
    QCheckBox *m_skipSystemCheck;
    QPushButton *m_scanButton;
    QTreeWidget *m_tree;
    QVector<FileEntry> m_files;
    QFutureWatcher<DedupResult> m_watcher;
    QString m_rootPath;
};

}
