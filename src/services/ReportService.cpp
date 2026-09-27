#include "services/ReportService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <numeric>

#include "services/PdfReportWriter.h"
#include "utils/SizeFormatter.h"

namespace opentree {

namespace {

struct ExtensionBucket {
    QString extension;
    qint64 size = 0;
    int count = 0;
};

struct ReportData {
    QVector<TreeEntry> folders;      // largest first
    QVector<FileEntry> files;        // largest first
    QVector<ExtensionBucket> types;  // largest first
    qint64 totalBytes = 0;
};

QString htmlEscape(QString value)
{
    value.replace('&', QStringLiteral("&amp;"));
    value.replace('<', QStringLiteral("&lt;"));
    value.replace('>', QStringLiteral("&gt;"));
    value.replace('"', QStringLiteral("&quot;"));
    return value;
}

QString typeKeyFor(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix.isEmpty() ? QStringLiteral("(none)") : QStringLiteral(".") + suffix;
}

ReportData collectReportData(const ScanResult &result, const ReportOptions &options)
{
    ReportData data;

    for (const FileEntry &file : result.files) {
        data.totalBytes += file.size;

        ExtensionBucket *bucket = nullptr;
        const QString key = typeKeyFor(file.path);
        for (ExtensionBucket &candidate : data.types) {
            if (candidate.extension == key) {
                bucket = &candidate;
                break;
            }
        }
        if (!bucket) {
            data.types.push_back({key, 0, 0});
            bucket = &data.types.last();
        }
        bucket->size += file.size;
        bucket->count += 1;
    }
    std::sort(data.types.begin(), data.types.end(), [](const ExtensionBucket &left, const ExtensionBucket &right) {
        return left.size > right.size;
    });
    if (data.types.size() > options.extensions) {
        data.types.resize(options.extensions);
    }

    for (const TreeEntry &entry : result.treeEntries) {
        if (entry.kind == TreeEntryKind::Folder
            && entry.path.compare(result.rootPath, Qt::CaseInsensitive) != 0) {
            data.folders.push_back(entry);
        }
    }
    std::sort(data.folders.begin(), data.folders.end(), [](const TreeEntry &left, const TreeEntry &right) {
        return left.size > right.size;
    });
    if (data.folders.size() > options.topFolders) {
        data.folders.resize(options.topFolders);
    }

    // Top-N without copying the whole file list: only an index vector (4 bytes per file)
    // is materialised, which matters on a whole-drive scan where the list is huge.
    const int fileLimit = std::max(1, options.topFiles);
    if (!result.files.isEmpty()) {
        QVector<int> order(result.files.size());
        std::iota(order.begin(), order.end(), 0);
        const int keep = std::min<int>(fileLimit, order.size());
        std::nth_element(order.begin(), order.begin() + keep - 1, order.end(),
                         [&result](int left, int right) {
                             return result.files[left].size > result.files[right].size;
                         });
        order.resize(keep);
        std::sort(order.begin(), order.end(), [&result](int left, int right) {
            return result.files[left].size > result.files[right].size;
        });
        data.files.reserve(keep);
        for (int index : order) {
            data.files.push_back(result.files[index]);
        }
    }

    return data;
}

const QVector<QString> &chartColors()
{
    static const QVector<QString> colors = {
        QStringLiteral("#2563eb"), QStringLiteral("#0ea5e9"), QStringLiteral("#14b8a6"),
        QStringLiteral("#22c55e"), QStringLiteral("#eab308"), QStringLiteral("#f97316"),
        QStringLiteral("#ef4444"), QStringLiteral("#a855f7"), QStringLiteral("#64748b"),
    };
    return colors;
}

QString buildStyles()
{
    return QStringLiteral(
        "body{font-family:'Segoe UI',sans-serif;background:#ffffff;color:#1c2330;margin:32px;}"
        "h1{font-size:22px;margin:0 0 4px 0;}"
        "h2{font-size:16px;margin:28px 0 8px 0;border-bottom:1px solid #d8dee9;padding-bottom:4px;}"
        "h3{font-size:13px;margin:12px 0 6px 0;}"
        "p{font-size:13px;margin:4px 0;}"
        ".meta{color:#5a6675;font-size:12px;}"
        ".totals{font-size:13px;margin:10px 0 0 0;}"
        "table{border-collapse:collapse;width:100%;font-size:12px;}"
        "th,td{border:1px solid #d8dee9;padding:5px 8px;text-align:left;}"
        "th{background:#eef2f7;font-weight:600;}"
        "td.num{text-align:right;white-space:nowrap;}"
        "tr:nth-child(even) td{background:#f7f9fc;}"
        ".barrow{display:flex;align-items:center;margin:3px 0;font-size:12px;}"
        ".barlabel{width:220px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}"
        ".bartrack{flex:1;height:11px;background:#eef2f7;border-radius:3px;margin:0 8px;}"
        ".bar{display:block;height:11px;border-radius:3px;}"
        ".barvalue{width:90px;text-align:right;color:#5a6675;white-space:nowrap;}"
        ".stack{display:flex;height:18px;border-radius:3px;overflow:hidden;background:#eef2f7;}"
        ".stackseg{display:block;height:18px;}"
        ".legend{font-size:12px;color:#5a6675;margin-top:6px;}"
        ".legend span{display:inline-flex;align-items:center;margin-right:14px;}"
        ".legend i{width:10px;height:10px;display:inline-block;border-radius:2px;margin-right:5px;}"
        ".pierow{display:flex;align-items:center;gap:20px;margin:6px 0 10px 0;}"
        ".pierow svg{flex:0 0 auto;}"
        ".legendcol{font-size:12px;color:#5a6675;display:flex;flex-direction:column;gap:4px;}");
}

// Inline SVG pie so the HTML report carries the same chart as the PDF.
QString buildSvgPie(const QVector<QPair<QString, qint64>> &slices, qint64 total, int size = 200)
{
    if (slices.isEmpty() || total <= 0) {
        return {};
    }

    QString svg = QStringLiteral("<svg width='%1' height='%1' viewBox='0 0 %1 %1' xmlns='http://www.w3.org/2000/svg'>")
                      .arg(size);
    const double center = size / 2.0;
    const double radius = center - 2.0;
    double angle = -90.0; // start at 12 o'clock

    for (int index = 0; index < slices.size(); ++index) {
        const QString &label = slices[index].first;
        const qint64 value = slices[index].second;
        const double sweep = 360.0 * double(value) / double(total);
        if (sweep <= 0.0) {
            continue;
        }
        const QString color = chartColors().at(index % chartColors().size());

        if (sweep >= 359.99) {
            svg += QStringLiteral("<circle cx='%1' cy='%1' r='%2' fill='%3'><title>%4</title></circle>")
                       .arg(QString::number(center, 'f', 2), QString::number(radius, 'f', 2), color,
                            htmlEscape(label));
            angle += sweep;
            continue;
        }

        const double startRadians = qDegreesToRadians(angle);
        const double endRadians = qDegreesToRadians(angle + sweep);
        const double x0 = center + radius * std::cos(startRadians);
        const double y0 = center + radius * std::sin(startRadians);
        const double x1 = center + radius * std::cos(endRadians);
        const double y1 = center + radius * std::sin(endRadians);
        const int largeArc = sweep > 180.0 ? 1 : 0;

        svg += QStringLiteral("<path d='M%1 %2 L%3 %4 A%5 %5 0 %6 1 %7 %8 Z' fill='%9'><title>%10</title></path>")
                   .arg(QString::number(center, 'f', 2), QString::number(center, 'f', 2),
                        QString::number(x0, 'f', 2), QString::number(y0, 'f', 2),
                        QString::number(radius, 'f', 2), QString::number(largeArc),
                        QString::number(x1, 'f', 2), QString::number(y1, 'f', 2),
                        color, htmlEscape(label));
        angle += sweep;
    }

    svg += QStringLiteral("</svg>");
    return svg;
}

QString buildTotalsRow(const ScanResult &result)
{
    qint64 totalBytes = 0;
    for (const FileEntry &file : result.files) {
        totalBytes += file.size;
    }

    return QStringLiteral("<p class='totals'><b>%1</b> across <b>%2</b> files in <b>%3</b> folders &middot; scanned via %4</p>")
        .arg(SizeFormatter::formatBytes(totalBytes))
        .arg(result.files.size())
        .arg(result.folders.size())
        .arg(result.usedEverything ? QStringLiteral("the Everything index") : QStringLiteral("filesystem walk"));
}

} // namespace

bool ReportService::pdfReportAvailable()
{
    return true;
}

QString ReportService::buildHtmlReport(const ScanResult &result, const ReportOptions &options)
{
    const ReportData data = collectReportData(result, options);

    QStringList html;
    html << QStringLiteral("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>")
         << QStringLiteral("<title>%1</title>").arg(htmlEscape(options.title))
         << QStringLiteral("<style>%1</style></head><body>").arg(buildStyles());

    html << QStringLiteral("<h1>%1</h1>").arg(htmlEscape(options.title))
         << QStringLiteral("<p class='meta'>Root: %1</p>").arg(htmlEscape(result.rootPath))
         << QStringLiteral("<p class='meta'>Generated: %1</p>")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
         << buildTotalsRow(result);

    // ---- Charts (pure CSS so the report stays a single self-contained file) ----
    const bool hasFolderChart = !data.folders.isEmpty();
    const bool hasTypeChart = !data.types.isEmpty() && data.totalBytes > 0;
    if (hasFolderChart || hasTypeChart) {
        html << QStringLiteral("<h2>Charts</h2>");
    }

    if (hasFolderChart) {
        html << QStringLiteral("<h3>Largest folders</h3>");
        const qint64 maximum = data.folders.first().size;
        // Chart the leaders only; the full list follows in the table below.
        const int charted = std::min<int>(data.folders.size(), 15);
        for (int index = 0; index < charted; ++index) {
            const TreeEntry &folder = data.folders[index];
            const double width = maximum <= 0 ? 0.0 : 100.0 * double(folder.size) / double(maximum);
            html << QStringLiteral(
                     "<div class='barrow'><span class='barlabel' title='%1'>%2</span>"
                     "<span class='bartrack'><span class='bar' style='width:%3%;background:%4'></span></span>"
                     "<span class='barvalue'>%5</span></div>")
                     .arg(htmlEscape(folder.path),
                          htmlEscape(folder.name.isEmpty() ? folder.path : folder.name),
                          QString::number(width, 'f', 2),
                          chartColors().at(index % chartColors().size()),
                          SizeFormatter::formatBytes(folder.size));
        }

        // Pie: how the scanned total splits across the biggest folders.
        QVector<QPair<QString, qint64>> shareSlices;
        qint64 topSum = 0;
        const int pieSlices = std::min<int>(data.folders.size(), 6);
        for (int index = 0; index < pieSlices; ++index) {
            const TreeEntry &folder = data.folders[index];
            shareSlices.push_back({folder.name.isEmpty() ? folder.path : folder.name, folder.size});
            topSum += folder.size;
        }
        if (data.totalBytes > topSum) {
            shareSlices.push_back({QStringLiteral("Other folders and files"), data.totalBytes - topSum});
        }

        html << QStringLiteral("<h3>Folder share</h3><div class='pierow'>%1<div class='legendcol'>")
                    .arg(buildSvgPie(shareSlices, data.totalBytes));
        for (int index = 0; index < shareSlices.size(); ++index) {
            const double share = data.totalBytes <= 0
                ? 0.0
                : 100.0 * double(shareSlices[index].second) / double(data.totalBytes);
            html << QStringLiteral("<span><i style='background:%1'></i>%2 &middot; %3 (%4%)</span>")
                        .arg(chartColors().at(index % chartColors().size()),
                             htmlEscape(shareSlices[index].first),
                             SizeFormatter::formatBytes(shareSlices[index].second),
                             QString::number(share, 'f', 1));
        }
        html << QStringLiteral("</div></div>");
    }

    if (hasTypeChart) {
        html << QStringLiteral("<h3>File types by size</h3><div class='stack'>");
        for (int index = 0; index < data.types.size(); ++index) {
            const ExtensionBucket &bucket = data.types[index];
            const double share = 100.0 * double(bucket.size) / double(data.totalBytes);
            html << QStringLiteral("<span class='stackseg' style='width:%1%;background:%2' title='%3'></span>")
                        .arg(QString::number(share, 'f', 2),
                             chartColors().at(index % chartColors().size()),
                             htmlEscape(bucket.extension));
        }
        html << QStringLiteral("</div><div class='legend'>");
        for (int index = 0; index < data.types.size(); ++index) {
            const ExtensionBucket &bucket = data.types[index];
            const double share = 100.0 * double(bucket.size) / double(data.totalBytes);
            html << QStringLiteral("<span><i style='background:%1'></i>%2 &middot; %3 (%4%)</span>")
                        .arg(chartColors().at(index % chartColors().size()),
                             htmlEscape(bucket.extension),
                             SizeFormatter::formatBytes(bucket.size),
                             QString::number(share, 'f', 1));
        }
        html << QStringLiteral("</div>");
    }

    // ---- Tables ----
    if (!data.folders.isEmpty()) {
        html << QStringLiteral("<h2>Largest folders</h2><table><tr><th>#</th><th>Folder</th><th>Size</th>"
                               "<th>Share</th><th>Files</th><th>Folders</th></tr>");
        for (int index = 0; index < data.folders.size(); ++index) {
            const TreeEntry &folder = data.folders[index];
            const double share = data.totalBytes <= 0 ? 0.0 : 100.0 * double(folder.size) / double(data.totalBytes);
            html << QStringLiteral("<tr><td class='num'>%1</td><td>%2</td><td class='num'>%3</td>"
                                   "<td class='num'>%4%</td><td class='num'>%5</td><td class='num'>%6</td></tr>")
                        .arg(index + 1)
                        .arg(htmlEscape(folder.path),
                             SizeFormatter::formatBytes(folder.size),
                             QString::number(share, 'f', 1),
                             QString::number(folder.fileCount),
                             QString::number(folder.folderCount));
        }
        html << QStringLiteral("</table>");
    }

    if (!data.files.isEmpty()) {
        html << QStringLiteral("<h2>Largest files</h2><table><tr><th>#</th><th>File</th><th>Size</th></tr>");
        for (int index = 0; index < data.files.size(); ++index) {
            html << QStringLiteral("<tr><td class='num'>%1</td><td>%2</td><td class='num'>%3</td></tr>")
                        .arg(index + 1)
                        .arg(htmlEscape(data.files[index].path),
                             SizeFormatter::formatBytes(data.files[index].size));
        }
        html << QStringLiteral("</table>");
    }

    if (!data.types.isEmpty()) {
        html << QStringLiteral("<h2>File types</h2><table><tr><th>Extension</th><th>Size</th>"
                               "<th>Files</th><th>Share</th></tr>");
        for (const ExtensionBucket &bucket : data.types) {
            const double share = data.totalBytes <= 0 ? 0.0 : 100.0 * double(bucket.size) / double(data.totalBytes);
            html << QStringLiteral("<tr><td>%1</td><td class='num'>%2</td><td class='num'>%3</td>"
                                   "<td class='num'>%4%</td></tr>")
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
    return PdfReportWriter::write(filePath, result, options, errorMessage);
}

} // namespace opentree
