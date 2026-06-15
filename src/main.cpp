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
        "  --demo         Use representative sample data (for docs/screenshots)\n"
        "  --help         Show this help message\n\n"
        "Reads thermal, memory, disk, and CPU data from sysfs/procfs and\n"
        "reports PASS / WARN / FAIL health states. Runs on any Linux machine;\n"
        "unavailable sensors are reported as UNKNOWN.\n";
}

const std::string kHtmlPath = "reports/latest_report.html";
const std::string kJsonPath = "logs/latest_report.json";

// Representative results for a healthy Jetson under light load. Used by --demo
// so the documentation / screenshots show a populated report on any machine
// (the live tool reports UNKNOWN for sensors a given host doesn't expose).
std::vector<DiagnosticResult> buildDemoResults() {
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
    auto info = [](const std::string& name, const std::string& text) {
        DiagnosticResult r;
        r.name = name;
        r.textValue = text;
        r.status = DiagnosticStatus::PASS;
        r.message = "Informational.";
        return r;
    };

    return {
        metric("CPU Temperature", 58.2, "C", DiagnosticStatus::PASS,
               "CPU temperature is within normal range."),
        metric("GPU Temperature", 61.7, "C", DiagnosticStatus::PASS,
               "GPU temperature is within normal range."),
        metric("Memory Usage", 43.0, "%", DiagnosticStatus::PASS,
               "Memory usage is within normal range."),
        metric("Disk Usage", 84.0, "%", DiagnosticStatus::WARN,
               "Disk usage is above recommended threshold."),
        metric("CPU Frequency", 1.43, "GHz", DiagnosticStatus::PASS,
               "CPU frequency reported successfully."),
        info("Hostname", "jetson-orin"),
        info("Operating System", "Ubuntu 22.04.3 LTS"),
        info("Kernel", "Linux version 5.10.120-tegra"),
        info("Uptime", "3d 7h 12m"),
        info("Platform", "NVIDIA Jetson (Tegra) detected"),
    };
}

}  // namespace

int main(int argc, char** argv) {
    bool wantHtml = false;
    bool wantJson = false;
    bool wantTerminal = true;
    bool demo = false;
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
            auto results = demo ? buildDemoResults() : monitor.runAll();
            reporter.printTerminalReport(results);
            std::cout << "\n(refreshing every " << watchSeconds
                      << "s — press Ctrl-C to stop)\n";
            std::this_thread::sleep_for(std::chrono::seconds(watchSeconds));
        }
    }

    auto results = demo ? buildDemoResults() : monitor.runAll();

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
