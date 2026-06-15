#pragma once

#include <string>
#include <vector>

#include "diagnostic_result.hpp"
#include "diagnostic_status.hpp"

// ReportGenerator renders a set of DiagnosticResults in three formats:
//   - terminal : color-coded, human-readable summary
//   - HTML     : self-contained, styled report (reports/latest_report.html)
//   - JSON     : structured, machine-readable log (logs/latest_report.json)
class ReportGenerator {
public:
    void printTerminalReport(const std::vector<DiagnosticResult>& results) const;
    void writeHtmlReport(const std::vector<DiagnosticResult>& results,
                         const std::string& outputPath) const;
    void writeJsonReport(const std::vector<DiagnosticResult>& results,
                         const std::string& outputPath) const;

    // The overall status is the most severe status across all results.
    static DiagnosticStatus overallStatus(
        const std::vector<DiagnosticResult>& results);
};
