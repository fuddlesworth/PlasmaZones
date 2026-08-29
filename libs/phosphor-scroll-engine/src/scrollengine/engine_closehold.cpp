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
