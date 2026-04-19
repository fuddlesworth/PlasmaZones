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
 * Each instance is bound to one `KWin::LogicalOutput` so a multi-monitor
 * session with mixed refresh rates (60 Hz + 144 Hz being the common case
 * we need to handle correctly) can phase-lock per-output without
 * cross-output beating. The effect holds one `CompositorClock` per
 * output (maintained via `screenAdded` / `screenRemoved` signals) plus a
 * fallback unbound clock for the bootstrap-before-screens-populate and
 * null-screen() migration windows. Each `prePaintScreen` feeds the
 * presentTime to the clock matching `data.screen`; animations bound to
 * that output tick on that clock, animations bound to other outputs
 * step with dt=0 (correct — they tick when their own output paints).
 *
 * ## Driver contract
 *
 * The effect (or test harness) is the *driver* — it owns the clock and
 * pushes presentTime via `updatePresentTime()` once per paint cycle.
 * `now()` returns the last pushed value, upcast to nanoseconds. This is
 * not a wall-clock; the reference epoch is whatever KWin uses for
 * presentTime (`std::chrono::steady_clock` on current KDE). Consumers
 * only use *differences* between consecutive readings, so epoch choice
 * is irrelevant.
 *
 * `updatePresentTime` is the only method that mutates clock state, and
 * it MUST be called from the compositor thread. All `IMotionClock`
 * methods (`now()`, `refreshRate()`, `requestFrame()`) must ALSO be
 * called from the compositor thread — they read the state written by
 * `updatePresentTime` without synchronization, and `requestFrame()`
 * additionally dereferences the `QPointer<LogicalOutput>` which is not
 * cross-thread safe. This diverges from the `QtQuickClock` sibling
 * (which explicitly supports cross-thread `now()` / `requestFrame()`
 * via atomics + Qt's thread-safe `update()`); consumers holding an
 * `IMotionClock*` polymorphically must therefore either treat the
 * pointer as main-thread-bound OR know which concrete class they got.
 * The base-class `IMotionClock::now()` doc lists per-implementation
 * thread-safety stories for exactly this reason.
 *
 * ## Monotonicity
 *
 * KWin's presentTime is sourced from `std::chrono::steady_clock` and
 * is monotonic in normal operation, but output hotplug / DPMS cycles
 * can (rarely) reset it. `updatePresentTime` latches the maximum seen
 * so `now()` stays monotonic regardless. The `IMotionClock` contract
 * mandates this; don't remove it.
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
     * @p output may be nullptr for the degenerate single-output /
     * test case where per-output phase-locking isn't required. In
     * that mode the clock self-drives from `std::chrono::steady_clock`
     * (see `now()`), `refreshRate()` returns 0, `requestFrame()` falls
     * through to `KWin::effects->addRepaintFull()`, and
     * `updatePresentTime()` is a no-op — the fallback ignores per-output
     * paint cadence to avoid N× stepping on N-output systems where
     * `prePaintScreen` fires once per output per vsync.
     */
    explicit CompositorClock(KWin::LogicalOutput* output = nullptr);
    ~CompositorClock() override;

    // IMotionClock
    std::chrono::nanoseconds now() const override;
    qreal refreshRate() const override;
    void requestFrame() override;
    const void* epochIdentity() const override;

    /**
     * @brief Push the next presentTime sample from the compositor.
     *
     * Call once per paint cycle from the effect's `prePaintScreen`
     * with the `presentTime` parameter KWin provides. The clock
     * upconverts to nanoseconds and latches the maximum so `now()`
     * stays monotonic even if the underlying source regresses.
     *
     * @param paintingOutput The output whose `prePaintScreen` is firing.
     *     Passed by the caller so the clock can assert in debug builds
     *     that it is receiving presentTime only for the output it was
     *     constructed against. Mis-plumbing (feeding a 60 Hz output's
     *     presentTime into a 144 Hz clock) would silently latch the
     *     faster output's timestamps into the slower clock, stepping
     *     its animations ahead of its own vsync — a correctness bug
     *     that neither test harness nor runtime paint cycle exercises
     *     unless this cross-check catches it. Release builds drop the
     *     assertion; the argument is otherwise unused, and `nullptr`
     *     is accepted (the fallback clock path is an intentional
     *     skip). Defaulted so existing direct callers (tests driving
     *     a bound CompositorClock without a real output) compile
     *     unchanged.
     */
    void updatePresentTime(std::chrono::milliseconds presentTime, KWin::LogicalOutput* paintingOutput = nullptr);

    /// The output this clock is bound to. May be null.
    KWin::LogicalOutput* output() const;

private:
    QPointer<KWin::LogicalOutput> m_output;
    std::chrono::nanoseconds m_latestPresentTime{0};
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
