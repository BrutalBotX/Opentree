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

// Builds shareable reports from a scan result. HTML and CSV are always available; PDF is
// compiled in when Qt PrintSupport is present.
class ReportService {
public:
    static QString buildHtmlReport(const ScanResult &result, const ReportOptions &options = {});
    static bool writeHtmlReport(const QString &filePath, const ScanResult &result,
                                const ReportOptions &options, QString *errorMessage = nullptr);
    static bool writeCsv(const QString &filePath, const QStringList &header,
                         const QVector<QStringList> &rows, QString *errorMessage = nullptr);

    static bool printSupportAvailable();

    // Renders the report into a PDF using Qt's rich text engine.
    static bool writePdfReport(const QString &filePath, const ScanResult &result,
                               const ReportOptions &options, QString *errorMessage = nullptr);
};

}
