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
 * methods (including `now()`) are const but read the state written by
 * `updatePresentTime` without synchronization — the single-threaded
 * contract makes that safe.
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
     */
    void updatePresentTime(std::chrono::milliseconds presentTime);

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
};

} // namespace PlasmaZones
