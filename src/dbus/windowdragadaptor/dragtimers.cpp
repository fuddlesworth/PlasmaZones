// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowdragadaptor.h"
#include "dragactivation.h"
#include <QTimer>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorScreens/ScreenIdentity.h>

/**
 * @file
 * @brief The drag adaptor's timer-driven work: the drag-scroll heartbeat, the
 * hold-mode release-grace expiry, and the scroll drop indicator those two
 * push.
 *
 * Split out of windowdragadaptor.cpp so that file stays inside the file-size
 * ceiling. The two timer families belong together: both are lazily created,
 * both are stopped from the shared teardown, and both self-terminate from
 * inside their own handler rather than trusting callers to stop them.
 */

namespace PlasmaZones {

// Sampling interval of the drag edge auto-scroll heartbeat, ~60 Hz. Not a
// tunable: it sets how finely the strip is sampled, never how fast it
// travels, because the engine integrates against the real elapsed time.
static constexpr int kDragScrollTickMs = 16;

void WindowDragAdaptor::ensureDragScrollTimerRunning(const QString& windowId)
{
    if (!m_dragScrollTimer) {
        m_dragScrollTimer = new QTimer(this);
        // The engine integrates against the REAL elapsed time, so this
        // figure only sets how finely the strip is sampled, not how fast it
        // travels.
        m_dragScrollTimer->setInterval(kDragScrollTickMs);
        connect(m_dragScrollTimer, &QTimer::timeout, this, &WindowDragAdaptor::advanceDragScroll);
    }
    if (m_dragScrollWindowId != windowId) {
        // A different drag owns the timer now: restart the elapsed clock so
        // the first tick of this drag cannot integrate the gap since the
        // last one.
        m_dragScrollWindowId = windowId;
        m_dragScrollElapsed.invalidate();
    }
    if (!m_dragScrollTimer->isActive()) {
        m_dragScrollElapsed.invalidate();
        m_dragScrollTimer->start();
    }
}

void WindowDragAdaptor::stopDragScrollTimer()
{
    if (m_dragScrollTimer) {
        m_dragScrollTimer->stop();
    }
    m_dragScrollWindowId.clear();
    m_dragScrollElapsed.invalidate();
}

void WindowDragAdaptor::armGraceExpiry(qint64 remainingMs)
{
    if (!m_graceExpiryTimer) {
        m_graceExpiryTimer = new QTimer(this);
        m_graceExpiryTimer->setSingleShot(true);
        m_graceExpiryTimer->setTimerType(Qt::PreciseTimer);
        connect(m_graceExpiryTimer, &QTimer::timeout, this, [this]() {
            // Replay the last tick with its own inputs. The trigger is still
            // released in them, and the grace has run out by now, so the
            // replay takes the release arm the original tick deferred. A
            // finished drag has no window id and the replay is a no-op.
            if (m_draggedWindowId.isEmpty()) {
                return;
            }
            dragMoved(m_draggedWindowId, m_lastTickCursorX, m_lastTickCursorY, m_lastTickModifiers,
                      m_lastTickMouseButtons);
        });
    }
    // Both rules live in dragactivation.h as pure functions so they can be
    // pinned without standing up a timer: one past the deadline, and earlier
    // deadline wins across the three families sharing this timer.
    const int dueMs = graceExpiryDueMs(remainingMs);
    if (shouldRearmGraceExpiry(m_graceExpiryTimer->isActive(), m_graceExpiryTimer->remainingTime(), dueMs)) {
        m_graceExpiryTimer->start(dueMs);
    }
}

void WindowDragAdaptor::stopGraceExpiry()
{
    if (m_graceExpiryTimer) {
        m_graceExpiryTimer->stop();
    }
}

void WindowDragAdaptor::advanceDragScroll()
{
    PhosphorEngine::IPlacementEngine* engine = dragInsertPreviewEngine();
    // Self-terminating: several engine-side paths cancel a preview without
    // telling the adaptor, and the drag itself can end between ticks. Rather
    // than trust every caller to have stopped us, notice here.
    if (!engine || m_draggedWindowId.isEmpty() || m_draggedWindowId != m_dragScrollWindowId) {
        stopDragScrollTimer();
        return;
    }
    // A dt of zero on the first tick of an arming is not a special case, only
    // a zero-length interval: the engine still needs the tick to arm its
    // start delay, and it clamps dt into [0, ceiling] itself. Resolved and
    // dispatched once so both arms share the same screen id and the same
    // post-condition — an earlier split let the first tick silently drop a
    // repaint the engine had asked for.
    const qreal dt = m_dragScrollElapsed.isValid() ? qreal(m_dragScrollElapsed.nsecsElapsed()) / 1'000'000'000.0 : 0.0;
    m_dragScrollElapsed.restart();
    const QString screenId = engine->dragInsertPreviewScreenId();
    if (!engine->dragAutoScrollTick(screenId, m_lastDragCursorPos, dt)) {
        return;
    }
    // The engine owns the target while it scrolls (it writes the view's
    // leading/trailing new-column slot itself), so there is nothing to
    // hit-test here — only the rect that target resolves to, which moves
    // with the view.
    pushScrollDropIndicator(screenId, engine->dragInsertIndicatorRect(screenId), /*animate=*/false);
}

void WindowDragAdaptor::pushScrollDropIndicator(const QString& screenId, const QRect& rect, bool animate)
{
    if (!m_overlayService || screenId.isEmpty()) {
        return;
    }
    // Cross-screen drag: hide the old screen's indicator before lighting the
    // new one. Without this the departed screen keeps painting a target the
    // drop can no longer land in, and nothing else would clear it — the
    // teardown paths only know the screen recorded here.
    // screensMatch, not raw !=, defensively: the recorded id and an incoming
    // one can spell the same output as a physical id or a virtual one, and a
    // raw compare would read a spelling change as a screen change — pushing a
    // hide the very next line un-hides. Every sibling comparison in this file
    // already uses it.
    if (!m_dropIndicatorScreenId.isEmpty()
        && !PhosphorScreens::ScreenIdentity::screensMatch(m_dropIndicatorScreenId, screenId)) {
        // The departing screen's hide is never animated: there is no target
        // to make legible, only a rectangle that must stop being painted.
        m_overlayService->updateScrollDropIndicator(m_dropIndicatorScreenId, QRect(), /*animate=*/false);
    }
    m_overlayService->updateScrollDropIndicator(screenId, rect, animate);
    // An empty rect means the engine has no paintable target (autotile by
    // interface default, or a preview with nothing hit-tested yet). The
    // overlay treats that as a hide, so do not record the screen as lit —
    // otherwise the next clear would push a redundant second hide.
    m_dropIndicatorScreenId = rect.isValid() ? screenId : QString();
}

void WindowDragAdaptor::clearScrollDropIndicator()
{
    if (m_dropIndicatorScreenId.isEmpty()) {
        return;
    }
    if (m_overlayService) {
        m_overlayService->updateScrollDropIndicator(m_dropIndicatorScreenId, QRect(), /*animate=*/false);
    }
    m_dropIndicatorScreenId.clear();
}

} // namespace PlasmaZones
