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

// Full-scale value for a gauge bar, chosen per unit so the fill is meaningful:
// percentages run 0–100, temperatures 0–100 C, clock speed 0–3 GHz.
double gaugeMax(const std::string& unit) {
    if (unit == "%") return 100.0;
    if (unit == "C") return 100.0;
    if (unit == "GHz") return 3.0;
    return 100.0;
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
        << "<title>Jetson Doctor — Diagnostic Dashboard</title>\n"
        << "<style>\n"
        << "  :root { --bg:#0d0f14; --panel:#161a22; --panel2:#1d2230;\n"
        << "          --line:#2a2f3c; --text:#e8eaed; --muted:#9aa0a6;\n"
        << "          --pass:#2e7d32; --warn:#ef6c00; --fail:#c62828;\n"
        << "          --unknown:#5f6671; --accent:#76b900; }\n"  // NVIDIA green
        << "  * { box-sizing:border-box; }\n"
        << "  body { font-family:-apple-system,Segoe UI,Roboto,Helvetica,"
           "sans-serif; background:var(--bg); color:var(--text); margin:0;\n"
        << "         padding:2rem 1.25rem; }\n"
        << "  .wrap { max-width:980px; margin:0 auto; }\n"
        << "  header { display:flex; flex-wrap:wrap; align-items:center;\n"
        << "           justify-content:space-between; gap:1rem; margin-bottom:1.5rem; }\n"
        << "  .title { display:flex; align-items:center; gap:.7rem; }\n"
        << "  .dot { width:.7rem; height:.7rem; border-radius:50%;\n"
        << "         background:var(--accent); box-shadow:0 0 12px var(--accent); }\n"
        << "  h1 { margin:0; font-size:1.45rem; letter-spacing:-.01em; }\n"
        << "  .sub { color:var(--muted); font-size:.85rem; margin-top:.15rem; }\n"
        << "  .overall-banner { display:flex; align-items:center; gap:.6rem;\n"
        << "         padding:.6rem 1.1rem; border-radius:12px; font-weight:700;\n"
        << "         font-size:1rem; color:#fff; }\n"
        << "  .tiles { display:grid; grid-template-columns:repeat(4,1fr);\n"
        << "         gap:.75rem; margin-bottom:1.5rem; }\n"
        << "  .tile { background:var(--panel); border:1px solid var(--line);\n"
        << "         border-radius:12px; padding:.9rem 1rem; }\n"
        << "  .tile .n { font-size:1.6rem; font-weight:700; }\n"
        << "  .tile .l { color:var(--muted); font-size:.75rem;\n"
        << "         text-transform:uppercase; letter-spacing:.06em; }\n"
        << "  .grid { display:grid; grid-template-columns:repeat(2,1fr);\n"
        << "         gap:.85rem; margin-bottom:1.5rem; }\n"
        << "  .metric { background:var(--panel); border:1px solid var(--line);\n"
        << "         border-left:4px solid var(--unknown); border-radius:12px;\n"
        << "         padding:1rem 1.1rem; }\n"
        << "  .metric .row { display:flex; justify-content:space-between;\n"
        << "         align-items:baseline; }\n"
        << "  .metric .name { color:var(--muted); font-size:.85rem; }\n"
        << "  .metric .val { font-size:1.5rem; font-weight:700; }\n"
        << "  .metric .val .u { font-size:.9rem; color:var(--muted);\n"
        << "         font-weight:500; margin-left:.15rem; }\n"
        << "  .bar { height:7px; background:#0c0f15; border-radius:999px;\n"
        << "         overflow:hidden; margin:.7rem 0 .55rem; }\n"
        << "  .bar > span { display:block; height:100%; border-radius:999px; }\n"
        << "  .msg { color:var(--muted); font-size:.8rem; }\n"
        << "  .badge { display:inline-block; padding:.12rem .55rem;\n"
        << "         border-radius:999px; color:#fff; font-weight:700;\n"
        << "         font-size:.72rem; letter-spacing:.03em; }\n"
        << "  .panel { background:var(--panel); border:1px solid var(--line);\n"
        << "         border-radius:12px; padding:1.1rem 1.3rem; }\n"
        << "  .panel h2 { margin:0 0 .8rem; font-size:.8rem; color:var(--muted);\n"
        << "         text-transform:uppercase; letter-spacing:.06em; }\n"
        << "  .info { display:grid; grid-template-columns:repeat(2,1fr);\n"
        << "         gap:.5rem 1.5rem; }\n"
        << "  .info .k { color:var(--muted); font-size:.85rem; }\n"
        << "  .info .v { font-size:.9rem; font-weight:600; word-break:break-word; }\n"
        << "  footer { color:var(--muted); font-size:.75rem; text-align:center;\n"
        << "         margin-top:1.5rem; }\n"
        << "  @media (max-width:640px){ .tiles{grid-template-columns:repeat(2,1fr);}\n"
        << "         .grid,.info{grid-template-columns:1fr;} }\n"
        << "</style>\n</head>\n<body>\n<div class=\"wrap\">\n";

    // Split numeric (gauge) metrics from informational rows.
    std::vector<const DiagnosticResult*> metrics, infos;
    int nPass = 0, nWarn = 0, nFail = 0, nUnknown = 0;
    for (const auto& r : results) {
        if (r.hasValue) metrics.push_back(&r);
        else infos.push_back(&r);
        switch (r.status) {
            case DiagnosticStatus::PASS:    ++nPass; break;
            case DiagnosticStatus::WARN:    ++nWarn; break;
            case DiagnosticStatus::FAIL:    ++nFail; break;
            case DiagnosticStatus::UNKNOWN: ++nUnknown; break;
        }
    }

    // --- Header with overall-status banner ---
    out << "<header>\n"
        << "  <div class=\"title\"><span class=\"dot\"></span><div>\n"
        << "    <h1>Jetson Doctor</h1>\n"
        << "    <div class=\"sub\">Diagnostic Dashboard &middot; generated "
        << htmlEscape(nowTimestamp()) << "</div>\n"
        << "  </div></div>\n"
        << "  <div class=\"overall-banner\" style=\"background:"
        << htmlColorFor(overall) << "\">Overall: " << toString(overall)
        << "</div>\n</header>\n";

    // --- Summary tiles ---
    out << "<div class=\"tiles\">\n";
    auto tile = [&](int n, const char* label, const char* color) {
        out << "  <div class=\"tile\"><div class=\"n\" style=\"color:" << color
            << "\">" << n << "</div><div class=\"l\">" << label
            << "</div></div>\n";
    };
    tile(nPass, "Pass", "var(--pass)");
    tile(nWarn, "Warn", "var(--warn)");
    tile(nFail, "Fail", "var(--fail)");
    tile(nUnknown, "Unknown", "var(--unknown)");
    out << "</div>\n";

    // --- Metric gauge cards ---
    out << "<div class=\"grid\">\n";
    for (const auto* r : metrics) {
        const char* color = htmlColorFor(r->status);
        double fill = 0.0;
        if (r->status != DiagnosticStatus::UNKNOWN) {
            fill = (r->value / gaugeMax(r->unit)) * 100.0;
            if (fill < 0) fill = 0;
            if (fill > 100) fill = 100;
        }
        std::ostringstream valNum;
        valNum << std::fixed << std::setprecision(1) << r->value;

        out << "  <div class=\"metric\" style=\"border-left-color:" << color
            << "\">\n"
            << "    <div class=\"row\"><span class=\"name\">"
            << htmlEscape(r->name) << "</span>"
            << "<span class=\"badge\" style=\"background:" << color << "\">"
            << toString(r->status) << "</span></div>\n"
            << "    <div class=\"val\">" << valNum.str()
            << "<span class=\"u\">" << htmlEscape(r->unit) << "</span></div>\n"
            << "    <div class=\"bar\"><span style=\"width:" << fill
            << "%;background:" << color << "\"></span></div>\n"
            << "    <div class=\"msg\">" << htmlEscape(r->message) << "</div>\n"
            << "  </div>\n";
    }
    out << "</div>\n";

    // --- System information panel ---
    if (!infos.empty()) {
        out << "<div class=\"panel\">\n<h2>System Information</h2>\n"
            << "<div class=\"info\">\n";
        for (const auto* r : infos) {
            out << "  <div class=\"k\">" << htmlEscape(r->name) << "</div>"
                << "<div class=\"v\">" << htmlEscape(formatValue(*r))
                << "</div>\n";
        }
        out << "</div>\n</div>\n";
    }

    out << "<footer>Jetson Doctor &middot; C++17 Linux hardware diagnostics "
           "&middot; PASS / WARN / FAIL</footer>\n";
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
