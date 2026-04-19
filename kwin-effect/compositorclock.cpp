// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compositorclock.h"

#include <core/output.h>
#include <effect/effecthandler.h>

#include <QCoreApplication>
#include <QThread>

namespace PlasmaZones {

CompositorClock::CompositorClock(KWin::LogicalOutput* output)
    : m_output(output)
{
}

CompositorClock::~CompositorClock() = default;

std::chrono::nanoseconds CompositorClock::now() const
{
    return m_latestPresentTime;
}

qreal CompositorClock::refreshRate() const
{
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
    } else {
        // Unbound clock — full-screen tick. Degenerate single-output
        // and test paths land here.
        KWin::effects->addRepaintFull();
    }
}

void CompositorClock::updatePresentTime(std::chrono::milliseconds presentTime)
{
    // Contract: compositor (main) thread only — the `now()` read path
    // is unsynchronised, so every mutator must come from the same
    // thread as the readers. Every in-tree caller (prePaintScreen)
    // satisfies this; the assertion catches future drift cheaply.
    Q_ASSERT(QCoreApplication::instance() == nullptr
             || QThread::currentThread() == QCoreApplication::instance()->thread());

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

} // namespace PlasmaZones
