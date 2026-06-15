#pragma once

#include <string>

// Health state for a single diagnostic check.
//
//   PASS    - metric is within the normal operating range
//   WARN    - metric is elevated but not yet critical
//   FAIL    - metric is outside safe limits, needs attention
//   UNKNOWN - the sensor / source could not be read on this machine
//
// UNKNOWN is important: Jetson Doctor is meant to run on any Linux box, so a
// missing sensor file is a normal, non-fatal condition rather than an error.
enum class DiagnosticStatus {
    PASS,
    WARN,
    FAIL,
    UNKNOWN
};

// Human-readable label, e.g. for terminal / JSON / HTML output.
inline std::string toString(DiagnosticStatus status) {
    switch (status) {
        case DiagnosticStatus::PASS:    return "PASS";
        case DiagnosticStatus::WARN:    return "WARN";
        case DiagnosticStatus::FAIL:    return "FAIL";
        case DiagnosticStatus::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

// Severity ranking used when computing the overall status: the worst (highest)
// status among all checks wins. UNKNOWN is treated as less severe than WARN so
// a machine missing a GPU sensor is not reported as unhealthy.
inline int severity(DiagnosticStatus status) {
    switch (status) {
        case DiagnosticStatus::PASS:    return 0;
        case DiagnosticStatus::UNKNOWN: return 1;
        case DiagnosticStatus::WARN:    return 2;
        case DiagnosticStatus::FAIL:    return 3;
    }
    return 1;
}
