#include "services/ReportService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>

#include <algorithm>

#include "utils/SizeFormatter.h"

#if defined(OPENTREE_HAVE_PRINTSUPPORT)
#include <QPageSize>
#include <QPrinter>
#include <QTextDocument>
#endif

namespace opentree {

namespace {

struct ExtensionBucket {
    QString extension;
    qint64 size = 0;
    int count = 0;
};

QString htmlEscape(QString value)
{
    value.replace('&', QStringLiteral("&amp;"));
    value.replace('<', QStringLiteral("&lt;"));
    value.replace('>', QStringLiteral("&gt;"));
    value.replace('"', QStringLiteral("&quot;"));
    return value;
}

QString extensionOf(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix.isEmpty() ? QStringLiteral("(none)") : QStringLiteral(".") + suffix;
}

QString buildTotalsRow(const ScanResult &result)
{
    qint64 totalBytes = 0;
    for (const FileEntry &file : result.files) {
        totalBytes += file.size;
    }

    return QStringLiteral("<p class='totals'><b>%1</b> across <b>%2</b> files in <b>%3</b> folders &middot; scanned via %4</p>")
        .arg(htmlEscape(SizeFormatter::formatBytes(totalBytes)))
        .arg(result.files.size())
        .arg(result.folders.size())
        .arg(result.usedEverything ? QStringLiteral("Everything index") : QStringLiteral("filesystem walk"));
}

} // namespace

bool ReportService::printSupportAvailable()
{
#if defined(OPENTREE_HAVE_PRINTSUPPORT)
    return true;
#else
    return false;
#endif
}

QString ReportService::buildHtmlReport(const ScanResult &result, const ReportOptions &options)
{
    QStringList html;
    html << QStringLiteral("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>")
         << QStringLiteral("<title>%1</title>").arg(htmlEscape(options.title))
         << QStringLiteral("<style>"
                           "body{font-family:'Segoe UI',sans-serif;background:#ffffff;color:#1c2330;margin:32px;}"
                           "h1{font-size:22px;margin:0 0 4px 0;}"
                           "h2{font-size:16px;margin:28px 0 8px 0;border-bottom:1px solid #d8dee9;padding-bottom:4px;}"
                           "p{font-size:13px;margin:4px 0;}"
                           ".meta{color:#5a6675;font-size:12px;}"
                           ".totals{font-size:13px;margin:10px 0 0 0;}"
                           "table{border-collapse:collapse;width:100%;font-size:12px;}"
                           "th,td{border:1px solid #d8dee9;padding:5px 8px;text-align:left;}"
                           "th{background:#eef2f7;font-weight:600;}"
                           "td.num{text-align:right;white-space:nowrap;}"
                           "tr:nth-child(even) td{background:#f7f9fc;}"
                           "</style></head><body>");

    html << QStringLiteral("<h1>%1</h1>").arg(htmlEscape(options.title))
         << QStringLiteral("<p class='meta'>Root: %1</p>").arg(htmlEscape(result.rootPath))
         << QStringLiteral("<p class='meta'>Generated: %1</p>")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
         << buildTotalsRow(result);

    // Top folders by size.
    QVector<TreeEntry> folders;
    for (const TreeEntry &entry : result.treeEntries) {
        if (entry.kind == TreeEntryKind::Folder && entry.path.compare(result.rootPath, Qt::CaseInsensitive) != 0) {
            folders.push_back(entry);
        }
    }
    std::sort(folders.begin(), folders.end(), [](const TreeEntry &left, const TreeEntry &right) {
        return left.size > right.size;
    });
    if (folders.size() > options.topFolders) {
        folders.resize(options.topFolders);
    }

    if (!folders.isEmpty()) {
        html << QStringLiteral("<h2>Largest folders</h2><table><tr><th>Folder</th><th>Size</th><th>Files</th><th>Folders</th></tr>");
        for (const TreeEntry &folder : folders) {
            html << QStringLiteral("<tr><td>%1</td><td class='num'>%2</td><td class='num'>%3</td><td class='num'>%4</td></tr>")
                        .arg(htmlEscape(folder.path),
                             SizeFormatter::formatBytes(folder.size),
                             QString::number(folder.fileCount),
                             QString::number(folder.folderCount));
        }
        html << QStringLiteral("</table>");
    }

    // Largest files.
    QVector<FileEntry> files = result.files;
    std::sort(files.begin(), files.end(), [](const FileEntry &left, const FileEntry &right) {
        return left.size > right.size;
    });
    if (files.size() > options.topFiles) {
        files.resize(options.topFiles);
    }

    if (!files.isEmpty()) {
        html << QStringLiteral("<h2>Largest files</h2><table><tr><th>File</th><th>Size</th></tr>");
        for (const FileEntry &file : files) {
            html << QStringLiteral("<tr><td>%1</td><td class='num'>%2</td></tr>")
                        .arg(htmlEscape(file.path), SizeFormatter::formatBytes(file.size));
        }
        html << QStringLiteral("</table>");
    }

    // Extension breakdown.
    QHash<QString, ExtensionBucket> buckets;
    qint64 totalBytes = 0;
    for (const FileEntry &file : result.files) {
        const QString extension = extensionOf(file.path);
        ExtensionBucket &bucket = buckets[extension];
        bucket.extension = extension;
        bucket.size += file.size;
        bucket.count += 1;
        totalBytes += file.size;
    }
    QVector<ExtensionBucket> extensionRows = buckets.values().toVector();
    std::sort(extensionRows.begin(), extensionRows.end(), [](const ExtensionBucket &left, const ExtensionBucket &right) {
        return left.size > right.size;
    });
    if (extensionRows.size() > options.extensions) {
        extensionRows.resize(options.extensions);
    }

    if (!extensionRows.isEmpty()) {
        html << QStringLiteral("<h2>File types</h2><table><tr><th>Extension</th><th>Size</th><th>Files</th><th>Share</th></tr>");
        for (const ExtensionBucket &bucket : extensionRows) {
            const double share = totalBytes <= 0 ? 0.0 : (100.0 * double(bucket.size) / double(totalBytes));
            html << QStringLiteral("<tr><td>%1</td><td class='num'>%2</td><td class='num'>%3</td><td class='num'>%4%</td></tr>")
                        .arg(htmlEscape(bucket.extension),
                             SizeFormatter::formatBytes(bucket.size),
                             QString::number(bucket.count),
                             QString::number(share, 'f', 1));
        }
        html << QStringLiteral("</table>");
    }

    html << QStringLiteral("<p class='meta'>Generated by OpenTree.</p></body></html>");
    return html.join(QLatin1Char('\n'));
}

bool ReportService::writeHtmlReport(const QString &filePath, const ScanResult &result,
                                    const ReportOptions &options, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not write %1.").arg(filePath);
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << buildHtmlReport(result, options);
    stream.flush();

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool ReportService::writeCsv(const QString &filePath, const QStringList &header,
                             const QVector<QStringList> &rows, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not write %1.").arg(filePath);
        }
        return false;
    }

    const auto quote = [](const QString &value) {
        QString escaped = value;
        escaped.replace('"', QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(escaped);
    };

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << header.join(QLatin1Char(',')) << '\n';
    for (const QStringList &row : rows) {
        QStringList quoted;
        quoted.reserve(row.size());
        for (const QString &cell : row) {
            quoted << quote(cell);
        }
        stream << quoted.join(QLatin1Char(',')) << '\n';
    }
    stream.flush();

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool ReportService::writePdfReport(const QString &filePath, const ScanResult &result,
                                   const ReportOptions &options, QString *errorMessage)
{
#if defined(OPENTREE_HAVE_PRINTSUPPORT)
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(filePath);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);

    QTextDocument document;
    document.setHtml(buildHtmlReport(result, options));
    document.print(&printer);

    if (errorMessage) {
        errorMessage->clear();
    }
    return QFileInfo::exists(filePath);
#else
    Q_UNUSED(filePath);
    Q_UNUSED(result);
    Q_UNUSED(options);
    if (errorMessage) {
        *errorMessage = QStringLiteral("This build has no PDF support (Qt PrintSupport missing).");
    }
    return false;
#endif
}

}
