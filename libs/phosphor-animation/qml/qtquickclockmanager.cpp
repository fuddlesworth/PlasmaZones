// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/QtQuickClockManager.h>

#include <PhosphorAnimation/QtQuickClock.h>

#include <QtCore/QThread>
#include <QtQuick/QQuickWindow>

namespace PhosphorAnimation {

QtQuickClockManager::QtQuickClockManager() = default;

QtQuickClockManager::~QtQuickClockManager()
{
    // Process-exit teardown hardening. Two hazards to handle:
    //
    //   1. Any `Entry::destroyedConnection` that is still live at this
    //      point would — if its source window is later destroyed after
    //      this singleton — fire the `releaseClockFor` lambda
    //      (DirectConnection) through a `this` that is mid-destruction.
    //      Disconnect explicitly under the lock so no later signal
    //      dispatches can reach us.
    //
    //   2. `QtQuickClock::~QtQuickClock` unsubscribes from its window's
    //      `beforeRendering`. That disconnect is safe against a
    //      destroyed window (Qt auto-disconnects), but running it under
    //      the lock serialises with any in-flight `clockFor` racing
    //      from another static destructor. The `unique_ptr` reset
    //      inside `m_entries.clear()` handles the delete order.
    //
    // At process exit with a well-behaved `QCoreApplication` teardown
    // (QApp is a stack-scoped local in `main()`), every QQuickWindow
    // is already gone by the time statics unwind — so these disconnects
    // are typically no-ops. The cost of doing them is negligible and
    // they catch the non-standard teardown paths (embedded shells,
    // test harnesses manipulating the QApp lifetime directly) where
    // the invariant does not hold.
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [_, entry] : m_entries) {
        QObject::disconnect(entry.destroyedConnection);
    }
    m_entries.clear();
}

QtQuickClockManager& QtQuickClockManager::instance()
{
    // Meyers singleton — thread-safe init under C++17's static-local
    // guarantee. The manager lives for the process lifetime; no
    // destruction order concerns because the manager holds only
    // QtQuickClock instances, which themselves tolerate destruction
    // during process shutdown (their destructor handles the signal-
    // disconnect + slot-join without external synchronisation).
    static QtQuickClockManager sInstance;
    return sInstance;
}

IMotionClock* QtQuickClockManager::clockFor(QQuickWindow* window)
{
    if (!window) {
        return nullptr;
    }

    // Thread contract: must be called from the thread that owns
    // @p window (always the GUI thread for a live QQuickWindow). The
    // `QObject::connect` call below dereferences `window` outside the
    // manager's lock; letting a non-owning thread run that would race
    // with a concurrent window teardown on the owning thread and UAF
    // inside Qt's connection machinery before the post-connect recheck
    // could catch the stale QPointer.
    //
    // In practice every caller today (PhosphorMotionAnimation,
    // PhosphorAnimatedValueBase, direct QML-adjacent consumers) runs
    // on the GUI thread by construction, so this assertion documents
    // the true contract rather than narrowing it.
    Q_ASSERT_X(window->thread() == QThread::currentThread(), "QtQuickClockManager::clockFor",
               "must be called from the window's owning thread (GUI thread for QQuickWindow)");

    // Phase 1: map mutation under the manager lock. We find-or-insert
    // the Entry and decide whether this call is the one that created
    // it — but we defer the QObject::connect call until AFTER the
    // lock is released. Qt6's connect() takes internal per-object
    // locks (a per-QObject signal/slot table mutex plus, for
    // DirectConnection with a functor receiver, Qt's per-thread
    // dispatcher state); nesting our own mutex around that call is a
    // lock-ordering hazard — any other code path that takes Qt's
    // internal lock first and then wants our mutex (e.g., a
    // `destroyed` signal firing into releaseClockFor while another
    // thread is mid-clockFor) could deadlock.
    //
    // Two-phase design constraint: another thread racing this one on
    // the SAME window must not install a second `destroyed`
    // connection. We guard this via a `justCreated` flag returned
    // from the locked block — only the winning inserter connects; the
    // loser sees an existing entry and returns its clock unchanged.
    IMotionClock* rawClock = nullptr;
    bool justCreated = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (auto it = m_entries.find(window); it != m_entries.end()) {
            // Stale-window check: the raw pointer matches a map key
            // but the QPointer inside the entry has gone null. With
            // eager `destroyed`-signal eviction (below) this should
            // be rare, but we still handle it defensively: the
            // destroyed signal can race with a direct teardown where
            // Qt tears down the QObject without emitting `destroyed`
            // (process-exit path).
            if (!it->second.window) {
                m_entries.erase(it);
            } else {
                return it->second.clock.get();
            }
        }

        // First lookup for this window — construct a fresh QtQuickClock.
        // QtQuickClock's ctor subscribes to beforeRendering internally;
        // no further wiring needed here.
        auto clock = std::make_unique<QtQuickClock>(window);
        rawClock = clock.get();

        // `try_emplace(key, args...)` is the race-safe variant of
        // `emplace`: if the key is already present it does NOT move
        // from `args`, so the local `clock` stays alive and owned by
        // us on the collision path. That lets us deterministically
        // tear down the just-constructed QtQuickClock (which has
        // already subscribed to `beforeRendering` in its ctor) rather
        // than leaking the subscription when a concurrent inserter
        // beat us to this key between the find() above and here.
        //
        // Capture the return value — ignoring it was the pre-fix bug:
        // two concurrent callers that both saw the initial find()
        // miss would each construct a QtQuickClock, and the loser's
        // clock + beforeRendering connection would leak. The thread-
        // ownership assert above should serialise GUI-thread callers,
        // but Qt's internal `destroyed`-signal dispatch (which evicts
        // entries) can also race this section, so defensive handling
        // is cheaper than the UAF surface.
        auto [insertedIt, inserted] =
            m_entries.try_emplace(window, Entry{QPointer<QQuickWindow>(window), std::move(clock), {}});
        if (!inserted) {
            // Race lost: another inserter already has this key. Our
            // local `clock` is still owned by us because try_emplace
            // does not move from args on collision. Let the local
            // unique_ptr's scope-exit destruction run — QtQuickClock's
            // dtor disconnects from `beforeRendering` so the losing
            // clock leaves no stray subscription. Return the winner's
            // clock pointer so every caller sees the same
            // `IMotionClock*` for this window.
            //
            // Because try_emplace didn't move the Entry argument, our
            // local `clock` inside that temporary Entry was also not
            // consumed — but the temporary Entry itself is destroyed at
            // the end of this full-expression, which destroys its
            // unique_ptr and disconnects the subscription. `justCreated`
            // stays false so the post-lock connect() block does not
            // install a destroyed-hook for an entry that is already
            // wired on the winning insertion.
            rawClock = insertedIt->second.clock.get();
            justCreated = false;
        } else {
            justCreated = true;
        }
    }

    if (justCreated) {
        // Eager eviction: when the window emits `destroyed`, drop the
        // entry synchronously on the GUI thread. This prevents the
        // address-reuse hazard (Qt recycling the QQuickWindow* for a
        // fresh window) AND lets consumers assume that after
        // `destroyed`, nobody is handing out a stale `IMotionClock*`
        // to this window.
        //
        // DirectConnection because the signal fires from the QObject
        // destructor on the GUI thread — there is no event loop to
        // deliver a queued connection to, and we need the eviction to
        // happen before other GUI-thread teardown hooks run.
        //
        // Race window between emplace() above and this connect(): if
        // the window is destroyed in this narrow gap (only possible
        // on the GUI thread for a GUI-thread-owned QObject), the
        // destroyed signal has already fired without a listener and
        // the map holds a stale entry. We catch that by rechecking
        // the QPointer under the lock below; if it's null we evict
        // and return nullptr so the caller does not route animations
        // onto a dead clock.
        // SingleShotConnection so a multi-fire is impossible by
        // construction. The receiver is `window`, so Qt auto-
        // disconnects on `~QObject` even without the SingleShot
        // flag — the explicit single-shot semantic also stops a
        // manual releaseClockFor (test teardown) from leaving a live
        // connection that races against the dying window.
        auto connection = QObject::connect(
            window, &QObject::destroyed, window,
            [this, window](QObject*) {
                releaseClockFor(window);
            },
            Qt::ConnectionType(Qt::DirectConnection | Qt::SingleShotConnection));

        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto it = m_entries.find(window); it != m_entries.end()) {
            if (!it->second.window) {
                // Window died between emplace() and connect() — evict
                // and refuse to hand out the clock; the caller will
                // re-resolve on the next frame tick and either find
                // the window gone entirely or bind to its replacement.
                QObject::disconnect(connection);
                m_entries.erase(it);
                return nullptr;
            }
            it->second.destroyedConnection = connection;
        } else {
            // Entry was evicted by a concurrent releaseClockFor (rare
            // but possible under the address-reuse path). Drop the
            // just-installed connection so it does not fire into a
            // map lookup that will miss.
            QObject::disconnect(connection);
            return nullptr;
        }
    }

    return rawClock;
}

void QtQuickClockManager::releaseClockFor(QQuickWindow* window)
{
    if (!window) {
        return;
    }
    // Two-phase teardown: move the unique_ptr<QtQuickClock> OUT of
    // the map under the lock, then erase the map entry, then drop
    // the lock and let the local unique_ptr destruct on scope exit.
    // The `~QtQuickClock` call disconnects from `beforeRendering`,
    // which takes Qt's per-QObject signal/slot lock — destructing
    // the clock under `m_mutex` would invert the lock order from
    // `clockFor`'s (Qt-lock then m_mutex) into (m_mutex then
    // Qt-lock) and produce exactly the deadlock the rest of this
    // class works to avoid.
    std::unique_ptr<QtQuickClock> evictedClock;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto it = m_entries.find(window); it != m_entries.end()) {
            // Disconnect the destroyed hook before erasing — we may
            // have been called FROM the destroyed signal (eager
            // eviction) in which case the connection is already
            // auto-disconnected by Qt, and the disconnect is a
            // no-op. For the manual-release path (tests, explicit
            // teardown), this prevents a stale lambda from firing
            // later.
            QObject::disconnect(it->second.destroyedConnection);
            evictedClock = std::move(it->second.clock);
            m_entries.erase(it);
        }
    }
    // evictedClock destructs here, lock-free.
}

int QtQuickClockManager::entryCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_entries.size());
}

void QtQuickClockManager::clearForTest()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // Disconnect every entry's destroyed hook before erasing — a
    // live connection outliving the entry would later fire into a
    // releaseClockFor on an already-empty map. That's safe (no-op
    // after lookup miss), but noisy and wrong-looking.
    for (auto& [_, entry] : m_entries) {
        QObject::disconnect(entry.destroyedConnection);
    }
    m_entries.clear();
}

} // namespace PhosphorAnimation
