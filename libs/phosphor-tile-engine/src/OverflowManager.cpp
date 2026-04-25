// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Project headers
#include <PhosphorTileEngine/OverflowManager.h>

// KDE/Qt logging
#include "tileenginelogging.h"

namespace PlasmaZones {

void OverflowManager::markOverflow(const QString& windowId, const QString& screenId)
{
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }

    // If the window was previously tracked on a different screen, remove the
    // stale entry to avoid ghost references in the old screen's set.
    auto it = m_windowToScreen.find(windowId);
    if (it != m_windowToScreen.end() && it.value() != screenId) {
        auto sit = m_overflow.find(it.value());
        if (sit != m_overflow.end()) {
            sit->remove(windowId);
            if (sit->isEmpty()) {
                m_overflow.erase(sit);
            }
        }
    }
    m_overflow[screenId].insert(windowId);
    m_windowToScreen[windowId] = screenId;
}

void OverflowManager::clearOverflow(const QString& windowId)
{
    auto it = m_windowToScreen.find(windowId);
    if (it == m_windowToScreen.end()) {
        return;
    }
    const QString screenId = it.value();
    m_windowToScreen.erase(it);

    auto sit = m_overflow.find(screenId);
    if (sit != m_overflow.end()) {
        sit->remove(windowId);
        if (sit->isEmpty()) {
            m_overflow.erase(sit);
        }
    }
}

bool OverflowManager::isOverflow(const QString& windowId) const
{
    return m_windowToScreen.contains(windowId);
}

QStringList OverflowManager::applyOverflow(const QString& screenId, const QStringList& windows, int tileCount)
{
    if (screenId.isEmpty()) {
        return {};
    }

    QStringList newlyOverflowed;
    for (int i = tileCount; i < windows.size(); ++i) {
        const QString& wid = windows[i];
        if (!isOverflow(wid)) {
            markOverflow(wid, screenId);
            newlyOverflowed.append(wid);
            qCInfo(PhosphorTileEngine::lcTileEngine) << "Overflow: tracking window" << wid << "on screen" << screenId;
        }
    }
    return newlyOverflowed;
}

QStringList OverflowManager::recoverIfRoom(const QString& screenId, int tiledCount, int maxWindows,
                                           const std::function<bool(const QString&)>& isFloating,
                                           const std::function<bool(const QString&)>& containsWindow)
{
    if (m_overflow.isEmpty()) {
        return {};
    }

    int room = maxWindows - tiledCount;
    if (room <= 0) {
        return {};
    }

    auto sit = m_overflow.find(screenId);
    if (sit == m_overflow.end() || sit->isEmpty()) {
        return {};
    }

    // Collect candidates first (avoid mutating during iteration).
    // Also detect stale entries: overflow windows that are no longer floating
    // or no longer in the PhosphorTiles::TilingState should be purged to prevent them
    // from getting stuck in the overflow set permanently.
    QStringList candidates;
    QStringList stale;
    for (const QString& wid : std::as_const(*sit)) {
        if (!containsWindow(wid)) {
            // Window removed from state entirely — stale entry
            stale.append(wid);
        } else if (isFloating(wid)) {
            candidates.append(wid);
        } else {
            // Window is in state but not floating — externally unfloated, purge
            stale.append(wid);
        }
    }

    // Purge stale entries
    for (const QString& wid : stale) {
        sit->remove(wid);
        m_windowToScreen.remove(wid);
        qCDebug(PhosphorTileEngine::lcTileEngine) << "Overflow: purged stale entry" << wid << "on screen" << screenId;
    }

    // Return candidates up to available room — caller performs state mutations
    QStringList toRecover;
    for (const QString& wid : candidates) {
        if (room <= 0) {
            break;
        }
        sit->remove(wid);
        m_windowToScreen.remove(wid);
        toRecover.append(wid);
        qCInfo(PhosphorTileEngine::lcTileEngine) << "Overflow: recovering window" << wid << "on screen" << screenId;
        --room;
    }

    if (sit->isEmpty()) {
        m_overflow.erase(sit);
    }

    return toRecover;
}

QSet<QString> OverflowManager::takeForScreen(const QString& screenId)
{
    QSet<QString> result = m_overflow.take(screenId);
    for (const QString& wid : result) {
        m_windowToScreen.remove(wid);
    }
    return result;
}

void OverflowManager::migrateWindow(const QString& windowId)
{
    auto it = m_windowToScreen.find(windowId);
    if (it == m_windowToScreen.end()) {
        return; // Not an overflow window
    }

    // Use the reverse index to find the correct per-screen set (authoritative
    // for overflow tracking, even if the engine's m_windowToScreen diverged).
    const QString trackedScreen = it.value();
    auto sit = m_overflow.find(trackedScreen);
    if (sit != m_overflow.end()) {
        sit->remove(windowId);
        if (sit->isEmpty()) {
            m_overflow.erase(sit);
        }
    }

    // Remove from reverse index (migration clears overflow status)
    m_windowToScreen.erase(it);
}

void OverflowManager::clearForRemovedScreens(const QSet<QString>& activeScreens)
{
    QStringList toRemove;
    for (auto it = m_windowToScreen.constBegin(); it != m_windowToScreen.constEnd(); ++it) {
        if (!activeScreens.contains(it.value())) {
            toRemove.append(it.key());
        }
    }
    for (const QString& wid : toRemove) {
        clearOverflow(wid);
    }
}

bool OverflowManager::isEmpty() const
{
    return m_windowToScreen.isEmpty();
}

} // namespace PlasmaZones
