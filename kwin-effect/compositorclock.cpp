// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compositorclock.h"

#include <core/output.h>
#include <effect/effecthandler.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QThread>

#include <chrono>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

CompositorClock::CompositorClock(KWin::LogicalOutput* output)
    : m_output(output)
    // Seed `m_latestPresentTime` from `std::chrono::steady_clock::now()`
    // so `now()` returns a sensible timestamp BEFORE the first
    // `updatePresentTime` call. Leaving it at zero produces a
    // cross-output rebind hazard: a window migrating to a newly-added
    // output mid-animation calls `AnimatedValue::rebindClock(newClock)`,
    // which rebases `m_startTime` by
    // `newClock->now() - oldClock->now()`. With a zero-initialised
    // latch, new clock's `now()` = 0 while old clock's `now()` ≈ 10⁹ ns,
    // producing `delta ≈ -10⁹ ns` and shoving `m_startTime` deep into
    // the past. The next advance computes `elapsed ≫ duration` and the
    // animation snaps to completion instantly. Seeding from the same
    // `std::chrono::steady_clock` source that `updatePresentTime` feeds
    // (KWin's presentTime is steady_clock-backed) keeps the rebase
    // delta bounded by the freshness gap between the two clocks'
    // last observation — small and benign.
    //
    // The fallback clock never reads `m_latestPresentTime` (its `now()`
    // goes directly to `steady_clock`), so the initial value is unused
    // there — but seeding it keeps the two construction paths
    // symmetric and avoids a "why is the bound branch different?"
    // puzzle on future reads.
    , m_latestPresentTime(std::chrono::steady_clock::now().time_since_epoch())
    , m_wasBound(output != nullptr)
{
}

CompositorClock::~CompositorClock() = default;

namespace {

/// Shared main-thread contract check. `CompositorClock`'s state
/// (m_latestPresentTime, m_output's QPointer dispatch) is read and
/// written without synchronisation; every access must come from the
/// compositor thread. Kept as an inline free function rather than a
/// macro — gives a real symbol in stack traces, avoids
/// macro-pollution, and nothing about `Q_ASSERT`'s compile-out
/// behaviour changes (an empty-body inline is optimised away in
/// release builds). Every call site is one line (`assertMainThread();`),
/// matching the former macro's ergonomics.
inline void assertMainThread()
{
    Q_ASSERT(QCoreApplication::instance() == nullptr
             || QThread::currentThread() == QCoreApplication::instance()->thread());
}

} // namespace

std::chrono::nanoseconds CompositorClock::now() const
{
    assertMainThread();
    const auto wall = std::chrono::steady_clock::now().time_since_epoch();
    if (!m_wasBound) {
        return wall;
    }
    // Return the greater of the latched presentTime and the current wall
    // time. When the effect is inactive (no animations, no drag), KWin
    // does not call prePaintScreen, so m_latestPresentTime goes stale.
    // An animation started via a D-Bus signal (float/unfloat, rotate)
    // while the effect is inactive would latch m_startTime from the stale
    // clock value. The next prePaintScreen (triggered by requestFrame)
    // feeds a fresh presentTime, and advance() computes
    // elapsed = freshTime - staleStartTime >> duration, completing the
    // animation instantly. Returning max(latched, wall) ensures now()
    // never falls behind wall time, so the first advance() after an
    // idle period latches a current start time and progresses normally.
    return std::max(m_latestPresentTime, std::chrono::duration_cast<std::chrono::nanoseconds>(wall));
}

qreal CompositorClock::refreshRate() const
{
    assertMainThread();
    if (!m_output) {
        return 0.0;
    }
    // KWin's LogicalOutput::refreshRate() returns millihertz (1 Hz ×
    // 1000) for sub-Hz precision — a 59.94 Hz display reads 59940.
    // Convert to Hz for the IMotionClock contract, which is plain Hz.
    const uint32_t mHz = m_output->refreshRate();
    return qreal(mHz) / 1000.0;
}

void CompositorClock::requestFrame()
{
    // Main-thread only — same contract as `now()` / `updatePresentTime`.
    // `m_output` (QPointer) and `KWin::effects` are both main-thread-
    // bound state; a cross-thread caller mirroring the QtQuickClock
    // pattern of "requestFrame is thread-safe" would race against
    // onScreenRemoved tearing the output down. The assertion lives in
    // one spot so a future refactor that promotes `CompositorClock` to
    // cross-thread use can drop it from a single call site after
    // properly synchronising the underlying state.
    assertMainThread();
    if (!KWin::effects) {
        // Test or teardown path — nothing to ask for another frame from.
        return;
    }
    if (m_output) {
        // Targeted: ask only our output to tick. If KWin's effect API
        // gains an output-scoped schedule-repaint in the future this is
        // the drop-in point; today `addRepaint(QRect)` is the closest
        // per-output signal we have, and the output's geometry scopes
        // it.
        //
        // Zero-area guard: during hotplug (output attaching, DPMS off,
        // disabled virtual-screen) `geometry()` can legitimately be
        // empty, which collapses `addRepaint(QRect())` to a no-op.
        // Animations on that output would freeze silently until an
        // unrelated paint event wakes the compositor. Fall back to
        // `addRepaintFull()` so motion keeps ticking — slightly wasteful
        // (repaints everything, not just our scope) but motion-
        // correctness trumps damage efficiency on a transient edge.
        const QRect geo = m_output->geometry();
        if (geo.isEmpty()) {
            if (!m_loggedEmptyGeometry) {
                // One-shot debug. Hotplug / DPMS-off legitimately hold
                // the geometry at 0×0 for several frames; every
                // `requestFrame()` during that window would otherwise
                // debug-log at paint rate.
                qCDebug(lcEffect) << "CompositorClock: output geometry empty — falling back to addRepaintFull";
                m_loggedEmptyGeometry = true;
            }
            KWin::effects->addRepaintFull();
            return;
        }
        // Successful targeted repaint — reset the empty-geometry latch so
        // a subsequent hotplug cycle logs the first empty-frame again
        // rather than being silently suppressed.
        m_loggedEmptyGeometry = false;
        KWin::effects->addRepaint(geo);
        return;
    }
    if (m_wasBound && !m_loggedStaleOutput) {
        // Bound at construction but the QPointer has since gone null —
        // the LogicalOutput was destroyed before onScreenRemoved fired
        // its reap + clock cleanup. The full-screen repaint below is
        // correct (the per-output damage scope we'd prefer is no longer
        // available), just wasteful. One-shot debug so the sequence is
        // visible in diagnostics without flooding at paint rate.
        qCDebug(lcEffect) << "CompositorClock: bound output destroyed before reap — falling back to addRepaintFull";
        m_loggedStaleOutput = true;
    }
    // Unbound-by-construction fallback clock (single-output / test path)
    // or the stale-output path handled above.
    KWin::effects->addRepaintFull();
}

void CompositorClock::updatePresentTime(std::chrono::milliseconds presentTime, KWin::LogicalOutput* paintingOutput)
{
    // Contract: compositor (main) thread only — the `now()` read path
    // is unsynchronised, so every mutator must come from the same
    // thread as the readers. Every in-tree caller (prePaintScreen)
    // satisfies this; the assertion catches future drift cheaply.
    // Same assertion is applied on now() / refreshRate() via the
    // shared macro above.
    assertMainThread();

    // Per-output isolation cross-check. The effect routes presentTime
    // by output via `m_motionClocksByOutput.find(data.screen)` so each
    // bound clock only sees its own output's samples. Mis-plumbing
    // (a future refactor that iterates all clocks per prePaintScreen,
    // or a test harness feeding two clocks the same sample) would
    // silently latch the wrong output's presentTime and step
    // animations ahead of their own vsync — a correctness bug with
    // no user-visible failure mode below 10 ms per frame. Debug-only
    // assertion catches the mis-plumbing at its source; release
    // builds are unaffected. `paintingOutput == nullptr` is the "no
    // cross-check" opt-out (default arg) used by tests driving a
    // bound clock without a real output; we skip validation in that
    // case.
    Q_ASSERT_X(!paintingOutput || paintingOutput == m_output, "CompositorClock::updatePresentTime",
               "presentTime routed to the wrong clock — this clock is bound to a different output");

    if (!m_wasBound) {
        // Fallback clock self-drives from steady_clock in now(); ignore
        // per-output presentTime pushes. The effect still calls this
        // unconditionally today, but the call is a no-op for the
        // fallback so N-output paint cadence cannot double-advance it.
        return;
    }

    const auto asNs = std::chrono::duration_cast<std::chrono::nanoseconds>(presentTime);
    // Monotonicity latch — KWin's presentTime is normally monotonic
    // (std::chrono::steady_clock-backed) but output hotplug / DPMS can
    // rarely reset it. The IMotionClock contract mandates non-
    // decreasing `now()`; downstream AnimatedValue<T> derives `dt`
    // from (now() - lastNow), and a negative dt would step the curve
    // backwards. Clamp here instead of propagating the reset.
    if (asNs > m_latestPresentTime) {
        m_latestPresentTime = asNs;
    }
}

KWin::LogicalOutput* CompositorClock::output() const
{
    return m_output.data();
}

const void* CompositorClock::epochIdentity() const
{
    // KWin's presentTime is sourced from std::chrono::steady_clock, so
    // this clock is rebind-compatible with any other steady_clock-backed
    // IMotionClock (notably QtQuickClock for shells that drive both
    // compositor- and QML-side animations on the same AnimatedValue).
    return IMotionClock::steadyClockEpoch();
}

} // namespace PlasmaZones
