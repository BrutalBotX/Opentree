#include "ui/ExtensionsPanel.h"

#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

#include "ui/TableItems.h"
#include "utils/PathUtils.h"
#include "utils/SizeFormatter.h"

namespace opentree {

namespace {

struct ExtensionRow {
    QString group;
    qint64 size = 0;
    int count = 0;
};

bool isSameOrDescendant(const QString &path, const QString &rootPath)
{
    return PathUtils::isSameOrDescendant(path, rootPath);
}

QString normalizedExtension(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().trimmed().toLower();
    return suffix.isEmpty() ? QStringLiteral("[no extension]") : QStringLiteral(".") + suffix;
}

const QStringList &videoExtensions()
{
    static const QStringList list = {"mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "mpg", "mpeg", "ts", "m2ts", "vob", "3gp"};
    return list;
}
const QStringList &audioExtensions()
{
    static const QStringList list = {"mp3", "flac", "wav", "aac", "ogg", "m4a", "wma", "opus", "aiff", "mid"};
    return list;
}
const QStringList &imageExtensions()
{
    static const QStringList list = {"jpg", "jpeg", "png", "gif", "bmp", "webp", "svg", "tiff", "tif", "heic", "raw", "ico", "psd"};
    return list;
}
const QStringList &archiveExtensions()
{
    static const QStringList list = {"zip", "rar", "7z", "tar", "gz", "bz2", "xz", "zst", "cab", "lz", "lzma"};
    return list;
}
const QStringList &documentExtensions()
{
    static const QStringList list = {"pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "txt", "md", "rtf", "odt", "ods", "epub", "csv"};
    return list;
}
const QStringList &codeExtensions()
{
    static const QStringList list = {"c", "cpp", "h", "hpp", "cs", "java", "py", "js", "mjs", "ts", "jsx", "tsx", "go", "rs", "rb", "php", "swift", "kt", "lua", "sql", "sh", "bat", "ps1", "json", "xml", "yml", "yaml", "html", "css", "scss"};
    return list;
}
const QStringList &executableExtensions()
{
    static const QStringList list = {"exe", "msi", "dll", "sys", "appimage", "apk", "deb", "rpm", "apk", "so", "dylib"};
    return list;
}
const QStringList &diskImageExtensions()
{
    static const QStringList list = {"iso", "img", "vhd", "vhdx", "vmdk", "wim", "qcow2"};
    return list;
}
const QStringList &fontExtensions()
{
    static const QStringList list = {"ttf", "otf", "woff", "woff2", "eot"};
    return list;
}

} // namespace

QString ExtensionsPanel::categoryForExtension(const QString &extension)
{
    if (extension == QStringLiteral("[no extension]")) {
        return QStringLiteral("[no extension]");
    }

    QString suffix = extension;
    if (suffix.startsWith('.')) {
        suffix = suffix.mid(1);
    }
    suffix = suffix.toLower();

    if (videoExtensions().contains(suffix)) return QStringLiteral("Video");
    if (audioExtensions().contains(suffix)) return QStringLiteral("Audio");
    if (imageExtensions().contains(suffix)) return QStringLiteral("Image");
    if (documentExtensions().contains(suffix)) return QStringLiteral("Document");
    if (archiveExtensions().contains(suffix)) return QStringLiteral("Archive");
    if (diskImageExtensions().contains(suffix)) return QStringLiteral("Disk image");
    if (codeExtensions().contains(suffix)) return QStringLiteral("Code / Text");
    if (executableExtensions().contains(suffix)) return QStringLiteral("Program");
    if (fontExtensions().contains(suffix)) return QStringLiteral("Font");
    return QStringLiteral("Other");
}

ExtensionsPanel::ExtensionsPanel(QWidget *parent)
    : QWidget(parent)
    , m_summaryLabel(new QLabel(this))
    , m_modeCombo(new QComboBox(this))
    , m_table(new QTableWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    m_summaryLabel->setWordWrap(true);
    m_modeCombo->addItem(QStringLiteral("By extension"), int(GroupMode::Extension));
    m_modeCombo->addItem(QStringLiteral("By category"), int(GroupMode::Category));
    m_modeCombo->setToolTip(QStringLiteral("Group files by raw extension or by a broader file family"));
    m_modeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_modeCombo->setMinimumWidth(170);

    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({"Group", "Size", "Files", "%"});
    // Fixed widths: Qt's header size hint ignores stylesheet padding, which clipped the
    // header text once a sort indicator was added.
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_table->setColumnWidth(1, 120);
    m_table->setColumnWidth(2, 80);
    m_table->setColumnWidth(3, 90);
    configureStandardTable(m_table);

    auto *topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addWidget(m_summaryLabel, 1);
    topRow->addWidget(m_modeCombo, 0);
    layout->addLayout(topRow);
    layout->addWidget(m_table, 1);

    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        m_groupMode = static_cast<GroupMode>(m_modeCombo->currentData().toInt());
        rebuild();
    });

    rebuild();
}

void ExtensionsPanel::setScanResult(const ScanResult &result)
{
    m_result = result;
    if (m_activeFolderPath.isEmpty()) {
        m_activeFolderPath = result.rootPath;
    }
    rebuild();
}

void ExtensionsPanel::setActiveFolderPath(const QString &path)
{
    if (m_activeFolderPath.compare(path, Qt::CaseInsensitive) == 0) {
        return;
    }

    m_activeFolderPath = path;
    rebuild();
}

void ExtensionsPanel::setViewMetric(ViewMetric metric)
{
    m_viewMetric = metric;
    rebuild();
}

void ExtensionsPanel::rebuild()
{
    m_table->clearContents();

    if (m_activeFolderPath.isEmpty()) {
        m_summaryLabel->setText("Extensions: scan a folder to group files by extension.");
        m_table->setRowCount(0);
        return;
    }

    // Prefer the file list, but fall back to file entries in the tree: a cached root only
    // carries folder summaries, and a partial result should still show something.
    QVector<QPair<QString, qint64>> files;
    files.reserve(m_result.files.size());
    for (const FileEntry &file : m_result.files) {
        files.push_back({file.path, file.size});
    }
    if (files.isEmpty()) {
        for (const TreeEntry &entry : m_result.treeEntries) {
            if (entry.kind == TreeEntryKind::File) {
                files.push_back({entry.path, entry.size});
            }
        }
    }

    QHash<QString, ExtensionRow> byGroup;
    qint64 totalSize = 0;
    int totalFiles = 0;
    for (const QPair<QString, qint64> &file : files) {
        if (!isSameOrDescendant(file.first, m_activeFolderPath)) {
            continue;
        }

        const QString extension = normalizedExtension(file.first);
        const QString group = (m_groupMode == GroupMode::Category) ? categoryForExtension(extension) : extension;
        ExtensionRow row = byGroup.value(group);
        row.group = group;
        row.size += file.second;
        row.count += 1;
        byGroup.insert(group, row);
        totalSize += file.second;
        totalFiles += 1;
    }

    QVector<ExtensionRow> rows = byGroup.values().toVector();
    std::sort(rows.begin(), rows.end(), [](const ExtensionRow &left, const ExtensionRow &right) {
        return left.size > right.size;
    });
    if (rows.size() > 100) {
        rows.resize(100);
    }

    {
        TableSortGuard sortGuard(m_table);
        m_table->setRowCount(rows.size());
        for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const ExtensionRow &row = rows[rowIndex];
            const double percent = totalSize <= 0 ? 0.0 : (100.0 * double(row.size) / double(totalSize));
            m_table->setItem(rowIndex, 0, makeTextItem(row.group));
            m_table->setItem(rowIndex, 1, makeNumberItem(SizeFormatter::formatBytes(row.size), row.size));
            m_table->setItem(rowIndex, 2, makeNumberItem(QString::number(row.count), row.count));
            m_table->setItem(rowIndex, 3, makePercentItem(percent));
        }
    }

    if (m_viewMetric == ViewMetric::Files) {
        m_table->sortByColumn(2, Qt::DescendingOrder);
    } else if (m_viewMetric == ViewMetric::Size) {
        m_table->sortByColumn(1, Qt::DescendingOrder);
    } else {
        m_table->sortByColumn(3, Qt::DescendingOrder);
    }

    if (totalFiles == 0) {
        m_summaryLabel->setText(QStringLiteral("Extensions: no files found under %1 in the current scan.").arg(m_activeFolderPath));
        return;
    }

    m_summaryLabel->setText(QStringLiteral("Extensions: %1 files under %2 | Total: %3 | Groups: %4")
                                .arg(totalFiles)
                                .arg(m_activeFolderPath)
                                .arg(SizeFormatter::formatBytes(totalSize))
                                .arg(rows.size()));
}

}
