// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/QtQuickClock.h>

#include <QObject>
#include <QQuickWindow>
#include <QScreen>

namespace PhosphorAnimation {

/**
 * @brief Internal QObject that owns the beforeRendering → now() wire-up.
 *
 * Kept private to the .cpp so the public header doesn't need Q_OBJECT
 * and doesn't pull QObject into the header dependency set. The adapter
 * writes the cached `now()` timestamp from the render thread; the
 * clock's `now()` reader runs on whichever thread the consumer calls
 * from (typically also the render thread for Qt Quick animations).
 *
 * Thread safety: the cached timestamp is held as `std::atomic` on
 * `QtQuickClock` with relaxed ordering. Same-thread use incurs only
 * aligned-load/store cost on x86_64; cross-thread use (GUI-thread
 * reader) avoids formal data-race UB.
 */
class QtQuickClock::SignalAdapter : public QObject
{
public:
    explicit SignalAdapter(QtQuickClock* owner, QQuickWindow* window);
    ~SignalAdapter() override = default;

private:
    QtQuickClock* m_owner;
};

// ═══════════════════════════════════════════════════════════════════════════════
// SignalAdapter
// ═══════════════════════════════════════════════════════════════════════════════

QtQuickClock::SignalAdapter::SignalAdapter(QtQuickClock* owner, QQuickWindow* window)
    : m_owner(owner)
{
    if (!window) {
        return;
    }
    // Direct connection: the slot runs on the render thread where
    // beforeRendering fires. That's where we want the timestamp
    // captured — it's the thread that will subsequently drive
    // AnimatedValue::advance() via the consumer's render-thread hook.
    QObject::connect(
        window, &QQuickWindow::beforeRendering, this,
        [this]() {
            const auto ns = std::chrono::steady_clock::now().time_since_epoch().count();
            m_owner->m_nowCache.store(ns, std::memory_order_relaxed);
        },
        Qt::DirectConnection);
}

// ═══════════════════════════════════════════════════════════════════════════════
// QtQuickClock
// ═══════════════════════════════════════════════════════════════════════════════

QtQuickClock::QtQuickClock(QQuickWindow* window)
    : m_window(window)
    , m_adapter(std::make_unique<SignalAdapter>(this, window))
{
}

QtQuickClock::~QtQuickClock() = default;

std::chrono::nanoseconds QtQuickClock::now() const
{
    return std::chrono::nanoseconds{m_nowCache.load(std::memory_order_relaxed)};
}

qreal QtQuickClock::refreshRate() const
{
    if (!m_window) {
        return 0.0;
    }
    const QScreen* screen = m_window->screen();
    if (!screen) {
        return 0.0;
    }
    // QScreen::refreshRate() returns Hz (qreal) — already the unit
    // IMotionClock::refreshRate contracts for, no conversion needed
    // (unlike CompositorClock which reads KWin's millihertz value).
    return screen->refreshRate();
}

void QtQuickClock::requestFrame()
{
    if (m_window) {
        // QQuickWindow::update() is thread-safe by Qt design —
        // internally posts an event to the GUI thread. Safe to call
        // from the render thread that beforeRendering fires on.
        m_window->update();
    }
}

QQuickWindow* QtQuickClock::window() const
{
    return m_window.data();
}

} // namespace PhosphorAnimation
