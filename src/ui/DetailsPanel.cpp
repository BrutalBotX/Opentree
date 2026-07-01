#include "ui/DetailsPanel.h"

#include <QAction>
#include <QClipboard>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QToolButton>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QVBoxLayout>

#include "utils/FileMetadataUtils.h"
#include "utils/SizeFormatter.h"

namespace opentree {

QLabel *DetailsPanel::createValueLabel(bool multiLine)
{
    auto *label = new QLabel(this);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(multiLine);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    if (multiLine) {
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }
    m_allValueLabels.push_back(label);
    return label;
}

void DetailsPanel::setValue(QLabel *label, const QString &value)
{
    label->setText(value.isEmpty() ? QStringLiteral("-") : value);
}

DetailsPanel::DetailsPanel(QWidget *parent)
    : QWidget(parent)
    , m_kindBadgeLabel(new QLabel(this))
    , m_nameLabel(createValueLabel(true))
    , m_pathLabel(createValueLabel(true))
    , m_sizeLabel(createValueLabel())
    , m_allocatedLabel(createValueLabel())
    , m_percentOfParentLabel(createValueLabel())
    , m_filesLabel(createValueLabel())
    , m_foldersLabel(createValueLabel())
    , m_lastModifiedLabel(createValueLabel())
    , m_lastAccessedLabel(createValueLabel())
    , m_creationDateLabel(createValueLabel())
    , m_ownerLabel(createValueLabel(true))
    , m_attributesLabel(createValueLabel(true))
    , m_compressionRateLabel(createValueLabel())
    , m_permissionsLabel(createValueLabel(true))
    , m_pathLengthLabel(createValueLabel())
    , m_openAction(new QAction("Open", this))
    , m_showInExplorerAction(new QAction("Show in Explorer", this))
    , m_copyPathAction(new QAction("Copy Path", this))
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(12, 12, 12, 12);
    outerLayout->setSpacing(10);

    auto *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(8);

    auto makeToolButton = [this](QAction *action) {
        auto *button = new QToolButton(this);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setAutoRaise(true);
        return button;
    };

    actionRow->addWidget(makeToolButton(m_openAction));
    actionRow->addWidget(makeToolButton(m_showInExplorerAction));
    actionRow->addWidget(makeToolButton(m_copyPathAction));
    actionRow->addStretch();
    outerLayout->addLayout(actionRow);

    m_kindBadgeLabel->setText("No Selection");
    auto *summaryLayout = new QVBoxLayout();
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    summaryLayout->setSpacing(4);
    summaryLayout->addWidget(m_kindBadgeLabel);
    summaryLayout->addWidget(m_nameLabel);
    summaryLayout->addWidget(m_pathLabel);
    outerLayout->addLayout(summaryLayout);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(8);
    form->addRow("Size :", m_sizeLabel);
    form->addRow("Allocated :", m_allocatedLabel);
    form->addRow("% of Parent (Size) :", m_percentOfParentLabel);
    form->addRow("Files :", m_filesLabel);
    form->addRow("Folders :", m_foldersLabel);
    form->addRow("Last Modified :", m_lastModifiedLabel);
    form->addRow("Last Accessed :", m_lastAccessedLabel);
    form->addRow("Creation Date :", m_creationDateLabel);
    form->addRow("Owner :", m_ownerLabel);
    form->addRow("Attributes :", m_attributesLabel);
    form->addRow("Compression Rate :", m_compressionRateLabel);
    form->addRow("Permissions :", m_permissionsLabel);
    form->addRow("Path Length :", m_pathLengthLabel);
    outerLayout->addLayout(form);
    outerLayout->addStretch();

    connect(m_openAction, &QAction::triggered, this, [this]() {
        if (!m_currentEntry.path.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentEntry.path));
        }
    });
    connect(m_showInExplorerAction, &QAction::triggered, this, [this]() {
        if (m_currentEntry.path.isEmpty()) {
            return;
        }
        const QString argument = QFileInfo(m_currentEntry.path).isDir()
            ? m_currentEntry.path
            : QStringLiteral("/select,") + QDir::toNativeSeparators(m_currentEntry.path);
        QProcess::startDetached("explorer.exe", {argument});
    });
    connect(m_copyPathAction, &QAction::triggered, this, [this]() {
        if (!m_currentEntry.path.isEmpty()) {
            QGuiApplication::clipboard()->setText(m_currentEntry.path);
        }
    });

    clear();
}

void DetailsPanel::updateActionState()
{
    const bool hasSelection = !m_currentEntry.path.isEmpty();
    m_openAction->setEnabled(hasSelection);
    m_showInExplorerAction->setEnabled(hasSelection);
    m_copyPathAction->setEnabled(hasSelection);
}

void DetailsPanel::setEntry(const TreeEntry &entry)
{
    m_currentEntry = entry;
    const QFileInfo info(entry.path);
    const FileMetadataDetails metadata = FileMetadataUtils::readMetadata(entry.path);

    setValue(m_kindBadgeLabel, entry.kind == TreeEntryKind::Folder ? QStringLiteral("Folder") : QStringLiteral("File"));
    setValue(m_nameLabel, entry.name);
    setValue(m_pathLabel, entry.path);
    setValue(m_sizeLabel, SizeFormatter::formatBytes(entry.size));
    setValue(m_allocatedLabel, metadata.allocatedBytesKnown ? SizeFormatter::formatBytes(metadata.allocatedBytes) : QStringLiteral("-"));
    setValue(m_percentOfParentLabel, FileMetadataUtils::formatPercentOfParent(entry.size, entry.parentSize));
    setValue(m_filesLabel, entry.kind == TreeEntryKind::Folder ? QString::number(entry.fileCount) : QStringLiteral("-"));
    setValue(m_foldersLabel, entry.kind == TreeEntryKind::Folder ? QString::number(entry.folderCount) : QStringLiteral("-"));
    setValue(m_lastModifiedLabel, FileMetadataUtils::formatTimestamp(info.lastModified()));
    setValue(m_lastAccessedLabel, FileMetadataUtils::formatTimestamp(info.lastRead()));
    setValue(m_creationDateLabel, FileMetadataUtils::formatTimestamp(info.birthTime()));
    setValue(m_ownerLabel, metadata.owner);
    setValue(m_attributesLabel, metadata.attributes);
    setValue(m_compressionRateLabel, FileMetadataUtils::formatCompressionRate(entry.size, metadata.allocatedBytes, metadata.allocatedBytesKnown));
    setValue(m_permissionsLabel, metadata.permissions);
    setValue(m_pathLengthLabel, QString::number(entry.path.size()));
    updateActionState();
}

void DetailsPanel::clear()
{
    m_currentEntry = {};
    setValue(m_kindBadgeLabel, QStringLiteral("No Selection"));
    setValue(m_nameLabel, QStringLiteral("Select a file or folder"));
    setValue(m_pathLabel, QStringLiteral("Choose an item from the tree to inspect its metadata."));
    setValue(m_sizeLabel, QStringLiteral("-"));
    setValue(m_allocatedLabel, QStringLiteral("-"));
    setValue(m_percentOfParentLabel, QStringLiteral("-"));
    setValue(m_filesLabel, QStringLiteral("-"));
    setValue(m_foldersLabel, QStringLiteral("-"));
    setValue(m_lastModifiedLabel, QStringLiteral("-"));
    setValue(m_lastAccessedLabel, QStringLiteral("-"));
    setValue(m_creationDateLabel, QStringLiteral("-"));
    setValue(m_ownerLabel, QStringLiteral("-"));
    setValue(m_attributesLabel, QStringLiteral("-"));
    setValue(m_compressionRateLabel, QStringLiteral("-"));
    setValue(m_permissionsLabel, QStringLiteral("-"));
    setValue(m_pathLengthLabel, QStringLiteral("-"));
    updateActionState();
}

}
