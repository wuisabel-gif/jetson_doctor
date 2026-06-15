#include "report_generator.hpp"

#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

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

// HTML hex color for each status, tuned for legibility on a dark window
// surface (GitHub Primer dark palette — proven contrast against #0e1116).
const char* htmlColorFor(DiagnosticStatus status) {
    switch (status) {
        case DiagnosticStatus::PASS:    return "#56d364";  // green
        case DiagnosticStatus::WARN:    return "#e3b341";  // amber
        case DiagnosticStatus::FAIL:    return "#f85149";  // red
        case DiagnosticStatus::UNKNOWN: return "#768390";  // gray
    }
    return "#768390";
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

// A small inline SVG glyph for a metric, chosen by name keyword. Strokes use
// currentColor so the icon inherits the row's status color. Self-contained so
// the report needs no icon font or network access.
std::string iconFor(const std::string& name) {
    std::string n;
    for (char c : name) n += static_cast<char>(std::tolower(c));

    const char* head =
        "<svg viewBox=\"0 0 24 24\" width=\"18\" height=\"18\" fill=\"none\" "
        "stroke=\"currentColor\" stroke-width=\"1.6\" stroke-linecap=\"round\" "
        "stroke-linejoin=\"round\">";
    const char* tail = "</svg>";

    std::string body;
    if (n.find("temp") != std::string::npos) {
        // thermometer
        body = "<path d=\"M10 13.5V5a2 2 0 1 1 4 0v8.5a4 4 0 1 1-4 0Z\"/>"
               "<path d=\"M12 16v-5\"/>";
    } else if (n.find("memory") != std::string::npos) {
        // RAM stick
        body = "<rect x=\"3\" y=\"7\" width=\"18\" height=\"9\" rx=\"1\"/>"
               "<path d=\"M7 16v2M11 16v2M15 16v2M19 16v2M8 11h2M14 11h2\"/>";
    } else if (n.find("disk") != std::string::npos) {
        // disk / storage cylinder
        body = "<ellipse cx=\"12\" cy=\"6\" rx=\"8\" ry=\"3\"/>"
               "<path d=\"M4 6v12c0 1.7 3.6 3 8 3s8-1.3 8-3V6\"/>"
               "<path d=\"M4 12c0 1.7 3.6 3 8 3s8-1.3 8-3\"/>";
    } else if (n.find("frequency") != std::string::npos ||
               n.find("cpu") != std::string::npos) {
        // cpu / chip
        body = "<rect x=\"7\" y=\"7\" width=\"10\" height=\"10\" rx=\"1\"/>"
               "<path d=\"M10 10h4v4h-4z\"/>"
               "<path d=\"M9 3v2M15 3v2M9 19v2M15 19v2M3 9h2M3 15h2M19 9h2"
               "M19 15h2\"/>";
    } else {
        // generic sensor / dot
        body = "<circle cx=\"12\" cy=\"12\" r=\"8\"/>"
               "<circle cx=\"12\" cy=\"12\" r=\"2\"/>";
    }
    return std::string(head) + body + tail;
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

    // Split sensor checks (anything with a unit, even when UNKNOWN) from the
    // informational rows. This keeps an unavailable sensor on the telemetry
    // list rather than hiding it in the system-info panel.
    std::vector<const DiagnosticResult*> metrics, infos;
    int nPass = 0, nWarn = 0, nFail = 0, nUnknown = 0;
    std::string host = "localhost", platform = "Linux device";
    bool jetson = false;
    for (const auto& r : results) {
        if (!r.unit.empty()) metrics.push_back(&r);
        else infos.push_back(&r);
        switch (r.status) {
            case DiagnosticStatus::PASS:    ++nPass; break;
            case DiagnosticStatus::WARN:    ++nWarn; break;
            case DiagnosticStatus::FAIL:    ++nFail; break;
            case DiagnosticStatus::UNKNOWN: ++nUnknown; break;
        }
        if (r.name == "Hostname" && !r.textValue.empty()) host = r.textValue;
        if (r.name == "Platform" && !r.textValue.empty()) {
            jetson = r.textValue.find("Jetson") != std::string::npos;
            platform = jetson ? "NVIDIA Jetson \xC2\xB7 Tegra" : "Generic Linux";
        }
    }

    const std::string ts = nowTimestamp();
    const std::string clockInit = ts.size() >= 8 ? ts.substr(11, 8) : ts;
    const char* oc = htmlColorFor(overall);

    // Overall one-line verdict for the sidebar.
    std::string verdict;
    const int attention = nWarn + nFail;
    if (overall == DiagnosticStatus::PASS) {
        verdict = "All systems nominal. " + std::to_string(nPass) +
                  " checks passed.";
    } else {
        verdict = std::to_string(attention) +
                  (attention == 1 ? " check needs attention."
                                  : " checks need attention.");
    }
    if (nUnknown > 0) {
        verdict += " " + std::to_string(nUnknown) + " sensor" +
                   (nUnknown == 1 ? "" : "s") + " unavailable.";
    }

    out << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
        << "<meta charset=\"utf-8\">\n"
        << "<meta name=\"viewport\" content=\"width=device-width, "
           "initial-scale=1\">\n"
        << "<title>Jetson Doctor — System Diagnostics</title>\n";

    out << R"CSS(<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
  --desktop:#06080c;--win:#0e1116;--chrome:#161b22;--side:#10141b;
  --panel:#151a21;--inset:#0a0d12;--line:#232a35;--line-2:#1b212b;
  --ink:#e9edf3;--mut:#9aa6b6;--dim:#69727f;--accent:#76b900;--r:11px;
}
html{color-scheme:dark}
body{
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Inter,sans-serif;
  background:var(--desktop);
  background-image:radial-gradient(circle at 1px 1px,rgba(255,255,255,.025) 1px,transparent 0);
  background-size:23px 23px;
  color:var(--ink);min-height:100vh;display:flex;flex-direction:column;
  align-items:center;justify-content:flex-start;padding:max(4vh,26px) 18px;
  -webkit-font-smoothing:antialiased;
}
.mono{font-family:ui-monospace,"SF Mono",SFMono-Regular,"JetBrains Mono",Menlo,Consolas,monospace;font-variant-numeric:tabular-nums}
.win{
  width:100%;max-width:940px;background:var(--win);
  border:1px solid rgba(255,255,255,.07);border-radius:var(--r);overflow:hidden;
  box-shadow:0 1px 0 rgba(255,255,255,.06) inset,0 42px 90px -28px rgba(0,0,0,.85),0 14px 32px -14px rgba(0,0,0,.6);
  animation:winIn .5s cubic-bezier(.2,.8,.2,1) both;
}
@keyframes winIn{from{opacity:0;transform:translateY(12px) scale(.985)}to{opacity:1;transform:none}}
.titlebar{display:flex;align-items:center;gap:14px;height:42px;padding:0 14px;
  background:linear-gradient(#1c222c,#161b22);border-bottom:1px solid var(--line)}
.lights{display:flex;gap:8px}
.lt{width:12px;height:12px;border-radius:50%;border:1px solid rgba(0,0,0,.35)}
.lt.r{background:#ff5f57}.lt.y{background:#febc2e}.lt.g{background:#28c840}
.wtitle{flex:1;text-align:center;font-size:13px;font-weight:500;color:var(--mut)}
.clock{display:flex;align-items:center;gap:7px;font-size:12.5px;color:var(--mut)}
.clock .live{width:7px;height:7px;border-radius:50%;background:var(--accent);box-shadow:0 0 8px var(--accent)}
.toolbar{display:flex;align-items:center;justify-content:space-between;gap:12px;
  padding:10px 16px;background:var(--chrome);border-bottom:1px solid var(--line)}
.device{display:flex;align-items:center;gap:11px}
.device .chip{display:flex;width:32px;height:32px;align-items:center;justify-content:center;
  border-radius:8px;color:var(--accent);background:color-mix(in srgb,var(--accent) 13%,transparent);
  border:1px solid color-mix(in srgb,var(--accent) 28%,transparent)}
.device .dn{font-size:13.5px;font-weight:500;line-height:1.25}
.device .dh{font-size:11.5px;color:var(--dim)}
.run{display:flex;align-items:center;gap:7px;font-size:11px;color:var(--mut);
  text-transform:uppercase;letter-spacing:.09em}
.run .pulse{width:7px;height:7px;border-radius:50%;background:var(--accent);animation:pulse 1.9s ease-in-out infinite}
@keyframes pulse{0%,100%{opacity:1;box-shadow:0 0 0 0 color-mix(in srgb,var(--accent) 55%,transparent)}50%{opacity:.45;box-shadow:0 0 0 6px transparent}}
.main{display:grid;grid-template-columns:250px 1fr}
.side{background:var(--side);border-right:1px solid var(--line);padding:18px 16px;
  display:flex;flex-direction:column;gap:16px}
.ov{--c:#768390;border:1px solid color-mix(in srgb,var(--c) 38%,transparent);
  background:color-mix(in srgb,var(--c) 9%,transparent);border-radius:10px;padding:14px 15px}
.ov .lab{font-size:10.5px;letter-spacing:.12em;text-transform:uppercase;color:var(--mut)}
.ov .big{display:flex;align-items:center;gap:11px;margin-top:9px}
.ov .led{width:13px;height:13px;border-radius:50%;background:var(--c);
  box-shadow:0 0 13px color-mix(in srgb,var(--c) 80%,transparent)}
.ov .st{font-size:25px;font-weight:500;color:var(--c)}
.ov .desc{margin-top:8px;font-size:12px;color:var(--mut);line-height:1.5}
.legend{display:flex;flex-direction:column;gap:1px}
.lg{display:flex;align-items:center;justify-content:space-between;padding:7px 9px;border-radius:7px}
.lg .k{display:flex;align-items:center;gap:9px;font-size:12.5px;color:var(--mut)}
.lg .k i{width:8px;height:8px;border-radius:2px;display:block}
.lg .v{font-size:13px;font-weight:500;color:var(--ink)}
.skv{margin-top:auto;display:flex;flex-direction:column;gap:9px;
  border-top:1px solid var(--line-2);padding-top:14px}
.skv div{display:flex;justify-content:space-between;gap:12px;font-size:12px}
.skv .k{color:var(--dim)}.skv .v{color:var(--mut);text-align:right;word-break:break-word}
.content{padding:18px 20px 10px}
.sect{display:flex;align-items:center;gap:11px;font-size:10.5px;letter-spacing:.13em;
  text-transform:uppercase;color:var(--dim);margin:4px 2px 13px}
.sect::after{content:"";flex:1;height:1px;background:var(--line-2)}
.metrics{display:flex;flex-direction:column;margin-bottom:24px}
.m{--c:#768390;display:grid;
  grid-template-columns:34px minmax(120px,1fr) 150px 92px 78px;
  align-items:center;gap:14px;padding:11px 10px;border-radius:8px;transition:background .15s}
.m:not(:first-child){border-top:1px solid var(--line-2)}
.m:hover{background:rgba(255,255,255,.025)}
.mi{display:flex;align-items:center;justify-content:center;width:34px;height:34px;
  border-radius:8px;color:var(--c);background:color-mix(in srgb,var(--c) 12%,transparent)}
.mn{display:flex;flex-direction:column;gap:2px;min-width:0}
.mt{font-size:14px;font-weight:500;color:var(--ink)}
.md{font-size:12px;color:var(--mut);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.mbar{height:6px;border-radius:999px;background:var(--inset);overflow:hidden;border:1px solid var(--line-2)}
.mfill{display:block;height:100%;border-radius:999px;background:var(--c);
  transition:width .9s cubic-bezier(.2,.8,.2,1)}
.mv{justify-self:end;display:flex;align-items:baseline;gap:3px}
.mv .num{font-size:18px;font-weight:500;color:var(--ink)}
.mv .un{font-size:11.5px;color:var(--dim)}
.mv .na{font-size:16px;color:var(--dim)}
.pill{justify-self:end;font-size:10.5px;font-weight:500;letter-spacing:.05em;
  padding:3px 9px;border-radius:6px;color:var(--c);
  background:color-mix(in srgb,var(--c) 14%,transparent);
  border:1px solid color-mix(in srgb,var(--c) 30%,transparent)}
.info{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:1px;
  background:var(--line-2);border:1px solid var(--line-2);border-radius:10px;
  overflow:hidden;margin-bottom:18px}
.iv{background:var(--panel);padding:12px 14px;display:flex;flex-direction:column;gap:4px}
.iv .k{font-size:10.5px;letter-spacing:.07em;text-transform:uppercase;color:var(--dim)}
.iv .v{font-size:13.5px;color:var(--ink);font-weight:500;word-break:break-word}
.statusbar{display:flex;align-items:center;justify-content:space-between;gap:12px;
  flex-wrap:wrap;padding:9px 16px;background:var(--chrome);border-top:1px solid var(--line);
  font-size:11.5px;color:var(--mut)}
.sb-o{--c:#768390;display:flex;align-items:center;gap:8px;font-weight:500}
.sb-o .led{width:9px;height:9px;border-radius:50%;background:var(--c);
  box-shadow:0 0 8px color-mix(in srgb,var(--c) 80%,transparent)}
.sb-mid{color:var(--dim)}
@media (max-width:720px){
  .main{grid-template-columns:1fr}
  .side{border-right:none;border-bottom:1px solid var(--line)}
  .skv{margin-top:6px}
  .m{grid-template-columns:34px 1fr auto;gap:10px 12px;
    grid-template-areas:"i n p" "i bar bar" "i v v"}
  .mi{grid-area:i}.mn{grid-area:n}.mbar{grid-area:bar}
  .mv{grid-area:v;justify-self:start}.pill{grid-area:p}
}
@media (prefers-reduced-motion:reduce){
  .win{animation:none}.mfill{transition:none}.run .pulse{animation:none}
}
</style>
)CSS";

    out << "</head>\n<body>\n"
        << "<div class=\"win\" role=\"application\" "
           "aria-label=\"Jetson Doctor system diagnostics\">\n";

    // --- Title bar: window controls, title, live clock ---
    out << "<div class=\"titlebar\">\n"
        << "  <div class=\"lights\"><span class=\"lt r\"></span>"
           "<span class=\"lt y\"></span><span class=\"lt g\"></span></div>\n"
        << "  <div class=\"wtitle\">Jetson Doctor — System Diagnostics</div>\n"
        << "  <div class=\"clock\"><span class=\"live\"></span>"
           "<span class=\"mono\" id=\"clock\">" << htmlEscape(clockInit)
        << "</span></div>\n</div>\n";

    // --- Toolbar: device identity + running indicator ---
    out << "<div class=\"toolbar\">\n"
        << "  <div class=\"device\"><span class=\"chip\">" << iconFor("cpu")
        << "</span><div><div class=\"dn\">" << htmlEscape(host)
        << "</div><div class=\"dh\">" << htmlEscape(platform)
        << "</div></div></div>\n"
        << "  <div class=\"run\"><span class=\"pulse\"></span>monitoring</div>\n"
        << "</div>\n";

    // --- Body: sidebar + content ---
    out << "<div class=\"main\">\n";

    // Sidebar
    out << "<aside class=\"side\">\n"
        << "  <div class=\"ov\" style=\"--c:" << oc << "\">\n"
        << "    <div class=\"lab\">Overall status</div>\n"
        << "    <div class=\"big\"><span class=\"led\"></span>"
           "<span class=\"st\">" << toString(overall) << "</span></div>\n"
        << "    <div class=\"desc\">" << htmlEscape(verdict) << "</div>\n"
        << "  </div>\n";

    out << "  <div class=\"legend\">\n";
    auto legendRow = [&](const char* label, int n, DiagnosticStatus s) {
        out << "    <div class=\"lg\"><span class=\"k\"><i style=\"background:"
            << htmlColorFor(s) << "\"></i>" << label
            << "</span><span class=\"v mono\">" << n << "</span></div>\n";
    };
    legendRow("Pass", nPass, DiagnosticStatus::PASS);
    legendRow("Warn", nWarn, DiagnosticStatus::WARN);
    legendRow("Fail", nFail, DiagnosticStatus::FAIL);
    legendRow("Unknown", nUnknown, DiagnosticStatus::UNKNOWN);
    out << "  </div>\n";

    out << "  <div class=\"skv\">\n"
        << "    <div><span class=\"k\">Host</span><span class=\"v\">"
        << htmlEscape(host) << "</span></div>\n"
        << "    <div><span class=\"k\">Platform</span><span class=\"v\">"
        << htmlEscape(platform) << "</span></div>\n"
        << "  </div>\n</aside>\n";

    // Content
    out << "<section class=\"content\">\n"
        << "  <div class=\"sect\">Sensors &amp; telemetry</div>\n"
        << "  <div class=\"metrics\">\n";

    for (const auto* r : metrics) {
        const char* c = htmlColorFor(r->status);
        double fill = 0.0;
        if (r->hasValue && r->status != DiagnosticStatus::UNKNOWN) {
            fill = (r->value / gaugeMax(r->unit)) * 100.0;
            if (fill < 0) fill = 0;
            if (fill > 100) fill = 100;
        }
        std::ostringstream fillStr;
        fillStr << std::fixed << std::setprecision(1) << fill;

        out << "    <div class=\"m\" style=\"--c:" << c << "\">\n"
            << "      <span class=\"mi\">" << iconFor(r->name) << "</span>\n"
            << "      <div class=\"mn\"><span class=\"mt\">"
            << htmlEscape(r->name) << "</span><span class=\"md\">"
            << htmlEscape(r->message) << "</span></div>\n"
            << "      <div class=\"mbar\"><span class=\"mfill\" data-fill=\""
            << fillStr.str() << "\" style=\"width:" << fillStr.str()
            << "%\"></span></div>\n";

        if (r->hasValue) {
            std::ostringstream v;
            v << std::fixed << std::setprecision(1) << r->value;
            out << "      <div class=\"mv\"><span class=\"num mono\">" << v.str()
                << "</span><span class=\"un\">" << htmlEscape(r->unit)
                << "</span></div>\n";
        } else {
            out << "      <div class=\"mv\"><span class=\"na mono\">—</span>"
                   "</div>\n";
        }

        out << "      <span class=\"pill\">" << toString(r->status)
            << "</span>\n    </div>\n";
    }
    out << "  </div>\n";

    // System information grid
    if (!infos.empty()) {
        out << "  <div class=\"sect\">System information</div>\n"
            << "  <div class=\"info\">\n";
        for (const auto* r : infos) {
            out << "    <div class=\"iv\"><span class=\"k\">"
                << htmlEscape(r->name) << "</span><span class=\"v\">"
                << htmlEscape(formatValue(*r)) << "</span></div>\n";
        }
        out << "  </div>\n";
    }
    out << "</section>\n</div>\n";  // close content + main

    // --- Status bar ---
    out << "<div class=\"statusbar\">\n"
        << "  <span class=\"sb-o\" style=\"--c:" << oc
        << "\"><span class=\"led\"></span>overall: " << toString(overall)
        << "</span>\n"
        << "  <span class=\"sb-mid mono\">" << nPass << " pass \xC2\xB7 "
        << nWarn << " warn \xC2\xB7 " << nFail << " fail \xC2\xB7 " << nUnknown
        << " unknown</span>\n"
        << "  <span>" << htmlEscape(ts) << " \xC2\xB7 jetson_doctor v1.0</span>\n"
        << "</div>\n</div>\n";  // close statusbar + win

    // --- Footer (below the window, on the desktop backdrop) ---
    out << R"FOOT(<footer style="width:100%;max-width:940px;margin:14px auto 0;padding:0 4px;display:flex;flex-wrap:wrap;gap:6px 16px;justify-content:space-between;align-items:center;font-size:12px;color:#69727f;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif">
  <span>&copy; 2026 wuisabel-gif &middot; All rights reserved</span>
  <a href="https://github.com/wuisabel-gif" target="_blank" rel="noopener noreferrer" style="color:#76b900;text-decoration:none">github.com/wuisabel-gif</a>
</footer>
)FOOT";

    out << R"JS(<script>
(function(){
  var c=document.getElementById('clock');
  function tick(){var d=new Date();c.textContent=d.toLocaleTimeString([],{hour12:false});}
  if(c){tick();setInterval(tick,1000);}
  var reduce=window.matchMedia&&window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  if(!reduce){
    document.querySelectorAll('.mfill').forEach(function(f){
      var target=f.getAttribute('data-fill');
      f.style.width='0%';
      requestAnimationFrame(function(){requestAnimationFrame(function(){f.style.width=target+'%';});});
    });
  }
})();
</script>
)JS";

    out << "</body>\n</html>\n";

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
