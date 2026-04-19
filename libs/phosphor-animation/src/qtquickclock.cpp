// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/QtQuickClock.h>

#include <QCoreApplication>
#include <QObject>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>

#include <mutex>

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
 * ## Thread-safety of the cached timestamp
 *
 * The cached timestamp is held as `std::atomic` on `QtQuickClock` with
 * relaxed ordering. Same-thread use incurs only aligned-load/store
 * cost on x86_64; cross-thread use (GUI-thread reader) avoids formal
 * data-race UB.
 *
 * ## Teardown race mitigation
 *
 * `Qt::DirectConnection` slots run on whichever thread emits the
 * signal (render thread for `beforeRendering`). `~QObject` atomically
 * disconnects future emissions but does not join already-executing
 * slot bodies. Without mitigation, destroying the clock on the GUI
 * thread while the render thread is mid-slot would write to a
 * freed `m_nowCache`. The adapter guards against this with a mutex:
 *
 * - Slot: acquires `m_mutex` for the duration of the write; early-
 *   returns if `m_detached` is set.
 * - Destructor: explicitly `QObject::disconnect(m_conn)` first so no
 *   NEW slot dispatches can occur, then acquires `m_mutex` (drains
 *   any in-flight slot), flips `m_detached`, and releases. Any
 *   already-running slot completes before the mutex is destructed
 *   with the adapter.
 */
class QtQuickClock::SignalAdapter : public QObject
{
public:
    explicit SignalAdapter(QtQuickClock* owner, QQuickWindow* window);
    ~SignalAdapter() override;

private:
    QtQuickClock* m_owner;
    std::mutex m_mutex;
    bool m_detached = false;
    QMetaObject::Connection m_conn;
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
    //
    // The connection is stored so ~SignalAdapter can explicitly
    // disconnect BEFORE draining via m_mutex — Qt's implicit
    // disconnect in ~QObject races with in-flight slot bodies on
    // direct connections.
    m_conn = QObject::connect(
        window, &QQuickWindow::beforeRendering, this,
        [this]() {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_detached) {
                return;
            }
            const auto ns = std::chrono::steady_clock::now().time_since_epoch().count();
            m_owner->m_nowCache.store(ns, std::memory_order_relaxed);
        },
        Qt::DirectConnection);
}

QtQuickClock::SignalAdapter::~SignalAdapter()
{
    // Disconnect first so no new slot dispatches occur while we drain.
    // `QObject::disconnect` is thread-safe and returns after the
    // connection is marked as inactive for future emissions.
    QObject::disconnect(m_conn);
    // Acquire the mutex to wait out any in-flight slot body. Once we
    // hold the lock, no slot is executing; set m_detached as
    // belt-and-suspenders against any spurious future dispatch that
    // slips past the disconnect (shouldn't happen but cheap to guard).
    std::lock_guard<std::mutex> lock(m_mutex);
    m_detached = true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// QtQuickClock
// ═══════════════════════════════════════════════════════════════════════════════

QtQuickClock::QtQuickClock(QQuickWindow* window)
    : m_window(window)
    , m_adapter(std::make_unique<SignalAdapter>(this, window))
{
    // Prime the cache with a real steady_clock reading so any `now()`
    // consumer that runs before the first `beforeRendering` slot fires
    // sees a monotonic timestamp in the same epoch the slot will later
    // continue from. Without this, the first advance() on an
    // AnimatedValue bound to a freshly-constructed clock latches
    // m_startTime = 0ns; the NEXT advance (post-beforeRendering) would
    // see elapsed = seconds-since-boot and skip directly to completion.
    m_nowCache.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
}

QtQuickClock::~QtQuickClock() = default;

std::chrono::nanoseconds QtQuickClock::now() const
{
    return std::chrono::nanoseconds{m_nowCache.load(std::memory_order_relaxed)};
}

qreal QtQuickClock::refreshRate() const
{
    // GUI-thread-only contract: QScreen lookup walks Qt's platform screen
    // list, which is not render-thread-safe. `now()` and `requestFrame()`
    // are the cross-thread methods by design; this one is explicitly not.
    // Fail loudly in debug builds if a consumer routes it off the GUI
    // thread so the constraint in the header doc bites on contact
    // instead of manifesting as a subtle crash later.
    Q_ASSERT_X(QCoreApplication::instance() == nullptr
                   || QThread::currentThread() == QCoreApplication::instance()->thread(),
               "QtQuickClock::refreshRate", "must be called on the GUI thread (touches QScreen)");
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

const void* QtQuickClock::epochIdentity() const
{
    // std::chrono::steady_clock — same epoch family as CompositorClock,
    // so an AnimatedValue started by a QML driver can rebind onto a
    // compositor clock mid-flight without a timestamp corruption.
    return IMotionClock::steadyClockEpoch();
}

} // namespace PhosphorAnimation
