#pragma once

#include <QString>

#include "domain/ScanTypes.h"
#include "services/ReportService.h"

namespace opentree {

// Writes the storage report as a real PDF: laid-out tables (paginated, alternating rows,
// right-aligned numbers, elided paths) plus charts (bar chart of the largest folders and a
// donut of the file-type breakdown).
//
// Uses QPdfWriter from QtGui, so no Qt PrintSupport dependency is needed.
class PdfReportWriter {
public:
    static bool write(const QString &filePath, const ScanResult &result,
                      const ReportOptions &options, QString *errorMessage = nullptr);
};

} // namespace opentree
