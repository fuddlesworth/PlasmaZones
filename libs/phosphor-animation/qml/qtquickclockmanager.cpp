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
        // the QPointer inside the entry has gone null (window was
        // destroyed but the map entry wasn't evicted — the lazy
        // teardown path). Evict and fall through to the construction
        // branch, which will see the same (now stale) pointer and
        // build a fresh QtQuickClock bound to it. The fresh clock's
        // internal QPointer also tracks the stale window, so every
        // subsequent `now()` / `requestFrame()` lands on the null
        // branch inside the clock — safe but pointless. Real fix
        // (not here): callers shouldn't route animations to a
        // destroyed window. The manager's job is to not crash, not
        // to resurrect the window.
        //
        // Evicting keeps the map bounded under address-reuse: if the
        // same QQuickWindow* value is later reused for a *new* window
        // (Qt can and does recycle addresses), a fresh lookup gets a
        // fresh clock for the new window rather than the ghost of
        // the old one.
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
    m_entries.emplace(window, Entry{QPointer<QQuickWindow>(window), std::move(clock)});
    return rawClock;
}

void QtQuickClockManager::releaseClockFor(QQuickWindow* window)
{
    if (!window) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.erase(window);
}

int QtQuickClockManager::entryCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_entries.size());
}

void QtQuickClockManager::clearForTest()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
}

} // namespace PhosphorAnimation
