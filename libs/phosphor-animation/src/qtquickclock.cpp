// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/QtQuickClock.h>

#include <QCoreApplication>
#include <QObject>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <thread>

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
 * Only functor-based `QObject::connect` is used — no string-based
 * signals/slots, so Q_OBJECT is not required. Future maintainers
 * adding signals or properties must add Q_OBJECT and a
 * `#include "qtquickclock.moc"` trailer for moc generation.
 *
 * ## Thread-safety of the cached timestamp
 *
 * The cached timestamp is held as `std::atomic` on `QtQuickClock` with
 * release/acquire ordering. Same-thread use incurs only aligned-load/
 * store cost on x86_64; cross-thread use (GUI-thread reader, render-
 * thread writer) is well-defined and publishes writes happens-before
 * the reader's acquire.
 *
 * ## Teardown race mitigation
 *
 * `Qt::DirectConnection` slots run on whichever thread emits the
 * signal (render thread for `beforeRendering`). `QObject::disconnect`
 * atomically marks the connection inactive for FUTURE emissions but
 * does NOT join already-executing slot bodies nor block in-progress
 * Qt-internal dispatches that resolved the functor pointer before the
 * disconnect call landed. Destroying the adapter's owned mutex while
 * a slot thread is still contending for it would be UB — so the
 * adapter deliberately does NOT own a mutex.
 *
 * Instead: an `std::atomic<bool> m_detached` flag gates every slot
 * body read; the destructor sets it (release) then disconnects. Any
 * slot that passed the disconnect check before we set the flag will
 * re-read the flag on entry via acquire and return without touching
 * owner state.
 *
 * Join-on-destroy: the atomic flag alone leaves a small UAF window
 * for a slot that passed the detached check *before* the destructor
 * fired but was preempted between the check and the cache write. By
 * the time the slot resumes, the destructor could have returned and
 * `QtQuickClock`'s remaining fields (after `m_adapter` destruction)
 * could have been torn down in the normal member-destruction order.
 * `m_slotDepth` counts in-progress slots — entry increments, exit
 * decrements, and the destructor spins until the count reaches zero
 * *after* setting `m_detached` + disconnecting. This guarantees any
 * slot that raced past the detached check has fully exited before
 * the destructor returns; any slot that saw `detached == true` paid
 * only an atomic acquire-load and returned without touching state.
 *
 * Deadlock constraint: the destructor MUST NOT run on the thread that
 * fires the slot (the render thread). If it did, the spin would wait
 * on a counter only this thread could decrement. For QtQuickClock the
 * owner thread is typically the GUI thread, where lifecycle events
 * fire; the render thread is distinct.
 */
class QtQuickClock::SignalAdapter : public QObject
{
public:
    explicit SignalAdapter(QtQuickClock* owner, QQuickWindow* window);
    ~SignalAdapter() override;

private:
    QtQuickClock* m_owner;
    std::atomic<bool> m_detached{false};
    // In-progress slot body count. Render-thread slot entries increment
    // (after the detached check passes) and exits decrement; the
    // destructor spins on this after setting `m_detached` so a
    // preempted slot finishes before owner state is torn down. Int
    // because the slot is reentrancy-free (one signal per window,
    // serialised by Qt) and the count never exceeds 1 in practice —
    // but the counter idiom is portable and matches the refcount
    // pattern used elsewhere in PhosphorAnimation.
    std::atomic<int> m_slotDepth{0};
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
    // disconnect, but the teardown mitigation relies on the atomic
    // `m_detached` flag, not the disconnect alone — see class-doc.
    m_conn = QObject::connect(
        window, &QQuickWindow::beforeRendering, this,
        [this]() {
            // Acquire on every slot entry — `~SignalAdapter` publishes
            // `m_detached=true` with release, so an entry observing
            // `true` is guaranteed to observe every memory write that
            // happened-before the destructor (even though here we just
            // need the flag bit itself).
            if (m_detached.load(std::memory_order_acquire)) {
                return;
            }
            // Join-on-destroy: increment the in-progress counter *after*
            // the detached check so the destructor's spin-wait reaches
            // zero the moment the last non-detached slot exits. Re-read
            // the detached flag under the counter — if the destructor
            // published `detached` between the earlier check and this
            // increment, we back out without touching owner state.
            m_slotDepth.fetch_add(1, std::memory_order_acquire);
            if (m_detached.load(std::memory_order_acquire)) {
                m_slotDepth.fetch_sub(1, std::memory_order_release);
                return;
            }
            const auto ns = std::chrono::steady_clock::now().time_since_epoch().count();
            // Monotonicity floor across the fallback→cache handoff:
            // GUI-thread `now()` reads on the fallback path also CAS-
            // seed the cache with their reading. The writer takes
            // `max(prev, ns)` so a slightly-earlier render-thread
            // capture (cross-core TSC skew on pathological systems)
            // cannot publish a value below what a GUI-thread reader
            // already observed. Under normal monotonic-steady_clock
            // semantics this is a no-op — `ns` is always ≥ `prev`.
            auto prev = m_owner->m_nowCache.load(std::memory_order_acquire);
            while (ns > prev
                   && !m_owner->m_nowCache.compare_exchange_weak(prev, ns, std::memory_order_release,
                                                                 std::memory_order_acquire)) {
                // CAS retry — `prev` updated with the latest seen value.
            }
            // Latch once on first fire — `now()` flips from the
            // steady_clock freshness fallback to reading the cache
            // directly, so every consumer of this clock sees the same
            // vsync-aligned reading for the rest of the frame.
            m_owner->m_renderLoopActive.store(true, std::memory_order_release);
            // Release-decrement pairs with the destructor's acquire-load
            // of the depth counter; publishes every write above
            // happens-before the join-wait observing zero.
            m_slotDepth.fetch_sub(1, std::memory_order_release);
        },
        Qt::DirectConnection);
}

QtQuickClock::SignalAdapter::~SignalAdapter()
{
    // Publish the detach flag FIRST (release). Any slot body that enters
    // after this point (whether before or after the disconnect returns)
    // will acquire-load `true` and early-return without touching owner
    // state. Ordering matters: the disconnect below is not a barrier for
    // already-dispatched emissions, so the detached flag is the only
    // thing that stops a mid-dispatch slot.
    m_detached.store(true, std::memory_order_release);
    // Disconnect FUTURE emissions — Qt marks the connection inactive,
    // so `beforeRendering` fired after this point will not dispatch our
    // functor. This is belt-and-suspenders: even if the detached flag
    // alone would prevent owner-state mutation, skipping the dispatch
    // entirely saves the render thread a mutex-less no-op call.
    QObject::disconnect(m_conn);
    // Join-on-destroy: spin until every in-progress slot body has
    // exited. A slot that observed `detached == false` between the
    // detached-flag store above and its increment below already
    // back-paths out of the work section (see the under-counter
    // re-check in the slot body); this wait covers the narrower
    // window of a slot that decided to proceed BEFORE we published
    // `detached` and is now racing toward owner-state writes. The
    // destructor blocks here (on the GUI / owner thread) until the
    // render thread finishes that one slot. Deadlock-safe only if
    // the destructor is NOT running on the thread that fires the
    // slot (the render thread) — QtQuickClock's documented
    // ownership model makes this a GUI-thread-only destruction.
    //
    // The worst-case wait is one `beforeRendering` slot body, which
    // is a few atomic operations + a steady_clock read — tens of
    // nanoseconds. `std::this_thread::yield()` is a polite backoff
    // that lets the OS scheduler hand the CPU to the render thread
    // if the two share a core.
    while (m_slotDepth.load(std::memory_order_acquire) > 0) {
        std::this_thread::yield();
    }
    // After the join, no slot can be running and no future slot will
    // dispatch (both the detached flag and the disconnect ensure
    // that). Owner state is safe to tear down in the subsequent
    // member destructors — the earlier "outlives via declaration
    // order" argument now carries no race weight.
}

// ═══════════════════════════════════════════════════════════════════════════════
// QtQuickClock
// ═══════════════════════════════════════════════════════════════════════════════

QtQuickClock::QtQuickClock(QQuickWindow* window)
    : m_window(window)
    , m_adapter(std::make_unique<SignalAdapter>(this, window))
{
    // No cache prime: `now()` reads `steady_clock` directly until the
    // first `beforeRendering` slot flips `m_renderLoopActive`, at which
    // point the slot overwrites `m_nowCache` before any reader sees it.
    // A constructor-time prime would be strictly overwritten before use.
}

QtQuickClock::~QtQuickClock() = default;

std::chrono::nanoseconds QtQuickClock::now() const
{
    // Null-window test mode: the adapter never connects, so
    // `beforeRendering` never fires. The documented contract for this
    // mode is "now() stays at zero" — a consumer driving an
    // AnimatedValue against a null-window clock is in a test / teardown
    // path with no paint loop; returning a steady_clock reading would
    // let animations tick with nonsense `dt` on every call. Match the
    // header.
    if (!m_window) {
        return std::chrono::nanoseconds{0};
    }
    // Freshness fallback. A `QQuickWindow` that hasn't been shown yet
    // never fires `beforeRendering`, so the cache would otherwise stay
    // at its default 0 forever — an AnimatedValue advancing against
    // this clock would see constant `now()` and `dt == 0` on every
    // tick (latched startTime, no progression). Until the render loop
    // fires for the first time, read `steady_clock` directly; once
    // `m_renderLoopActive` is set, trust the cache so every
    // AnimatedValue advanced in the same frame sees an identical
    // vsync-aligned reading (the whole point of caching).
    if (m_renderLoopActive.load(std::memory_order_acquire)) {
        return std::chrono::nanoseconds{m_nowCache.load(std::memory_order_acquire)};
    }
    // Pre-handoff fallback: read steady_clock directly, but also CAS-
    // seed the cache with this reading. The eventual render-thread
    // writer takes `max(prev, captured)` so the first post-flip cache
    // value can never be below any fallback reading a GUI consumer
    // already observed. This closes the monotonicity hole that would
    // otherwise open if cross-core TSC skew lets the render thread
    // capture a slightly-earlier steady_clock value than the GUI
    // thread's most recent fallback read.
    const auto reading = std::chrono::steady_clock::now().time_since_epoch().count();
    auto prev = m_nowCache.load(std::memory_order_acquire);
    while (reading > prev
           && !m_nowCache.compare_exchange_weak(prev, reading, std::memory_order_release, std::memory_order_acquire)) {
        // CAS retry — `prev` updated with the latest seen value.
    }
    return std::chrono::nanoseconds{reading};
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
