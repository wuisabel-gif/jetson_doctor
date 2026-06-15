#include "system_monitor.hpp"

#include <sys/statvfs.h>  // statvfs() for disk usage
#include <unistd.h>       // gethostname()

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// Small file helpers. All of these are defensive: a missing or unreadable file
// is a normal condition (non-Jetson hardware, restricted permissions, etc.).
// ---------------------------------------------------------------------------

// Read the entire contents of a file into a string. Returns false on failure.
bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Read a single line / number from a file (typical for sysfs entries).
bool readFirstLine(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::getline(f, out);
    return true;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// Classify a temperature reading (Celsius) using the shared thermal thresholds.
DiagnosticStatus classifyTemp(double celsius) {
    if (celsius < 75.0) return DiagnosticStatus::PASS;
    if (celsius <= 85.0) return DiagnosticStatus::WARN;
    return DiagnosticStatus::FAIL;
}

}  // namespace

// ---------------------------------------------------------------------------
// Thermal zones
//
// Linux exposes temperature sensors under /sys/class/thermal/thermal_zoneN/.
// Each zone has:
//   - type : a label such as "CPU-therm", "GPU-therm", "x86_pkg_temp"
//   - temp : temperature in milli-Celsius (e.g. 58200 == 58.2 C)
// We scan all zones and pick the first whose type matches a keyword, so the
// tool works across the many different Jetson and x86 naming conventions.
// ---------------------------------------------------------------------------
double SystemMonitor::readThermalZone(const std::vector<std::string>& keywords,
                                      int fallbackZone) const {
    const std::string base = "/sys/class/thermal";

    auto readZoneTemp = [](const std::string& zonePath) -> double {
        std::string raw;
        if (!readFirstLine(zonePath + "/temp", raw)) return std::nan("");
        try {
            return std::stod(trim(raw)) / 1000.0;  // milli-C -> C
        } catch (...) {
            return std::nan("");
        }
    };

    // First pass: match a zone whose `type` contains one of the keywords.
    if (fs::exists(base)) {
        for (const auto& entry : fs::directory_iterator(base)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("thermal_zone", 0) != 0) continue;

            std::string type;
            if (!readFirstLine(entry.path().string() + "/type", type)) continue;
            const std::string lowerType = toLower(trim(type));

            for (const auto& kw : keywords) {
                if (lowerType.find(toLower(kw)) != std::string::npos) {
                    const double t = readZoneTemp(entry.path().string());
                    if (!std::isnan(t)) return t;
                }
            }
        }
    }

    // Fallback: a specific zone index (useful when types are unlabeled).
    const std::string fallbackPath =
        base + "/thermal_zone" + std::to_string(fallbackZone);
    if (fs::exists(fallbackPath)) {
        return readZoneTemp(fallbackPath);
    }

    return std::nan("");
}

DiagnosticResult SystemMonitor::checkCpuTemperature() {
    DiagnosticResult r;
    r.name = "CPU Temperature";
    r.unit = "C";

    const double t = readThermalZone({"cpu", "x86_pkg", "coretemp", "soc"}, 0);
    if (std::isnan(t)) {
        r.status = DiagnosticStatus::UNKNOWN;
        r.message = "CPU thermal sensor not available on this system.";
        return r;
    }

    r.value = t;
    r.hasValue = true;
    r.status = classifyTemp(t);
    switch (r.status) {
        case DiagnosticStatus::PASS:
            r.message = "CPU temperature is within normal range."; break;
        case DiagnosticStatus::WARN:
            r.message = "CPU temperature is elevated."; break;
        default:
            r.message = "CPU temperature exceeds safe limits."; break;
    }
    return r;
}

DiagnosticResult SystemMonitor::checkGpuTemperature() {
    DiagnosticResult r;
    r.name = "GPU Temperature";
    r.unit = "C";

    const double t = readThermalZone({"gpu", "gpu-therm"}, -1);
    if (std::isnan(t)) {
        r.status = DiagnosticStatus::UNKNOWN;
        r.message = "GPU thermal sensor not available on this system.";
        return r;
    }

    r.value = t;
    r.hasValue = true;
    r.status = classifyTemp(t);
    switch (r.status) {
        case DiagnosticStatus::PASS:
            r.message = "GPU temperature is within normal range."; break;
        case DiagnosticStatus::WARN:
            r.message = "GPU temperature is elevated."; break;
        default:
            r.message = "GPU temperature exceeds safe limits."; break;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Memory usage
//
// /proc/meminfo reports memory in kB. We use MemAvailable (the kernel's own
// estimate of reclaimable memory) against MemTotal, which is far more accurate
// than MemFree alone.
// ---------------------------------------------------------------------------
DiagnosticResult SystemMonitor::checkMemoryUsage() {
    DiagnosticResult r;
    r.name = "Memory Usage";
    r.unit = "%";

    std::string contents;
    if (!readFile("/proc/meminfo", contents)) {
        r.status = DiagnosticStatus::UNKNOWN;
        r.message = "/proc/meminfo not available on this system.";
        return r;
    }

    long memTotal = -1, memAvailable = -1;
    std::istringstream iss(contents);
    std::string key;
    long valueKb;
    std::string unit;
    while (iss >> key >> valueKb >> unit) {
        if (key == "MemTotal:")     memTotal = valueKb;
        else if (key == "MemAvailable:") memAvailable = valueKb;
    }

    if (memTotal <= 0 || memAvailable < 0) {
        r.status = DiagnosticStatus::UNKNOWN;
        r.message = "Could not parse memory information.";
        return r;
    }

    const double used = static_cast<double>(memTotal - memAvailable);
    const double pct = (used / static_cast<double>(memTotal)) * 100.0;

    r.value = pct;
    r.hasValue = true;
    if (pct < 75.0) {
        r.status = DiagnosticStatus::PASS;
        r.message = "Memory usage is within normal range.";
    } else if (pct <= 90.0) {
        r.status = DiagnosticStatus::WARN;
        r.message = "Memory usage is high.";
    } else {
        r.status = DiagnosticStatus::FAIL;
        r.message = "Memory usage is critically high.";
    }
    return r;
}

// ---------------------------------------------------------------------------
// Disk usage
//
// statvfs() on the root filesystem gives block counts; we compute used space as
// (total - available-to-unprivileged-users) so the figure matches `df`.
// ---------------------------------------------------------------------------
DiagnosticResult SystemMonitor::checkDiskUsage() {
    DiagnosticResult r;
    r.name = "Disk Usage";
    r.unit = "%";

    struct statvfs st;
    if (statvfs("/", &st) != 0) {
        r.status = DiagnosticStatus::UNKNOWN;
        r.message = "Could not read filesystem statistics.";
        return r;
    }

    const double total = static_cast<double>(st.f_blocks) * st.f_frsize;
    const double avail = static_cast<double>(st.f_bavail) * st.f_frsize;
    if (total <= 0) {
        r.status = DiagnosticStatus::UNKNOWN;
        r.message = "Filesystem reported zero capacity.";
        return r;
    }

    const double used = total - avail;
    const double pct = (used / total) * 100.0;

    r.value = pct;
    r.hasValue = true;
    if (pct < 80.0) {
        r.status = DiagnosticStatus::PASS;
        r.message = "Disk usage is within normal range.";
    } else if (pct <= 90.0) {
        r.status = DiagnosticStatus::WARN;
        r.message = "Disk usage is above recommended threshold.";
    } else {
        r.status = DiagnosticStatus::FAIL;
        r.message = "Disk usage is critically high.";
    }
    return r;
}

// ---------------------------------------------------------------------------
// CPU frequency
//
// /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq holds the current
// frequency in kHz. We report the highest current frequency across cores.
// This is informational (no FAIL state) but we flag a stuck-low clock as WARN.
// ---------------------------------------------------------------------------
DiagnosticResult SystemMonitor::checkCpuFrequency() {
    DiagnosticResult r;
    r.name = "CPU Frequency";
    r.unit = "GHz";

    const std::string base = "/sys/devices/system/cpu";
    double maxKhz = -1.0;

    if (fs::exists(base)) {
        for (const auto& entry : fs::directory_iterator(base)) {
            const std::string name = entry.path().filename().string();
            // Match cpu0, cpu1, ... but not "cpufreq" or "cpuidle".
            if (name.rfind("cpu", 0) != 0 || name.size() < 4 ||
                !std::isdigit(static_cast<unsigned char>(name[3]))) {
                continue;
            }
            std::string raw;
            const std::string path =
                entry.path().string() + "/cpufreq/scaling_cur_freq";
            if (!readFirstLine(path, raw)) continue;
            try {
                maxKhz = std::max(maxKhz, std::stod(trim(raw)));
            } catch (...) {
                // ignore unparseable cores
            }
        }
    }

    if (maxKhz < 0) {
        r.status = DiagnosticStatus::UNKNOWN;
        r.message = "CPU frequency scaling info not available.";
        return r;
    }

    const double ghz = maxKhz / 1e6;  // kHz -> GHz
    r.value = ghz;
    r.hasValue = true;
    if (ghz >= 0.6) {
        r.status = DiagnosticStatus::PASS;
        r.message = "CPU frequency reported successfully.";
    } else {
        r.status = DiagnosticStatus::WARN;
        r.message = "CPU clock appears unusually low.";
    }
    return r;
}

// ---------------------------------------------------------------------------
// System information (informational rows, no health evaluation)
// ---------------------------------------------------------------------------
std::vector<DiagnosticResult> SystemMonitor::collectSystemInfo() {
    std::vector<DiagnosticResult> info;

    auto makeInfo = [](const std::string& name, const std::string& value) {
        DiagnosticResult r;
        r.name = name;
        r.textValue = value;
        r.status = DiagnosticStatus::PASS;
        r.message = "Informational.";
        return r;
    };

    // Hostname.
    {
        char host[256] = {0};
        if (gethostname(host, sizeof(host) - 1) == 0) {
            info.push_back(makeInfo("Hostname", host));
        }
    }

    // OS name from /etc/os-release (PRETTY_NAME=...).
    {
        std::string contents;
        if (readFile("/etc/os-release", contents)) {
            std::istringstream iss(contents);
            std::string line;
            while (std::getline(iss, line)) {
                if (line.rfind("PRETTY_NAME=", 0) == 0) {
                    std::string v = line.substr(std::string("PRETTY_NAME=").size());
                    // strip surrounding quotes
                    if (!v.empty() && v.front() == '"') v.erase(0, 1);
                    if (!v.empty() && v.back() == '"') v.pop_back();
                    info.push_back(makeInfo("Operating System", v));
                    break;
                }
            }
        }
    }

    // Kernel version from /proc/version (first three tokens).
    {
        std::string contents;
        if (readFirstLine("/proc/version", contents)) {
            std::istringstream iss(contents);
            std::string a, b, c;
            iss >> a >> b >> c;  // e.g. "Linux version 5.10.x"
            info.push_back(makeInfo("Kernel", trim(a + " " + b + " " + c)));
        }
    }

    // Uptime from /proc/uptime (first field = seconds).
    {
        std::string contents;
        if (readFirstLine("/proc/uptime", contents)) {
            try {
                const double seconds = std::stod(trim(contents.substr(
                    0, contents.find(' '))));
                const long s = static_cast<long>(seconds);
                const long days = s / 86400;
                const long hours = (s % 86400) / 3600;
                const long mins = (s % 3600) / 60;
                std::ostringstream up;
                if (days > 0) up << days << "d ";
                up << hours << "h " << mins << "m";
                info.push_back(makeInfo("Uptime", up.str()));
            } catch (...) {
                // ignore
            }
        }
    }

    // Jetson detection.
    info.push_back(makeInfo(
        "Platform", isJetson() ? "NVIDIA Jetson (Tegra) detected"
                               : "Generic Linux (non-Jetson)"));

    return info;
}

bool SystemMonitor::isJetson() const {
    // Jetson L4T images ship this release file; its presence is a reliable
    // marker even when no Tegra sensors are exposed.
    return fs::exists("/etc/nv_tegra_release") ||
           fs::exists("/sys/module/tegra_fuse");
}

std::vector<DiagnosticResult> SystemMonitor::runAll() {
    std::vector<DiagnosticResult> results;
    results.push_back(checkCpuTemperature());
    results.push_back(checkGpuTemperature());
    results.push_back(checkMemoryUsage());
    results.push_back(checkDiskUsage());
    results.push_back(checkCpuFrequency());

    auto info = collectSystemInfo();
    results.insert(results.end(), info.begin(), info.end());
    return results;
}
