#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "report_generator.hpp"
#include "system_monitor.hpp"

namespace {

void printHelp() {
    std::cout <<
        "Jetson Doctor — C++ Linux hardware diagnostic monitor\n\n"
        "Usage:\n"
        "  jetson_doctor [options]\n\n"
        "Options:\n"
        "  (no args)      Print a terminal diagnostic report (default)\n"
        "  --html         Generate reports/latest_report.html\n"
        "  --json         Generate logs/latest_report.json\n"
        "  --all          Print terminal report and generate HTML + JSON\n"
        "  --watch <sec>  Continuously refresh the terminal report\n"
        "  --demo [name]  Use sample data instead of live sensors. Scenarios:\n"
        "                 healthy | warning | critical | nonjetson\n"
        "                 (default: warning) — for docs / screenshots\n"
        "  --help         Show this help message\n\n"
        "Reads thermal, memory, disk, and CPU data from sysfs/procfs and\n"
        "reports PASS / WARN / FAIL health states. Runs on any Linux machine;\n"
        "unavailable sensors are reported as UNKNOWN.\n";
}

const std::string kHtmlPath = "reports/latest_report.html";
const std::string kJsonPath = "logs/latest_report.json";

// Representative results used by --demo so the docs / screenshots show a
// populated report on any machine (the live tool reports UNKNOWN for sensors a
// given host doesn't expose). Several named scenarios let a viewer compare how
// the report looks across the health spectrum:
//   healthy   - everything nominal (overall PASS)
//   warning   - one metric elevated (overall WARN)
//   critical  - thermal / resource crisis with throttling (overall FAIL)
//   nonjetson - generic Linux box with no thermal sensors (overall UNKNOWN)
std::vector<DiagnosticResult> buildDemoResults(const std::string& scenario) {
    auto metric = [](const std::string& name, double value,
                     const std::string& unit, DiagnosticStatus status,
                     const std::string& msg) {
        DiagnosticResult r;
        r.name = name;
        r.value = value;
        r.unit = unit;
        r.status = status;
        r.message = msg;
        r.hasValue = true;
        return r;
    };
    auto unknownMetric = [](const std::string& name, const std::string& unit,
                            const std::string& msg) {
        DiagnosticResult r;
        r.name = name;
        r.unit = unit;
        r.status = DiagnosticStatus::UNKNOWN;
        r.message = msg;
        r.hasValue = false;
        return r;
    };
    auto info = [](const std::string& name, const std::string& text) {
        DiagnosticResult r;
        r.name = name;
        r.textValue = text;
        r.status = DiagnosticStatus::PASS;
        r.message = "Informational.";
        return r;
    };
    auto addInfo = [&](std::vector<DiagnosticResult>& v, const char* host,
                       const char* os, const char* kernel, const char* uptime,
                       const char* platform) {
        v.push_back(info("Hostname", host));
        v.push_back(info("Operating System", os));
        v.push_back(info("Kernel", kernel));
        v.push_back(info("Uptime", uptime));
        v.push_back(info("Platform", platform));
    };

    using S = DiagnosticStatus;
    std::vector<DiagnosticResult> v;

    if (scenario == "healthy" || scenario == "pass") {
        v.push_back(metric("CPU Temperature", 46.5, "C", S::PASS,
                           "CPU temperature is within normal range."));
        v.push_back(metric("GPU Temperature", 49.1, "C", S::PASS,
                           "GPU temperature is within normal range."));
        v.push_back(metric("Memory Usage", 34.0, "%", S::PASS,
                           "Memory usage is within normal range."));
        v.push_back(metric("Disk Usage", 58.0, "%", S::PASS,
                           "Disk usage is within normal range."));
        v.push_back(metric("CPU Frequency", 1.90, "GHz", S::PASS,
                           "CPU frequency reported successfully."));
        addInfo(v, "jetson-orin", "Ubuntu 22.04.3 LTS",
                "Linux version 5.10.120-tegra", "12d 4h 30m",
                "NVIDIA Jetson (Tegra) detected");
        return v;
    }

    if (scenario == "critical" || scenario == "fail") {
        v.push_back(metric("CPU Temperature", 88.4, "C", S::FAIL,
                           "CPU temperature exceeds safe limits."));
        v.push_back(metric("GPU Temperature", 91.2, "C", S::FAIL,
                           "GPU temperature exceeds safe limits."));
        v.push_back(metric("Memory Usage", 94.6, "%", S::FAIL,
                           "Memory usage is critically high."));
        v.push_back(metric("Disk Usage", 96.0, "%", S::FAIL,
                           "Disk usage is critically high."));
        v.push_back(metric("CPU Frequency", 0.42, "GHz", S::WARN,
                           "CPU clock appears unusually low (thermal throttling)."));
        addInfo(v, "jetson-nano", "Ubuntu 20.04.6 LTS",
                "Linux version 4.9.299-tegra", "0d 1h 03m",
                "NVIDIA Jetson (Tegra) detected");
        return v;
    }

    if (scenario == "nonjetson" || scenario == "degraded" ||
        scenario == "unknown") {
        v.push_back(unknownMetric("CPU Temperature", "C",
                                  "CPU thermal sensor not available on this system."));
        v.push_back(unknownMetric("GPU Temperature", "C",
                                  "GPU thermal sensor not available on this system."));
        v.push_back(metric("Memory Usage", 51.0, "%", S::PASS,
                           "Memory usage is within normal range."));
        v.push_back(metric("Disk Usage", 72.0, "%", S::PASS,
                           "Disk usage is within normal range."));
        v.push_back(unknownMetric("CPU Frequency", "GHz",
                                  "CPU frequency scaling info not available."));
        addInfo(v, "build-server", "Debian GNU/Linux 12 (bookworm)",
                "Linux version 6.1.0-21-amd64", "27d 9h 41m",
                "Generic Linux (non-Jetson)");
        return v;
    }

    // Default: "warning" — one elevated metric, overall WARN.
    v.push_back(metric("CPU Temperature", 58.2, "C", S::PASS,
                       "CPU temperature is within normal range."));
    v.push_back(metric("GPU Temperature", 61.7, "C", S::PASS,
                       "GPU temperature is within normal range."));
    v.push_back(metric("Memory Usage", 43.0, "%", S::PASS,
                       "Memory usage is within normal range."));
    v.push_back(metric("Disk Usage", 84.0, "%", S::WARN,
                       "Disk usage is above recommended threshold."));
    v.push_back(metric("CPU Frequency", 1.43, "GHz", S::PASS,
                       "CPU frequency reported successfully."));
    addInfo(v, "jetson-orin", "Ubuntu 22.04.3 LTS",
            "Linux version 5.10.120-tegra", "3d 7h 12m",
            "NVIDIA Jetson (Tegra) detected");
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    bool wantHtml = false;
    bool wantJson = false;
    bool wantTerminal = true;
    bool demo = false;
    std::string demoScenario = "warning";
    int watchSeconds = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        } else if (arg == "--html") {
            wantHtml = true;
            wantTerminal = false;
        } else if (arg == "--json") {
            wantJson = true;
            wantTerminal = false;
        } else if (arg == "--all") {
            wantHtml = true;
            wantJson = true;
            wantTerminal = true;
        } else if (arg == "--watch") {
            if (i + 1 < argc) {
                try {
                    watchSeconds = std::stoi(argv[++i]);
                } catch (...) {
                    std::cerr << "Invalid interval for --watch.\n";
                    return 1;
                }
            } else {
                std::cerr << "--watch requires a number of seconds.\n";
                return 1;
            }
        } else if (arg == "--demo") {
            demo = true;
            // Optional scenario name as the next argument (not another flag).
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                demoScenario = argv[++i];
            }
        } else {
            std::cerr << "Unknown option: " << arg << "\n\n";
            printHelp();
            return 1;
        }
    }

    SystemMonitor monitor;
    ReportGenerator reporter;

    // Continuous monitoring mode: redraw the terminal report on an interval
    // until interrupted (Ctrl-C).
    if (watchSeconds > 0) {
        while (true) {
            std::cout << "\033[2J\033[H";  // clear screen, cursor home
            auto results = demo ? buildDemoResults(demoScenario) : monitor.runAll();
            reporter.printTerminalReport(results);
            std::cout << "\n(refreshing every " << watchSeconds
                      << "s — press Ctrl-C to stop)\n";
            std::this_thread::sleep_for(std::chrono::seconds(watchSeconds));
        }
    }

    auto results = demo ? buildDemoResults(demoScenario) : monitor.runAll();

    if (wantTerminal) {
        reporter.printTerminalReport(results);
    }
    if (wantHtml) {
        reporter.writeHtmlReport(results, kHtmlPath);
    }
    if (wantJson) {
        reporter.writeJsonReport(results, kJsonPath);
    }

    // Exit code mirrors the overall health so the tool is CI-friendly:
    //   0 = PASS / UNKNOWN, 1 = WARN, 2 = FAIL
    switch (ReportGenerator::overallStatus(results)) {
        case DiagnosticStatus::FAIL: return 2;
        case DiagnosticStatus::WARN: return 1;
        default:                     return 0;
    }
}
