// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <QTimer>

namespace PhosphorScrollEngine {

// The close-settle reflow hold: windowClosed (engine_lifecycle.cpp) starts a
// hold instead of reflowing immediately, so the closing window's disappear
// animation plays over an unchanged strip; windowFocused defers its viewport
// scroll through the same hold. The three helpers live in their own TU
// because engine_lifecycle.cpp is over the file-size ceiling and this feature
// is a self-contained state machine (m_closeReflowHoldUntil /
// m_closeReflowFlushScheduled / m_closeReflowClock, all declared together in
// ScrollEngine.h).
//
// THREE arms defer, and the third is what makes the other two work.
// scheduleRetileForScreen's queued apply (engine_core.cpp) consults the hold
// because the engine's own signal fan-out goes around it: windowClosed emits
// placementChanged from its own last line, the daemon wires that signal
// synchronously to its tiled-count gate, and a close ALWAYS moves the count.
// The gate re-derives the engine screen set and pushes it back through
// setActiveScreens; the set is identical across a close, and that branch
// retiles every screen unconditionally. So before the third arm every close
// reflowed the strip one event-loop turn later through that path while both
// other arms sat holding.
//
// Swallowing a retile loses nothing. The flush below applies with the same
// default focusWindowAfter the retile would have passed, so it IS that apply,
// one hold later — which is equally true of the config, per-screen and
// min-size retiles that reach the same guard.
//
// A screen that LEAVES the scrolling set, or the engine entirely, drops its
// entries: setActiveScreens' removal loop and pruneStatesForRemovedScreen both
// sweep, so a deadline never outlives the strip it was holding. The mode-exit
// half of that is hygiene rather than a live defect, and it is worth being
// exact about which: the removal tears the context state down and releases its
// windows, so a retile swallowed on re-entry by a leftover deadline would have
// had an empty strip to lay out, and the windows repopulate it through
// windowOpened, which bypasses the hold by design. Swept because every other
// per-screen arm on that path is, and because the entry otherwise outlives the
// only strip it means anything for. An armed flush timer holds only the id and
// re-reads both containers, so dropping the entries just makes it a no-op.

void ScrollEngine::startCloseReflowHold(const QString& screenId)
{
    if (!m_closeReflowClock.isValid()) {
        m_closeReflowClock.start();
    }
    // Latest close wins: a second close inside the hold pushes the deadline
    // so ITS animation also plays out before the one flush runs.
    m_closeReflowHoldUntil[screenId] = m_closeReflowClock.elapsed() + m_closeReflowDelayMs;
    scheduleCloseReflowFlush(screenId);
}

bool ScrollEngine::deferForCloseReflowHold(const QString& screenId)
{
    const auto it = m_closeReflowHoldUntil.constFind(screenId);
    if (it == m_closeReflowHoldUntil.constEnd()) {
        return false;
    }
    if (!m_closeReflowClock.isValid() || m_closeReflowClock.elapsed() >= *it) {
        m_closeReflowHoldUntil.remove(screenId);
        return false;
    }
    // Still inside the hold: make sure a flush is coming, and tell the
    // caller to skip its immediate applyLayout — the flush is that apply.
    scheduleCloseReflowFlush(screenId);
    return true;
}

void ScrollEngine::scheduleCloseReflowFlush(const QString& screenId)
{
    if (m_closeReflowFlushScheduled.contains(screenId)) {
        return;
    }
    m_closeReflowFlushScheduled.insert(screenId);
    const qint64 remaining = qMax<qint64>(0, m_closeReflowHoldUntil.value(screenId) - m_closeReflowClock.elapsed());
    QTimer::singleShot(static_cast<int>(remaining) + 1, this, [this, screenId] {
        m_closeReflowFlushScheduled.remove(screenId);
        const auto it = m_closeReflowHoldUntil.constFind(screenId);
        if (it != m_closeReflowHoldUntil.constEnd() && m_closeReflowClock.elapsed() < *it) {
            // A later close pushed the deadline while this timer was armed —
            // re-arm for the remainder rather than flushing early.
            scheduleCloseReflowFlush(screenId);
            return;
        }
        m_closeReflowHoldUntil.remove(screenId);
        // applyLayout resolves the screen's CURRENT context itself, so a
        // desktop switch during the hold lands this on the right strip; on
        // a screen that stopped scrolling meanwhile it no-ops.
        applyLayout(screenId, false);
    });
}

} // namespace PhosphorScrollEngine
