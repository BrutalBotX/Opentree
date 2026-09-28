#include "ui/DuplicatesPanel.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "services/ConfigService.h"
#include "ui/EntryActions.h"
#include "utils/SizeFormatter.h"

namespace opentree {

DuplicatesPanel::DuplicatesPanel(ConfigService *configService, QWidget *parent)
    : QWidget(parent)
    , m_configService(configService)
    , m_summaryLabel(new QLabel(this))
    , m_minSizeSpin(new QSpinBox(this))
    , m_skipSystemCheck(new QCheckBox(QStringLiteral("Skip system folders"), this))
    , m_scanButton(new QPushButton(QStringLiteral("Find Duplicates"), this))
    , m_tree(new QTreeWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    m_summaryLabel->setWordWrap(true);

    m_minSizeSpin->setRange(1, 4096);
    m_minSizeSpin->setSuffix(QStringLiteral(" MB"));
    m_minSizeSpin->setMinimumWidth(110);
    m_minSizeSpin->setToolTip(QStringLiteral("Only files at least this large are compared"));
    if (m_configService) {
        const qint64 bytes = m_configService->dedupMinimumBytes();
        m_minSizeSpin->setValue(int(std::max<qint64>(1, bytes / (1024 * 1024))));
    } else {
        m_minSizeSpin->setValue(50);
    }

    // Hashing system locations can trip over locked Windows files, so they are skipped by
    // default. Turning this off is allowed but discouraged.
    m_skipSystemCheck->setChecked(m_configService ? m_configService->dedupSkipSystemFolders() : true);
    m_skipSystemCheck->setToolTip(QStringLiteral(
        "Leave this ON. Hashing Windows, Program Files, ProgramData, WinSxS, packaged app\n"
        "caches, the recycle bin and volume metadata can hit locked system files and is very\n"
        "slow. Only turn it off if you really need to compare those locations."));

    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({QStringLiteral("File"), QStringLiteral("Size"), QStringLiteral("Folder")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_tree->setColumnWidth(1, 110);
    m_tree->setColumnWidth(2, 320);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSortingEnabled(false);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_tree, &QTreeWidget::itemActivated, this, &DuplicatesPanel::handleItemActivated);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        QTreeWidgetItem *item = m_tree->itemAt(position);
        if (!item) {
            return;
        }
        const QString path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty()) {
            return; // Group headers have no path of their own.
        }

        TreeEntry entry;
        entry.kind = TreeEntryKind::File;
        entry.path = path;
        entry.size = item->data(0, Qt::UserRole + 1).toLongLong();
        entry.name = QFileInfo(path).fileName();

        QMenu menu(this);
        QAction *openAction = menu.addAction(QStringLiteral("Open"));
        menu.addSeparator();
        const SharedEntryActions shared = addSharedEntryActions(menu);
        QAction *selected = menu.exec(m_tree->viewport()->mapToGlobal(position));
        if (!selected) {
            return;
        }
        if (selected == openAction) {
            emit entryActivated(entry);
            return;
        }
        runSharedEntryAction(this, selected, entry, shared);
    });

    auto *topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addWidget(m_summaryLabel, 1);
    topRow->addWidget(m_skipSystemCheck, 0);
    topRow->addWidget(m_minSizeSpin, 0);
    topRow->addWidget(m_scanButton, 0);
    layout->addLayout(topRow);
    layout->addWidget(m_tree, 1);

    connect(m_scanButton, &QPushButton::clicked, this, &DuplicatesPanel::startScan);
    connect(&m_watcher, &QFutureWatcher<DedupResult>::finished, this, &DuplicatesPanel::finishScan);

    m_summaryLabel->setText(QStringLiteral("Duplicates: scan a folder, then find identical files above the size limit."));
}

void DuplicatesPanel::setScanResult(const ScanResultPtr &result)
{
    m_result = result ? result : std::make_shared<const ScanResult>();
    m_rootPath = m_result->rootPath;
    m_tree->clear();

    if (m_result->files.isEmpty()) {
        m_summaryLabel->setText(QStringLiteral("Duplicates: scan a folder first."));
        return;
    }

    m_summaryLabel->setText(QStringLiteral("Duplicates: %1 files scanned under %2. Press \"Find duplicates\".")
                                .arg(m_result->files.size())
                                .arg(m_rootPath));
}

void DuplicatesPanel::startScan()
{
    if (m_watcher.isRunning()) {
        return;
    }
    if (m_result->files.isEmpty()) {
        m_summaryLabel->setText(QStringLiteral("Duplicates: scan a folder first."));
        return;
    }

    const qint64 minimumBytes = qint64(m_minSizeSpin->value()) * 1024LL * 1024LL;
    const bool skipSystemPaths = m_skipSystemCheck->isChecked();
    if (m_configService) {
        m_configService->setDedupMinimumBytes(minimumBytes);
        m_configService->setDedupSkipSystemFolders(skipSystemPaths);
    }

    m_scanButton->setEnabled(false);
    m_tree->clear();
    m_summaryLabel->setText(QStringLiteral("Duplicates: comparing %1 files of at least %2%3 ...")
                                .arg(m_result->files.size())
                                .arg(SizeFormatter::formatBytes(minimumBytes))
                                .arg(skipSystemPaths ? QStringLiteral(" (skipping system folders)") : QString()));

    // Capture the shared result, not a copy of the file list: hashing a large scan must not
    // duplicate every path.
    const ScanResultPtr result = m_result;
    DedupService service;
    m_watcher.setFuture(QtConcurrent::run([service, result, minimumBytes, skipSystemPaths]() {
        return service.findDuplicates(result->files, minimumBytes, skipSystemPaths);
    }));
}

void DuplicatesPanel::finishScan()
{
    m_scanButton->setEnabled(true);
    populate(m_watcher.result());
}

void DuplicatesPanel::populate(const DedupResult &result)
{
    m_tree->clear();

    if (result.groups.isEmpty()) {
        m_summaryLabel->setText(QStringLiteral("Duplicates: no duplicates found. %1 size-group candidates, %2 files fully hashed, %3 skipped, %4 system files excluded.")
                                    .arg(result.candidateFiles)
                                    .arg(result.hashedFiles)
                                    .arg(result.skippedFiles)
                                    .arg(result.filteredFiles));
        return;
    }

    for (const DuplicateGroup &group : result.groups) {
        auto *groupItem = new QTreeWidgetItem(m_tree);
        groupItem->setText(0, QStringLiteral("%1 identical files").arg(group.files.size()));
        groupItem->setText(1, SizeFormatter::formatBytes(group.size));
        groupItem->setText(2, QStringLiteral("wasted %1").arg(SizeFormatter::formatBytes(group.wastedBytes())));
        groupItem->setFirstColumnSpanned(false);
        QFont bold = groupItem->font(0);
        bold.setBold(true);
        for (int column = 0; column < 3; ++column) {
            groupItem->setFont(column, bold);
        }

        for (const DuplicateFile &file : group.files) {
            auto *fileItem = new QTreeWidgetItem(groupItem);
            fileItem->setText(0, QFileInfo(file.path).fileName());
            fileItem->setToolTip(0, file.path);
            fileItem->setText(1, SizeFormatter::formatBytes(file.size));
            fileItem->setText(2, QFileInfo(file.path).absolutePath());
            fileItem->setData(0, Qt::UserRole, file.path);
            fileItem->setData(0, Qt::UserRole + 1, file.size);
        }
    }
    m_tree->expandToDepth(0);

    m_summaryLabel->setText(QStringLiteral("Duplicates: %1 groups | %2 reclaimable | %3 candidates, %4 files hashed, %5 system files excluded")
                                .arg(result.groups.size())
                                .arg(SizeFormatter::formatBytes(result.wastedBytes))
                                .arg(result.candidateFiles)
                                .arg(result.hashedFiles)
                                .arg(result.filteredFiles));
}

void DuplicatesPanel::handleItemActivated(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item) {
        return;
    }
    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) {
        return;
    }

    TreeEntry entry;
    entry.kind = TreeEntryKind::File;
    entry.path = path;
    entry.name = QFileInfo(path).fileName();
    entry.size = item->data(0, Qt::UserRole + 1).toLongLong();
    emit entryActivated(entry);
}

}
