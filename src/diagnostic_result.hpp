#pragma once

#include <string>

#include "diagnostic_status.hpp"

// One row of the diagnostic report: the outcome of a single metric check.
//
// `hasValue` distinguishes numeric metrics (CPU temp, disk usage, ...) from
// informational rows (hostname, kernel version) that carry text only. When a
// metric could not be read, status is UNKNOWN and `hasValue` is false.
struct DiagnosticResult {
    std::string name;          // e.g. "CPU Temperature"
    double value = 0.0;        // numeric reading (valid only if hasValue)
    std::string unit;          // e.g. "C", "%", "GHz"
    std::string textValue;     // for informational rows (hostname, kernel, ...)
    DiagnosticStatus status = DiagnosticStatus::UNKNOWN;
    std::string message;       // short explanation of the status
    bool hasValue = false;     // true if `value`/`unit` hold a real number
};
