#include "services/PdfReportWriter.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QVector>

#include <algorithm>
#include <cmath>

#include "utils/SizeFormatter.h"

namespace opentree {

namespace {

// ---------------------------------------------------------------------------
// Page geometry (points: QPdfWriter is used at 72 dpi, so 1 unit = 1 pt)
// ---------------------------------------------------------------------------

constexpr qreal kPageMarginLeft = 44.0;
constexpr qreal kPageMarginRight = 44.0;
constexpr qreal kPageMarginTop = 40.0;
constexpr qreal kPageMarginBottom = 52.0;
constexpr qreal kRowHeight = 15.0;
constexpr qreal kHeaderRowHeight = 17.0;

const QVector<QColor> &slicePalette()
{
    static const QVector<QColor> palette = {
        QColor("#2563eb"), QColor("#0ea5e9"), QColor("#14b8a6"), QColor("#22c55e"),
        QColor("#eab308"), QColor("#f97316"), QColor("#ef4444"), QColor("#a855f7"),
        QColor("#64748b"),
    };
    return palette;
}

struct Column {
    QString title;
    qreal width = 0.0;
    Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter;
    bool elideMiddle = false;
};

struct TableData {
    QString title;
    QVector<Column> columns;
    QVector<QStringList> rows;
    QString caption;
};

struct Slice {
    QString label;
    qint64 value = 0;
};

struct BarItem {
    QString label;
    qint64 value = 0;
};

// Splits the file-type breakdown into "top N" plus an aggregate "Other" slice.
QVector<Slice> buildTypeSlices(const ScanResult &result, int topCount)
{
    QHash<QString, QPair<qint64, int>> buckets; // extension -> (size, count)
    qint64 total = 0;
    for (const FileEntry &file : result.files) {
        QString suffix = QFileInfo(file.path).suffix().toLower();
        if (suffix.isEmpty()) {
            suffix = QStringLiteral("(none)");
        } else {
            suffix.prepend(QLatin1Char('.'));
        }
        QPair<qint64, int> &bucket = buckets[suffix];
        bucket.first += file.size;
        bucket.second += 1;
        total += file.size;
    }

    QVector<QPair<QString, QPair<qint64, int>>> sorted;
    sorted.reserve(buckets.size());
    for (auto it = buckets.cbegin(); it != buckets.cend(); ++it) {
        sorted.push_back({it.key(), it.value()});
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto &left, const auto &right) {
        return left.second.first > right.second.first;
    });

    QVector<Slice> slices;
    qint64 restSize = 0;
    for (int index = 0; index < sorted.size(); ++index) {
        if (index < topCount) {
            slices.push_back({sorted[index].first, sorted[index].second.first});
        } else {
            restSize += sorted[index].second.first;
        }
    }
    if (restSize > 0 && total > 0) {
        slices.push_back({QStringLiteral("Other"), restSize});
    }
    return slices;
}

// ---------------------------------------------------------------------------
// Report layout
// ---------------------------------------------------------------------------

class ReportDocument {
public:
    ReportDocument(const QString &filePath, const QString &title)
        : m_filePath(filePath)
        , m_title(title)
        , m_writer(filePath)
    {
    }

    bool write(const ScanResult &result, const ReportOptions &options, QString *errorMessage);

private:
    void beginPage();
    void endPage();
    void ensureSpace(qreal height);
    qreal contentWidth() const { return m_pageRect.width(); }
    qreal contentBottom() const { return m_pageRect.bottom(); }

    void drawCover(const ScanResult &result, qint64 totalBytes);
    void drawSectionHeading(const QString &text);
    void drawTable(const TableData &table, bool drawTitle);
    void drawBarChart(const QString &title, const QVector<BarItem> &items);
    void drawPieChart(const QString &title, const QVector<Slice> &slices, qint64 total);
    void drawDonutChart(const QString &title, const QVector<Slice> &slices, qint64 total);
    void drawLegend(qreal top, qreal left, const QVector<Slice> &slices, qint64 total, int maxRows = 9);
    void drawSummaryCards(const QStringList &labels, const QStringList &values, const QVector<QColor> &accents);

    QFont font(qreal size, bool bold = false) const;
    void drawText(const QRectF &rect, const QString &text, const QFont &textFont, const QColor &color,
                  Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter, int elideMode = Qt::ElideNone);

    QString m_filePath;
    QString m_title;
    QPdfWriter m_writer;
    QPainter m_painter;
    QRectF m_pageRect;
    qreal m_cursorY = 0.0;
    int m_pageNumber = 0;
    QString m_footerText;
};

QFont ReportDocument::font(qreal size, bool bold) const
{
    QFont textFont(QStringLiteral("Segoe UI"));
    textFont.setPointSizeF(size);
    textFont.setBold(bold);
    return textFont;
}

void ReportDocument::drawText(const QRectF &rect, const QString &text, const QFont &textFont,
                              const QColor &color, Qt::Alignment alignment, int elideMode)
{
    m_painter.setFont(textFont);
    m_painter.setPen(color);

    QString shown = text;
    if (elideMode != Qt::ElideNone) {
        const QFontMetricsF metrics(textFont);
        shown = metrics.elidedText(text, Qt::TextElideMode(elideMode), rect.width());
    }
    m_painter.drawText(rect, int(alignment), shown);
}

void ReportDocument::beginPage()
{
    if (m_pageNumber > 0) {
        m_writer.newPage();
    }
    ++m_pageNumber;
    m_cursorY = m_pageRect.top();
    // Soft page background so the charts read the same as the tables.
    m_painter.fillRect(QRectF(0, 0, m_pageRect.width() + kPageMarginLeft + kPageMarginRight,
                              m_pageRect.height() + kPageMarginTop + kPageMarginBottom),
                       QColor("#ffffff"));
}

void ReportDocument::endPage()
{
    // Footer: title on the left, page number on the right.
    const QFont footerFont = font(7.5);
    const qreal footerY = m_pageRect.bottom() + 14.0;
    drawText(QRectF(m_pageRect.left(), footerY, contentWidth() * 0.7, 12.0),
             m_footerText, footerFont, QColor("#8a94a3"));
    drawText(QRectF(m_pageRect.left() + contentWidth() * 0.7, footerY, contentWidth() * 0.3, 12.0),
             QStringLiteral("Page %1").arg(m_pageNumber), footerFont, QColor("#8a94a3"),
             Qt::AlignRight | Qt::AlignVCenter);
}

void ReportDocument::ensureSpace(qreal height)
{
    if (m_cursorY + height <= contentBottom()) {
        return;
    }
    endPage();
    beginPage();
}

void ReportDocument::drawSectionHeading(const QString &text)
{
    ensureSpace(30.0);
    const QFont headingFont = font(12.0, true);
    m_painter.setPen(QColor("#1c2330"));
    m_painter.setFont(headingFont);
    m_painter.drawText(QPointF(m_pageRect.left(), m_cursorY + 11.0), text);
    m_cursorY += 17.0;
    m_painter.setPen(QPen(QColor("#d8dee9"), 0.7));
    m_painter.drawLine(QPointF(m_pageRect.left(), m_cursorY),
                       QPointF(m_pageRect.right(), m_cursorY));
    m_cursorY += 9.0;
}

void ReportDocument::drawSummaryCards(const QStringList &labels, const QStringList &values,
                                      const QVector<QColor> &accents)
{
    const int count = labels.size();
    if (count == 0) {
        return;
    }

    const qreal gap = 8.0;
    const qreal cardWidth = (contentWidth() - gap * (count - 1)) / count;
    const qreal cardHeight = 44.0;
    ensureSpace(cardHeight + 10.0);

    for (int index = 0; index < count; ++index) {
        const QRectF card(m_pageRect.left() + index * (cardWidth + gap), m_cursorY, cardWidth, cardHeight);
        m_painter.setPen(Qt::NoPen);
        m_painter.setBrush(QColor("#f5f7fa"));
        m_painter.drawRoundedRect(card, 4.0, 4.0);
        m_painter.setBrush(accents.value(index, QColor("#2563eb")));
        m_painter.drawRoundedRect(QRectF(card.left(), card.top(), 3.0, card.height()), 1.5, 1.5);

        drawText(card.adjusted(9.0, 5.0, -6.0, -cardHeight / 2.0 + 3.0), labels.value(index),
                 font(7.5), QColor("#5a6675"));
        drawText(card.adjusted(9.0, cardHeight / 2.0 - 4.0, -6.0, -5.0), values.value(index),
                 font(13.0, true), QColor("#1c2330"));
    }
    m_cursorY += cardHeight + 14.0;
}

void ReportDocument::drawCover(const ScanResult &result, qint64 totalBytes)
{
    const QFont titleFont = font(19.0, true);
    drawText(QRectF(m_pageRect.left(), m_cursorY, contentWidth(), 24.0), m_title, titleFont,
             QColor("#111827"));
    m_cursorY += 26.0;

    drawText(QRectF(m_pageRect.left(), m_cursorY, contentWidth(), 13.0),
             QStringLiteral("Root: %1").arg(result.rootPath), font(9.0), QColor("#5a6675"),
             Qt::AlignLeft | Qt::AlignVCenter, Qt::ElideMiddle);
    m_cursorY += 13.0;

    drawText(QRectF(m_pageRect.left(), m_cursorY, contentWidth(), 13.0),
             QStringLiteral("Generated %1 with OpenTree | scanned via %2")
                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                      result.usedEverything ? QStringLiteral("the Everything index")
                                            : QStringLiteral("the filesystem walk")),
             font(9.0), QColor("#5a6675"));
    m_cursorY += 20.0;

    qint64 fileBytes = 0;
    for (const FileEntry &file : result.files) {
        fileBytes += file.size;
    }

    drawSummaryCards({QStringLiteral("Total size"), QStringLiteral("Files"), QStringLiteral("Folders"),
                      QStringLiteral("Average file size")},
                     {SizeFormatter::formatBytes(totalBytes),
                      QString::number(result.files.size()),
                      QString::number(result.folders.size()),
                      SizeFormatter::formatBytes(result.files.isEmpty() ? 0 : fileBytes / result.files.size())},
                     {QColor("#2563eb"), QColor("#0ea5e9"), QColor("#14b8a6"), QColor("#a855f7")});
}

void ReportDocument::drawTable(const TableData &table, bool drawTitle)
{
    if (table.rows.isEmpty()) {
        return;
    }

    if (drawTitle) {
        drawSectionHeading(table.title);
    } else {
        ensureSpace(24.0);
    }

    const auto drawHeaderRow = [this, &table]() {
        ensureSpace(kHeaderRowHeight + kRowHeight);
        const QRectF headerRect(m_pageRect.left(), m_cursorY, contentWidth(), kHeaderRowHeight);
        m_painter.setPen(Qt::NoPen);
        m_painter.setBrush(QColor("#eef2f7"));
        m_painter.drawRect(headerRect);

        qreal x = headerRect.left();
        for (const Column &column : table.columns) {
            drawText(QRectF(x + 4.0, headerRect.top(), column.width - 8.0, headerRect.height()),
                     column.title, font(8.5, true), QColor("#334155"), column.alignment);
            x += column.width;
        }
        m_painter.setPen(QPen(QColor("#d8dee9"), 0.6));
        m_painter.drawLine(QPointF(headerRect.left(), headerRect.bottom()),
                           QPointF(headerRect.right(), headerRect.bottom()));
        m_cursorY += kHeaderRowHeight;
    };

    drawHeaderRow();

    for (int rowIndex = 0; rowIndex < table.rows.size(); ++rowIndex) {
        if (m_cursorY + kRowHeight > contentBottom()) {
            endPage();
            beginPage();
            drawText(QRectF(m_pageRect.left(), m_cursorY, contentWidth(), 14.0),
                     QStringLiteral("%1 (continued)").arg(table.title), font(9.5, true), QColor("#334155"));
            m_cursorY += 18.0;
            drawHeaderRow();
        }

        const QStringList &row = table.rows[rowIndex];
        const QRectF rowRect(m_pageRect.left(), m_cursorY, contentWidth(), kRowHeight);
        if (rowIndex % 2 == 1) {
            m_painter.setPen(Qt::NoPen);
            m_painter.setBrush(QColor("#f8fafc"));
            m_painter.drawRect(rowRect);
        }

        qreal x = rowRect.left();
        for (int columnIndex = 0; columnIndex < table.columns.size(); ++columnIndex) {
            const Column &column = table.columns[columnIndex];
            const QString cell = row.value(columnIndex);
            drawText(QRectF(x + 4.0, rowRect.top(), column.width - 8.0, rowRect.height()), cell,
                     font(8.5), QColor("#1f2937"), column.alignment,
                     column.elideMiddle ? Qt::ElideMiddle : Qt::ElideRight);
            x += column.width;
        }

        m_painter.setPen(QPen(QColor("#e5e7eb"), 0.5));
        m_painter.drawLine(QPointF(rowRect.left(), rowRect.bottom()),
                           QPointF(rowRect.right(), rowRect.bottom()));
        m_cursorY += kRowHeight;
    }

    if (!table.caption.isEmpty()) {
        m_cursorY += 3.0;
        drawText(QRectF(m_pageRect.left(), m_cursorY, contentWidth(), 12.0), table.caption,
                 font(7.5), QColor("#8a94a3"));
        m_cursorY += 12.0;
    }
    m_cursorY += 12.0;
}

void ReportDocument::drawBarChart(const QString &title, const QVector<BarItem> &items)
{
    if (items.isEmpty()) {
        return;
    }

    // Only the leading bars are charted; the full list is in the table further down.
    constexpr int kMaximumBars = 10;
    QVector<BarItem> bars = items;
    if (bars.size() > kMaximumBars) {
        bars.resize(kMaximumBars);
    }

    const qreal titleHeight = 16.0;
    const qreal rowHeight = 16.0;
    const qreal chartHeight = titleHeight + bars.size() * rowHeight + 6.0;
    ensureSpace(chartHeight);

    drawText(QRectF(m_pageRect.left(), m_cursorY, contentWidth(), titleHeight), title,
             font(10.0, true), QColor("#334155"));
    m_cursorY += titleHeight + 2.0;

    const qreal labelWidth = std::min<qreal>(190.0, contentWidth() * 0.38);
    const qreal valueWidth = 68.0;
    const qreal barWidth = contentWidth() - labelWidth - valueWidth - 8.0;
    qint64 maximum = 1;
    for (const BarItem &item : bars) {
        maximum = std::max(maximum, item.value);
    }

    for (int index = 0; index < bars.size(); ++index) {
        const BarItem &item = bars[index];
        const QRectF rowRect(m_pageRect.left(), m_cursorY, contentWidth(), rowHeight);
        drawText(QRectF(rowRect.left(), rowRect.top(), labelWidth - 6.0, rowRect.height()),
                 item.label, font(8.0), QColor("#1f2937"), Qt::AlignLeft | Qt::AlignVCenter,
                 Qt::ElideMiddle);

        const qreal ratio = maximum <= 0 ? 0.0 : double(item.value) / double(maximum);
        const qreal barLength = std::max<qreal>(2.0, barWidth * ratio);
        const QRectF barRect(rowRect.left() + labelWidth, rowRect.top() + 4.0, barLength,
                             rowRect.height() - 8.0);
        const QColor color = slicePalette().at(index % slicePalette().size());
        m_painter.setPen(Qt::NoPen);
        m_painter.setBrush(color.lighter(155));
        m_painter.drawRoundedRect(QRectF(barRect.left(), barRect.top(), barWidth, barRect.height()), 2.0, 2.0);
        m_painter.setBrush(color);
        m_painter.drawRoundedRect(barRect, 2.0, 2.0);

        drawText(QRectF(rowRect.left() + labelWidth + barWidth + 6.0, rowRect.top(), valueWidth, rowRect.height()),
                 SizeFormatter::formatBytes(item.value), font(8.0), QColor("#5a6675"),
                 Qt::AlignRight | Qt::AlignVCenter);
        m_cursorY += rowHeight;
    }
    m_cursorY += 10.0;
}

void ReportDocument::drawLegend(qreal top, qreal left, const QVector<Slice> &slices, qint64 total, int maxRows)
{
    const qreal legendLeft = left;
    const qreal legendWidth = m_pageRect.right() - legendLeft;
    const int legendRows = std::min<int>(slices.size(), maxRows);
    const qreal legendRowHeight = 15.0;
    for (int index = 0; index < legendRows; ++index) {
        const Slice &slice = slices[index];
        const qreal y = top + index * legendRowHeight;
        m_painter.setPen(Qt::NoPen);
        m_painter.setBrush(slicePalette().at(index % slicePalette().size()));
        m_painter.drawRoundedRect(QRectF(legendLeft, y + 4.0, 9.0, 9.0), 2.0, 2.0);

        const double share = 100.0 * double(slice.value) / double(total);
        drawText(QRectF(legendLeft + 14.0, y, (legendWidth - 14.0) * 0.55, legendRowHeight), slice.label,
                 font(8.0), QColor("#1f2937"), Qt::AlignLeft | Qt::AlignVCenter, Qt::ElideRight);
        drawText(QRectF(legendLeft + (legendWidth * 0.55), y, legendWidth * 0.45, legendRowHeight),
                 QStringLiteral("%1  (%2%)").arg(SizeFormatter::formatBytes(slice.value)).arg(QString::number(share, 'f', 1)),
                 font(8.0), QColor("#5a6675"), Qt::AlignRight | Qt::AlignVCenter);
    }
}

void ReportDocument::drawPieChart(const QString &title, const QVector<Slice> &slices, qint64 total)
{
    if (slices.isEmpty() || total <= 0) {
        return;
    }

    const qreal chartHeight = 172.0;
    ensureSpace(chartHeight + 10.0);

    drawText(QRectF(m_pageRect.left(), m_cursorY, contentWidth(), 16.0), title,
             font(10.0, true), QColor("#334155"));
    const qreal chartTop = m_cursorY + 18.0;

    const qreal diameter = 130.0;
    const QRectF pie(m_pageRect.left() + 6.0, chartTop, diameter, diameter);
    qreal startAngle = 90.0; // 12 o'clock, clockwise
    for (int index = 0; index < slices.size(); ++index) {
        const Slice &slice = slices[index];
        const qreal span = -360.0 * double(slice.value) / double(total);
        m_painter.setPen(QPen(QColor("#ffffff"), 1.0));
        m_painter.setBrush(slicePalette().at(index % slicePalette().size()));
        m_painter.drawPie(pie, int(startAngle * 16.0), int(span * 16.0));
        startAngle += span;
    }

    drawLegend(chartTop, pie.right() + 18.0, slices, total);
    m_cursorY = chartTop + diameter + 16.0;
}

void ReportDocument::drawDonutChart(const QString &title, const QVector<Slice> &slices, qint64 total)
{
    if (slices.isEmpty() || total <= 0) {
        return;
    }

    const qreal chartHeight = 172.0;
    ensureSpace(chartHeight + 10.0);

    drawText(QRectF(m_pageRect.left(), m_cursorY, contentWidth(), 16.0), title,
             font(10.0, true), QColor("#334155"));
    const qreal chartTop = m_cursorY + 18.0;

    const qreal diameter = 130.0;
    const QRectF donut(m_pageRect.left() + 6.0, chartTop, diameter, diameter);
    qreal startAngle = 90.0; // start at 12 o'clock, clockwise
    for (int index = 0; index < slices.size(); ++index) {
        const Slice &slice = slices[index];
        const qreal span = -360.0 * double(slice.value) / double(total);
        m_painter.setPen(Qt::NoPen);
        m_painter.setBrush(slicePalette().at(index % slicePalette().size()));
        m_painter.drawPie(donut, int(startAngle * 16.0), int(span * 16.0));
        startAngle += span;
    }
    // Punch the middle out with the page colour to make it a donut.
    m_painter.setBrush(QColor("#ffffff"));
    const qreal innerInset = diameter * 0.28;
    m_painter.drawEllipse(donut.adjusted(innerInset, innerInset, -innerInset, -innerInset));

    drawText(QRectF(donut.left(), donut.center().y() - 16.0, donut.width(), 14.0),
             SizeFormatter::formatBytes(total), font(11.0, true), QColor("#111827"),
             Qt::AlignHCenter | Qt::AlignVCenter);
    drawText(QRectF(donut.left(), donut.center().y(), donut.width(), 12.0),
             QStringLiteral("total"), font(7.5), QColor("#8a94a3"), Qt::AlignHCenter | Qt::AlignVCenter);

    // Legend to the right of the donut.
    const int legendRows = std::min<int>(slices.size(), 9);
    drawLegend(chartTop, donut.right() + 18.0, slices, total);

    m_cursorY = std::max(chartTop + diameter, chartTop + legendRows * 15.0) + 16.0;
}

bool ReportDocument::write(const ScanResult &result, const ReportOptions &options, QString *errorMessage)
{
    m_writer.setPageSize(QPageSize(QPageSize::A4));
    m_writer.setResolution(72); // 1 device unit = 1 point
    m_writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    m_writer.setTitle(m_title);
    m_writer.setCreator(QStringLiteral("OpenTree"));

    QFile probe(m_filePath);
    if (!probe.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not write %1 (is the file open elsewhere?).").arg(m_filePath);
        }
        return false;
    }
    probe.close();

    if (!m_painter.begin(&m_writer)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not start the PDF writer for %1.").arg(m_filePath);
        }
        return false;
    }

    m_painter.setRenderHint(QPainter::Antialiasing, true);
    m_pageRect = QRectF(kPageMarginLeft, kPageMarginTop,
                        m_writer.width() - kPageMarginLeft - kPageMarginRight,
                        m_writer.height() - kPageMarginTop - kPageMarginBottom);
    m_footerText = QStringLiteral("%1 | %2").arg(m_title, result.rootPath);

    qint64 totalBytes = 0;
    for (const FileEntry &file : result.files) {
        totalBytes += file.size;
    }

    beginPage();
    drawCover(result, totalBytes);

    // ---- Charts (kept together, right after the cover) ----
    QVector<TreeEntry> folders;
    for (const TreeEntry &entry : result.treeEntries) {
        if (entry.kind == TreeEntryKind::Folder
            && entry.path.compare(result.rootPath, Qt::CaseInsensitive) != 0) {
            folders.push_back(entry);
        }
    }
    std::sort(folders.begin(), folders.end(), [](const TreeEntry &left, const TreeEntry &right) {
        return left.size > right.size;
    });

    const int topFolders = std::max(1, options.topFolders);
    if (folders.size() > topFolders) {
        folders.resize(topFolders);
    }

    const QVector<Slice> typeSlices = buildTypeSlices(result, 8);
    if (!folders.isEmpty() || !typeSlices.isEmpty()) {
        drawSectionHeading(QStringLiteral("Charts"));
        QVector<BarItem> bars;
        bars.reserve(folders.size());
        for (const TreeEntry &folder : folders) {
            bars.push_back({folder.path, folder.size});
        }
        drawBarChart(QStringLiteral("Largest folders"), bars);

        // Pie: how the scanned total splits across the biggest folders.
        QVector<Slice> folderSlices;
        qint64 topSum = 0;
        const int pieSlices = std::min<int>(folders.size(), 6);
        for (int index = 0; index < pieSlices; ++index) {
            const TreeEntry &folder = folders[index];
            folderSlices.push_back({folder.name.isEmpty() ? folder.path : folder.name, folder.size});
            topSum += folder.size;
        }
        if (totalBytes > topSum) {
            folderSlices.push_back({QStringLiteral("Other folders and files"), totalBytes - topSum});
        }
        drawPieChart(QStringLiteral("Folder share"), folderSlices, totalBytes);

        drawDonutChart(QStringLiteral("File types by size"), typeSlices, totalBytes);
    }

    // ---- Tables ----
    TableData folderTable;
    folderTable.title = QStringLiteral("Largest folders");
    folderTable.columns = {
        {QStringLiteral("#"), 22.0, Qt::AlignRight | Qt::AlignVCenter, false},
        {QStringLiteral("Folder"), 0.0, Qt::AlignLeft | Qt::AlignVCenter, true},
        {QStringLiteral("Size"), 62.0, Qt::AlignRight | Qt::AlignVCenter, false},
        {QStringLiteral("Share"), 46.0, Qt::AlignRight | Qt::AlignVCenter, false},
        {QStringLiteral("Files"), 40.0, Qt::AlignRight | Qt::AlignVCenter, false},
        {QStringLiteral("Folders"), 46.0, Qt::AlignRight | Qt::AlignVCenter, false},
    };
    folderTable.columns[1].width = contentWidth() - 22.0 - 62.0 - 46.0 - 40.0 - 46.0;
    for (int index = 0; index < folders.size(); ++index) {
        const TreeEntry &folder = folders[index];
        const double share = totalBytes <= 0 ? 0.0 : 100.0 * double(folder.size) / double(totalBytes);
        folderTable.rows.push_back({
            QString::number(index + 1),
            folder.path,
            SizeFormatter::formatBytes(folder.size),
            QStringLiteral("%1%").arg(QString::number(share, 'f', 1)),
            QString::number(folder.fileCount),
            QString::number(folder.folderCount),
        });
    }
    folderTable.caption = QStringLiteral("Top %1 folders under the scanned root, largest first.")
                              .arg(folderTable.rows.size());
    drawTable(folderTable, true);

    QVector<FileEntry> files = result.files;
    std::sort(files.begin(), files.end(), [](const FileEntry &left, const FileEntry &right) {
        return left.size > right.size;
    });
    if (files.size() > options.topFiles) {
        files.resize(options.topFiles);
    }

    TableData fileTable;
    fileTable.title = QStringLiteral("Largest files");
    fileTable.columns = {
        {QStringLiteral("#"), 22.0, Qt::AlignRight | Qt::AlignVCenter, false},
        {QStringLiteral("File"), 0.0, Qt::AlignLeft | Qt::AlignVCenter, true},
        {QStringLiteral("Size"), 70.0, Qt::AlignRight | Qt::AlignVCenter, false},
    };
    fileTable.columns[1].width = contentWidth() - 22.0 - 70.0;
    for (int index = 0; index < files.size(); ++index) {
        fileTable.rows.push_back({QString::number(index + 1), files[index].path,
                                  SizeFormatter::formatBytes(files[index].size)});
    }
    fileTable.caption = QStringLiteral("Top %1 files under the scanned root, largest first.")
                            .arg(fileTable.rows.size());
    drawTable(fileTable, true);

    TableData typeTable;
    typeTable.title = QStringLiteral("File types");
    typeTable.columns = {
        {QStringLiteral("Extension"), 110.0, Qt::AlignLeft | Qt::AlignVCenter, false},
        {QStringLiteral("Size"), 0.0, Qt::AlignRight | Qt::AlignVCenter, false},
        {QStringLiteral("Share"), 60.0, Qt::AlignRight | Qt::AlignVCenter, false},
    };
    typeTable.columns[1].width = contentWidth() - 110.0 - 60.0;
    qint64 otherSize = 0;
    int otherCount = 0;
    {
        QHash<QString, QPair<qint64, int>> buckets;
        for (const FileEntry &file : result.files) {
            QString suffix = QFileInfo(file.path).suffix().toLower();
            if (suffix.isEmpty()) {
                suffix = QStringLiteral("(none)");
            } else {
                suffix.prepend(QLatin1Char('.'));
            }
            QPair<qint64, int> &bucket = buckets[suffix];
            bucket.first += file.size;
            bucket.second += 1;
        }
        QVector<QPair<QString, QPair<qint64, int>>> sorted;
        for (auto it = buckets.cbegin(); it != buckets.cend(); ++it) {
            sorted.push_back({it.key(), it.value()});
        }
        std::sort(sorted.begin(), sorted.end(), [](const auto &left, const auto &right) {
            return left.second.first > right.second.first;
        });

        const int topTypes = std::max(1, options.extensions);
        for (int index = 0; index < sorted.size(); ++index) {
            if (index >= topTypes) {
                otherSize += sorted[index].second.first;
                otherCount += sorted[index].second.second;
                continue;
            }
            const double share = totalBytes <= 0 ? 0.0 : 100.0 * double(sorted[index].second.first) / double(totalBytes);
            typeTable.rows.push_back({
                sorted[index].first,
                SizeFormatter::formatBytes(sorted[index].second.first),
                QStringLiteral("%1%").arg(QString::number(share, 'f', 1)),
            });
        }
    }
    if (otherCount > 0) {
        const double share = totalBytes <= 0 ? 0.0 : 100.0 * double(otherSize) / double(totalBytes);
        typeTable.rows.push_back({QStringLiteral("Other (%1 types)").arg(otherCount),
                                  SizeFormatter::formatBytes(otherSize),
                                  QStringLiteral("%1%").arg(QString::number(share, 'f', 1))});
    }
    typeTable.caption = QStringLiteral("Sorted by total size. Share is relative to all %1 files.")
                            .arg(result.files.size());
    drawTable(typeTable, true);

    endPage();
    m_painter.end();

    if (errorMessage) {
        errorMessage->clear();
    }
    return QFileInfo::exists(m_filePath) && QFileInfo(m_filePath).size() > 0;
}

} // namespace

bool PdfReportWriter::write(const QString &filePath, const ScanResult &result,
                            const ReportOptions &options, QString *errorMessage)
{
    if (filePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No output file was given.");
        }
        return false;
    }
    if (result.rootPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Scan a folder before exporting a report.");
        }
        return false;
    }

    ReportDocument document(filePath, options.title);
    return document.write(result, options, errorMessage);
}

} // namespace opentree
