// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/IMotionClock.h>

#include <QPointer>

#include <chrono>

namespace KWin {
class LogicalOutput;
}

namespace PlasmaZones {

/**
 * @brief KWin adapter implementing `PhosphorAnimation::IMotionClock`.
 *
 * Each instance is bound to one `KWin::LogicalOutput`, so `refreshRate()`
 * and `requestFrame()` answer for that output rather than for the session.
 * The effect holds one `CompositorClock` per output (maintained via
 * `screenAdded` / `screenRemoved` signals) plus a fallback unbound clock
 * for the bootstrap-before-screens-populate and null-screen() migration
 * windows.
 *
 * The class was originally built to phase-lock each output to its own
 * presentTime, so a mixed-refresh session (60 Hz + 144 Hz) would not beat.
 * That never worked. `now()` returned max(latched presentTime, wall), and
 * the only value ever fed to the latch was the effect's own
 * `duration_cast<milliseconds>(steady_clock::now())`, which truncates, so
 * the latched value was never above the wall time it was sampled at and the
 * max always selected wall. KWin 6.7 then dropped the predicted presentTime
 * the design needed, and the latch was retired rather than kept as cover
 * for an API that has moved away. Every clock reads wall time, and on an
 * N-output desktop advanceAnimations steps every in-flight animation with a
 * real dt N times per vsync. Correctness is unaffected (parametric curves
 * read elapsed/duration off wall time, and Spring::step composes exactly
 * across sub-steps); the per-output pacing is simply not a thing this class
 * does. What is still per-output is the refresh rate and the repaint scope.
 *
 * ## Thread contract
 *
 * `now()` is a plain `steady_clock` read, but `refreshRate()` and
 * `requestFrame()` dereference the `QPointer<LogicalOutput>`, which is not
 * cross-thread safe, and `requestFrame()` also touches `KWin::effects` and
 * the debug-log rate-limit latches. All `IMotionClock` methods must
 * therefore be called from the compositor thread. This diverges from the
 * `QtQuickClock` sibling (which explicitly supports cross-thread `now()` /
 * `requestFrame()` via atomics + Qt's thread-safe `update()`); consumers
 * holding an `IMotionClock*` polymorphically must either treat the pointer
 * as main-thread-bound OR know which concrete class they got. The
 * base-class `IMotionClock::now()` doc lists per-implementation
 * thread-safety stories for exactly this reason.
 *
 * ## Monotonicity
 *
 * The `IMotionClock` contract mandates a non-decreasing `now()`, because
 * `AnimatedValue<T>` downstream derives `dt` from (now() - lastNow) and a
 * negative dt would step the curve backwards. `std::chrono::steady_clock`
 * supplies that property directly, so nothing here has to clamp for it.
 *
 * ## Output lifetime
 *
 * The clock holds a `QPointer<KWin::LogicalOutput>` — a bound output
 * that is destroyed (e.g., monitor disconnect) leaves the clock
 * nominally alive but with `refreshRate() == 0` and a no-op
 * `requestFrame()`. The effect is expected to destroy the
 * `CompositorClock` instance at that point; the QPointer guards
 * against a single tick racing past the disconnect notification.
 */
class CompositorClock final : public PhosphorAnimation::IMotionClock
{
public:
    /**
     * @brief Construct a clock bound to @p output.
     *
     * @p output may be nullptr for the bootstrap and migration windows
     * where the effect has no screen to bind to yet, and for tests. In
     * that mode `refreshRate()` returns 0 and `requestFrame()` falls
     * through to `KWin::effects->addRepaintFull()` instead of scoping
     * the repaint to an output's geometry. `now()` behaves identically
     * either way.
     */
    explicit CompositorClock(KWin::LogicalOutput* output = nullptr);
    ~CompositorClock() override;

    // IMotionClock
    std::chrono::nanoseconds now() const override;
    qreal refreshRate() const override;
    void requestFrame() override;
    const void* epochIdentity() const override;

    /// The output this clock is bound to. May be null.
    KWin::LogicalOutput* output() const;

private:
    QPointer<KWin::LogicalOutput> m_output;
    // `true` if the clock was constructed bound to a non-null output
    // (per-output instance), `false` for the always-unbound fallback
    // clock. Used by `requestFrame()` to distinguish "stale output
    // destroyed before onScreenRemoved" (rare, worth a debug log) from
    // "unbound by design" (normal, silent).
    const bool m_wasBound;
    // Rate-limit: set once a stale-output debug log fires so a misbehaving
    // compositor sequence doesn't flood the log at paint rate. Written
    // from requestFrame() which is non-const; no `mutable` needed.
    bool m_loggedStaleOutput = false;
    // Rate-limit: set once a zero-geometry addRepaintFull fallback fires.
    // Output hotplug / DPMS-off legitimately produces empty geometry
    // for a few frames; without the flag every animation tick on an
    // affected output would debug-log through the fallback branch.
    bool m_loggedEmptyGeometry = false;
};

} // namespace PlasmaZones
