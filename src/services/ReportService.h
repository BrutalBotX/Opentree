#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "domain/ScanTypes.h"

namespace opentree {

struct ReportOptions {
    QString title = QStringLiteral("OpenTree Storage Report");
    int topFolders = 40;
    int topFiles = 40;
    int extensions = 25;
};

// Builds shareable reports from a scan result: a self-contained HTML file (with CSS charts),
// CSV helpers and a laid-out PDF (tables + charts via QPdfWriter, no PrintSupport needed).
class ReportService {
public:
    static QString buildHtmlReport(const ScanResult &result, const ReportOptions &options = {});
    static bool writeHtmlReport(const QString &filePath, const ScanResult &result,
                                const ReportOptions &options, QString *errorMessage = nullptr);
    static bool writeCsv(const QString &filePath, const QStringList &header,
                         const QVector<QStringList> &rows, QString *errorMessage = nullptr);

    // Always true: the PDF writer is part of QtGui.
    static bool pdfReportAvailable();

    // Writes the report as a paginated PDF with charts and proper tables.
    static bool writePdfReport(const QString &filePath, const ScanResult &result,
                               const ReportOptions &options, QString *errorMessage = nullptr);
};

} // namespace opentree
