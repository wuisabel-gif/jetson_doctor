#pragma once

#include <string>
#include <vector>

#include "diagnostic_result.hpp"

// SystemMonitor collects platform health metrics by reading Linux kernel
// interfaces (sysfs / procfs) and a few standard system calls. Every check
// returns a DiagnosticResult and never throws: if a source is missing the
// result is reported as UNKNOWN so the tool runs on any Linux machine,
// Jetson or not.
class SystemMonitor {
public:
    DiagnosticResult checkCpuTemperature();
    DiagnosticResult checkGpuTemperature();
    DiagnosticResult checkMemoryUsage();
    DiagnosticResult checkDiskUsage();
    DiagnosticResult checkCpuFrequency();

    // Informational rows (no PASS/WARN/FAIL evaluation): hostname, kernel,
    // OS name, uptime. Returned as a group for convenience.
    std::vector<DiagnosticResult> collectSystemInfo();

    // Convenience: run every check and return all results in display order.
    std::vector<DiagnosticResult> runAll();

    // True if this looks like an NVIDIA Jetson (Tegra) platform.
    bool isJetson() const;

private:
    // Reads the first thermal zone whose `type` matches one of `keywords`
    // (case-insensitive). Falls back to `fallbackZone` when no name matches.
    // Returns temperature in Celsius, or NaN if nothing could be read.
    double readThermalZone(const std::vector<std::string>& keywords,
                           int fallbackZone) const;
};
