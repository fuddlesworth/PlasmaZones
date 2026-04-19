// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compositorclock.h"

#include <core/output.h>
#include <effect/effecthandler.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QThread>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

CompositorClock::CompositorClock(KWin::LogicalOutput* output)
    : m_output(output)
    , m_wasBound(output != nullptr)
{
}

CompositorClock::~CompositorClock() = default;

// Shared main-thread contract assertion. CompositorClock's state
// (m_latestPresentTime, m_output's QPointer dispatch) is read and
// written without synchronization; every access must come from the
// compositor thread. The assertion is a debug-only check — cheap to
// keep in production builds (no-op) and invaluable for catching
// future drift during testing. See updatePresentTime's contract
// comment for the full rationale.
#define PLASMAZONES_COMPOSITORCLOCK_ASSERT_MAIN_THREAD()                                                               \
    Q_ASSERT(QCoreApplication::instance() == nullptr                                                                   \
             || QThread::currentThread() == QCoreApplication::instance()->thread())

std::chrono::nanoseconds CompositorClock::now() const
{
    PLASMAZONES_COMPOSITORCLOCK_ASSERT_MAIN_THREAD();
    return m_latestPresentTime;
}

qreal CompositorClock::refreshRate() const
{
    PLASMAZONES_COMPOSITORCLOCK_ASSERT_MAIN_THREAD();
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
        KWin::effects->addRepaint(m_output->geometry());
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

void CompositorClock::updatePresentTime(std::chrono::milliseconds presentTime)
{
    // Contract: compositor (main) thread only — the `now()` read path
    // is unsynchronised, so every mutator must come from the same
    // thread as the readers. Every in-tree caller (prePaintScreen)
    // satisfies this; the assertion catches future drift cheaply.
    // Same assertion is applied on now() / refreshRate() via the
    // shared macro above.
    PLASMAZONES_COMPOSITORCLOCK_ASSERT_MAIN_THREAD();

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
