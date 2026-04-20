// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/QtQuickClockManager.h>

#include <PhosphorAnimation/QtQuickClock.h>

#include <QtQuick/QQuickWindow>

namespace PhosphorAnimation {

QtQuickClockManager::QtQuickClockManager() = default;

QtQuickClockManager::~QtQuickClockManager() = default;

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

    std::lock_guard<std::mutex> lock(m_mutex);

    if (auto it = m_entries.find(window); it != m_entries.end()) {
        // Stale-window check: the raw pointer matches a map key but
        // the QPointer inside the entry has gone null. With eager
        // `destroyed`-signal eviction (below) this should be rare,
        // but we still handle it defensively: the destroyed signal
        // can race with a direct teardown where Qt tears down the
        // QObject without emitting `destroyed` (process-exit path).
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
    IMotionClock* rawClock = clock.get();

    Entry entry{QPointer<QQuickWindow>(window), std::move(clock), {}};
    // Eager eviction: when the window emits `destroyed`, drop the
    // entry synchronously on the GUI thread. This prevents the
    // address-reuse hazard (Qt recycling the QQuickWindow* for a
    // fresh window) AND lets consumers assume that after `destroyed`,
    // nobody is handing out a stale `IMotionClock*` to this window.
    //
    // DirectConnection because the signal fires from the QObject
    // destructor on the GUI thread — there is no event loop to
    // deliver a queued connection to, and we need the eviction to
    // happen before other GUI-thread teardown hooks run.
    entry.destroyedConnection = QObject::connect(
        window, &QObject::destroyed, window,
        [this, window](QObject*) {
            releaseClockFor(window);
        },
        Qt::DirectConnection);

    m_entries.emplace(window, std::move(entry));
    return rawClock;
}

void QtQuickClockManager::releaseClockFor(QQuickWindow* window)
{
    if (!window) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (auto it = m_entries.find(window); it != m_entries.end()) {
        // Disconnect the destroyed hook before erasing — we may have
        // been called FROM the destroyed signal (eager eviction) in
        // which case the connection is already auto-disconnected by
        // Qt, and the disconnect is a no-op. For the manual-release
        // path (tests, explicit teardown), this prevents a stale
        // lambda from firing later.
        QObject::disconnect(it->second.destroyedConnection);
        m_entries.erase(it);
    }
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
