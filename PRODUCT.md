# Product

## Register

product

## Users

Platform / embedded engineers doing NVIDIA Jetson hardware bring-up, validation, and
failure triage. They run `jetson_doctor` on a board (often headless, over SSH) and open
the generated HTML report on a second monitor in a lab, alongside terminals, to confirm
at a glance whether the board is healthy.

## Product Purpose

A C++17 Linux hardware diagnostic that reads thermal, memory, disk, and compute telemetry
from `sysfs`/`procfs` and reports PASS / WARN / FAIL health states. The HTML report is the
human-facing surface: it must make overall health and any out-of-range metric obvious in
under two seconds, while staying credible to an engineer who lives in real system tools.

## Brand Personality

Instrument-grade, honest, unfussy. Three words: precise, native, telemetric. It should
feel like a real piece of system software — an Activity Monitor / btop for Jetson — not a
marketing dashboard. NVIDIA green is the brand anchor.

## Anti-references

- Generic dark-SaaS analytics dashboards (gradient hero metric, rounded pastel cards).
- Consumer "health app" styling with playful illustrations.
- Anything that looks like a web marketing page rather than a desktop utility.

## Design Principles

- **Read like an instrument.** Values are monospace and aligned; status is unmistakable.
- **Earned familiarity.** Borrow the vocabulary of real OS system monitors; don't invent
  affordances.
- **Honest telemetry.** Missing sensors are shown as UNKNOWN, never faked or hidden.
- **Glanceable first, detailed second.** Overall verdict dominates; per-metric detail
  supports it.

## Accessibility & Inclusion

Target WCAG 2.1 AA: body text ≥4.5:1, status never encoded by color alone (always paired
with a text label). Full `prefers-reduced-motion` support — meters and window entrance
degrade to static. Dark theme tuned for low-glare lab/bench viewing.
