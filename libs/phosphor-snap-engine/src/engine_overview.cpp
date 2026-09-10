// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>

#include <QRect>
#include <QSet>

#include <utility>

namespace PhosphorSnapEngine {

using PhosphorEngine::OverviewWindowEntry;
using PhosphorEngine::PlacementStateKey;

std::optional<QList<OverviewWindowEntry>> SnapEngine::overviewWindowsFor(const PlacementStateKey& key) const
{
    // The empty screenId is the global holder's sentinel key. The holder keeps
    // per-app scalars and screenless float bookkeeping, never a workspace's
    // placement, so it is not a context the overview can show. Refusing it
    // here also keeps the holder's windows from leaking into a per-key read
    // whatever PerScreenStates happens to hold under that key.
    if (key.screenId.isEmpty()) {
        return std::nullopt;
    }
    const SnapState* state = m_states.stateForKey(key);
    if (!state) {
        return std::nullopt;
    }

    QList<OverviewWindowEntry> entries;
    QSet<QString> listed;

    // Residence-tagged windows first: every snapped window and every window
    // floated with a screen and desktop slot (setFloatingOnScreen, unsnapForFloat)
    // carries the tag, so this is the store's view of (screen, desktop). A tag
    // of 0 is the on-all-desktops sentinel: such a window shows on this
    // desktop too, and it lives in exactly one store, so it is listed here.
    QStringList resident = state->windowsOnScreenAndDesktop(key.screenId, key.desktop);
    resident += state->windowsOnScreenAndDesktop(key.screenId, 0);
    for (const QString& windowId : std::as_const(resident)) {
        if (listed.contains(windowId)) {
            continue;
        }
        OverviewWindowEntry entry;
        entry.windowId = windowId;
        const QStringList zoneIds = state->zonesForWindow(windowId);
        if (!zoneIds.isEmpty()) {
            // A spanned window's frame is the union of its zones. The rect stays
            // null when the tracker is absent or no zone resolves (no screen
            // geometry, or the layout no longer holds the zone); the daemon fills
            // a null rect from its tracked geometry.
            if (m_windowTracker) {
                QRect united;
                for (const QString& zoneId : zoneIds) {
                    const QRect zoneRect = m_windowTracker->zoneGeometry(zoneId, key.screenId);
                    if (!zoneRect.isValid()) {
                        continue;
                    }
                    united = united.isValid() ? united.united(zoneRect) : zoneRect;
                }
                entry.rect = united;
            }
        } else {
            // No zone: the store models a minimized window as a freed-zone float,
            // so the float bit is the whole story here and minimized stays false.
            entry.floating = state->isFloating(windowId);
        }
        entries.append(entry);
        listed.insert(windowId);
    }

    // A window floated through the screen-agnostic setFloating carries no
    // residence tag, so the scan above cannot see it. It still lives in THIS
    // store (the reverse map homed it here), so it belongs to this key unless
    // a tag pins it to some other desktop.
    const QStringList floating = state->floatingWindows();
    const QHash<QString, int>& desktops = state->desktopAssignments();
    for (const QString& windowId : floating) {
        if (listed.contains(windowId) || desktops.contains(windowId)) {
            continue;
        }
        OverviewWindowEntry entry;
        entry.windowId = windowId;
        entry.floating = true;
        entries.append(entry);
        listed.insert(windowId);
    }

    return entries;
}

} // namespace PhosphorSnapEngine
