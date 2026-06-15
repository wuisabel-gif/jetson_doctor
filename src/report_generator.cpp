#include "report_generator.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// ANSI color codes for the terminal report.
const char* RESET  = "\033[0m";
const char* GREEN  = "\033[32m";
const char* YELLOW = "\033[33m";
const char* RED    = "\033[31m";
const char* GRAY   = "\033[90m";
const char* BOLD   = "\033[1m";

const char* colorFor(DiagnosticStatus status) {
    switch (status) {
        case DiagnosticStatus::PASS:    return GREEN;
        case DiagnosticStatus::WARN:    return YELLOW;
        case DiagnosticStatus::FAIL:    return RED;
        case DiagnosticStatus::UNKNOWN: return GRAY;
    }
    return GRAY;
}

// HTML hex color for each status (used in the styled report).
const char* htmlColorFor(DiagnosticStatus status) {
    switch (status) {
        case DiagnosticStatus::PASS:    return "#2e7d32";  // green
        case DiagnosticStatus::WARN:    return "#ef6c00";  // orange
        case DiagnosticStatus::FAIL:    return "#c62828";  // red
        case DiagnosticStatus::UNKNOWN: return "#757575";  // gray
    }
    return "#757575";
}

std::string nowTimestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

// Format a numeric value with one decimal place, or its text value, or "N/A".
std::string formatValue(const DiagnosticResult& r) {
    if (r.hasValue) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << r.value;
        if (!r.unit.empty()) oss << " " << r.unit;
        return oss.str();
    }
    if (!r.textValue.empty()) return r.textValue;
    return "N/A";
}

// Escape the handful of characters that matter for embedding text in HTML.
std::string htmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:  out += c; break;
        }
    }
    return out;
}

// Escape a string for safe inclusion in JSON.
std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

}  // namespace

DiagnosticStatus ReportGenerator::overallStatus(
    const std::vector<DiagnosticResult>& results) {
    DiagnosticStatus worst = DiagnosticStatus::PASS;
    for (const auto& r : results) {
        if (severity(r.status) > severity(worst)) worst = r.status;
    }
    return worst;
}

void ReportGenerator::printTerminalReport(
    const std::vector<DiagnosticResult>& results) const {
    std::cout << BOLD << "Jetson Doctor Diagnostic Report" << RESET << "\n";
    std::cout << "--------------------------------\n";
    std::cout << GRAY << nowTimestamp() << RESET << "\n\n";

    for (const auto& r : results) {
        // Left-align the metric name, then value, then colored status.
        std::ostringstream line;
        line << std::left << std::setw(20) << (r.name + ":")
             << std::setw(16) << formatValue(r);

        std::cout << line.str()
                  << colorFor(r.status) << toString(r.status) << RESET;

        // Show the explanation for anything that is not a clean PASS.
        if (r.status != DiagnosticStatus::PASS && !r.message.empty()) {
            std::cout << "  " << GRAY << "— " << r.message << RESET;
        }
        std::cout << "\n";
    }

    const DiagnosticStatus overall = overallStatus(results);
    std::cout << "\n" << BOLD << "Overall Status: " << RESET
              << colorFor(overall) << BOLD << toString(overall) << RESET
              << "\n";
}

void ReportGenerator::writeHtmlReport(
    const std::vector<DiagnosticResult>& results,
    const std::string& outputPath) const {
    fs::path p(outputPath);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }

    std::ofstream out(outputPath);
    if (!out.is_open()) {
        std::cerr << "Error: could not write HTML report to " << outputPath
                  << "\n";
        return;
    }

    const DiagnosticStatus overall = overallStatus(results);

    out << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
        << "<meta charset=\"utf-8\">\n"
        << "<meta name=\"viewport\" content=\"width=device-width, "
           "initial-scale=1\">\n"
        << "<title>Jetson Doctor Diagnostic Report</title>\n"
        << "<style>\n"
        << "  body { font-family: -apple-system, Segoe UI, Roboto, sans-serif; "
           "background:#0f1116; color:#e6e6e6; margin:0; padding:2rem; }\n"
        << "  .card { max-width:840px; margin:0 auto; background:#1a1d24; "
           "border-radius:12px; padding:2rem; box-shadow:0 8px 24px "
           "rgba(0,0,0,.4); }\n"
        << "  h1 { margin:0 0 .25rem; font-size:1.6rem; }\n"
        << "  .sub { color:#9aa0a6; font-size:.9rem; margin-bottom:1.5rem; }\n"
        << "  table { width:100%; border-collapse:collapse; }\n"
        << "  th, td { text-align:left; padding:.6rem .75rem; "
           "border-bottom:1px solid #2a2e37; }\n"
        << "  th { color:#9aa0a6; font-weight:600; font-size:.8rem; "
           "text-transform:uppercase; letter-spacing:.05em; }\n"
        << "  .badge { display:inline-block; padding:.15rem .6rem; "
           "border-radius:999px; color:#fff; font-weight:600; "
           "font-size:.8rem; }\n"
        << "  .overall { margin-top:1.5rem; font-size:1.15rem; "
           "font-weight:700; }\n"
        << "  .msg { color:#9aa0a6; font-size:.85rem; }\n"
        << "</style>\n</head>\n<body>\n<div class=\"card\">\n";

    out << "<h1>Jetson Doctor Diagnostic Report</h1>\n";
    out << "<div class=\"sub\">Generated " << htmlEscape(nowTimestamp())
        << "</div>\n";

    out << "<table>\n<thead><tr>"
        << "<th>Metric</th><th>Value</th><th>Status</th><th>Notes</th>"
        << "</tr></thead>\n<tbody>\n";

    for (const auto& r : results) {
        out << "<tr>"
            << "<td>" << htmlEscape(r.name) << "</td>"
            << "<td>" << htmlEscape(formatValue(r)) << "</td>"
            << "<td><span class=\"badge\" style=\"background:"
            << htmlColorFor(r.status) << "\">" << toString(r.status)
            << "</span></td>"
            << "<td class=\"msg\">" << htmlEscape(r.message) << "</td>"
            << "</tr>\n";
    }

    out << "</tbody>\n</table>\n";
    out << "<div class=\"overall\">Overall Status: "
        << "<span class=\"badge\" style=\"background:"
        << htmlColorFor(overall) << "\">" << toString(overall)
        << "</span></div>\n";
    out << "</div>\n</body>\n</html>\n";

    std::cout << "HTML report written to " << outputPath << "\n";
}

void ReportGenerator::writeJsonReport(
    const std::vector<DiagnosticResult>& results,
    const std::string& outputPath) const {
    fs::path p(outputPath);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }

    std::ofstream out(outputPath);
    if (!out.is_open()) {
        std::cerr << "Error: could not write JSON report to " << outputPath
                  << "\n";
        return;
    }

    const DiagnosticStatus overall = overallStatus(results);

    out << "{\n";
    out << "  \"project\": \"Jetson Doctor\",\n";
    out << "  \"timestamp\": \"" << jsonEscape(nowTimestamp()) << "\",\n";
    out << "  \"overall_status\": \"" << toString(overall) << "\",\n";
    out << "  \"results\": [\n";

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        out << "    {\n";
        out << "      \"name\": \"" << jsonEscape(r.name) << "\",\n";
        if (r.hasValue) {
            std::ostringstream v;
            v << std::fixed << std::setprecision(1) << r.value;
            out << "      \"value\": " << v.str() << ",\n";
            out << "      \"unit\": \"" << jsonEscape(r.unit) << "\",\n";
        } else {
            out << "      \"value\": "
                << (r.textValue.empty()
                        ? "null"
                        : "\"" + jsonEscape(r.textValue) + "\"")
                << ",\n";
        }
        out << "      \"status\": \"" << toString(r.status) << "\",\n";
        out << "      \"message\": \"" << jsonEscape(r.message) << "\"\n";
        out << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
    }

    out << "  ]\n";
    out << "}\n";

    std::cout << "JSON report written to " << outputPath << "\n";
}
