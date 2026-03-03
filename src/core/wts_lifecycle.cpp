// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Window lifecycle, layout change handling, state management, and private helpers.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include "windowtrackingservice.h"
#include "constants.h"
#include "layout.h"
#include "zone.h"
#include "layoutmanager.h"
#include "virtualdesktopmanager.h"
#include "utils.h"
#include "logging.h"
#include <QScreen>
#include <QUuid>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Window Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::windowClosed(const QString& windowId)
{
    QString stableId = Utils::extractStableId(windowId);

    // Persist the zone assignment to pending BEFORE removing from active tracking.
    // This ensures the window can be restored to its zone when reopened.
    // BUT: Don't persist if the window is floating - floating windows should stay floating
    // and not be auto-snapped when reopened.
    QStringList zoneIds = m_windowZoneAssignments.value(windowId);
    QString zoneId = zoneIds.isEmpty() ? QString() : zoneIds.first();
    // Check floating with full windowId first, fallback to stableId
    bool isFloating = isWindowFloating(windowId);
    if (!zoneId.isEmpty() && !zoneId.startsWith(QStringLiteral("zoneselector-")) && !isFloating) {
        if (!stableId.isEmpty()) {
            m_pendingZoneAssignments[stableId] = zoneIds;

            QString screenName = m_windowScreenAssignments.value(windowId);
            if (!screenName.isEmpty()) {
                m_pendingZoneScreens[stableId] = screenName;
            } else {
                m_pendingZoneScreens.remove(stableId);
            }

            int desktop = m_windowDesktopAssignments.value(windowId, 0);
            if (desktop <= 0 && m_virtualDesktopManager) {
                desktop = m_virtualDesktopManager->currentDesktop();
            }
            if (desktop > 0) {
                m_pendingZoneDesktops[stableId] = desktop;
            } else {
                m_pendingZoneDesktops.remove(stableId);
            }

            // Save the layout ID to ensure we only restore if the same layout is active
            // This prevents restoring windows to wrong zones when layouts have been changed
            // Use resolveLayoutForScreen() for proper multi-screen support
            Layout* contextLayout = m_layoutManager
                ? m_layoutManager->resolveLayoutForScreen(screenName) : nullptr;
            if (contextLayout) {
                m_pendingZoneLayouts[stableId] = contextLayout->id().toString();
            } else {
                m_pendingZoneLayouts.remove(stableId);
            }

            // Save zone numbers for fallback when zone UUIDs get regenerated on layout edit
            QList<int> zoneNumbers;
            for (const QString& zId : zoneIds) {
                Zone* z = findZoneById(zId);
                if (z) zoneNumbers.append(z->zoneNumber());
            }
            if (!zoneNumbers.isEmpty()) {
                m_pendingZoneNumbers[stableId] = zoneNumbers;
            } else {
                m_pendingZoneNumbers.remove(stableId);
            }

            qCInfo(lcCore) << "Persisted zone" << zoneId << "for closed window" << stableId
                            << "screen:" << screenName << "desktop:" << desktop
                            << "layout:" << (contextLayout ? contextLayout->id().toString() : QStringLiteral("none"))
                            << "zoneNumbers:" << zoneNumbers;
        }
    }

    // Now clean up active tracking state (but NOT floating state or pre-snap geometry -
    // those persist across close/reopen for proper session restore behavior)
    m_windowZoneAssignments.remove(windowId);
    m_windowScreenAssignments.remove(windowId);
    m_windowDesktopAssignments.remove(windowId);

    // Convert pre-float entries from full window ID to stable ID so unfloating
    // after reopen works (new window instance will have a different pointer address).
    if (m_preFloatZoneAssignments.contains(windowId)) {
        m_preFloatZoneAssignments[stableId] = m_preFloatZoneAssignments.take(windowId);
    }
    if (m_preFloatScreenAssignments.contains(windowId)) {
        m_preFloatScreenAssignments[stableId] = m_preFloatScreenAssignments.take(windowId);
    }
    // Convert pre-snap geometry from full windowId to stableId for persistence
    // so that when the window reopens (with a new pointer address), the geometry
    // can still be found via stableId fallback
    if (m_preSnapGeometries.contains(windowId) && stableId != windowId) {
        m_preSnapGeometries[stableId] = m_preSnapGeometries.take(windowId);
    }
    // Convert pre-autotile geometry: remove full-windowId entry, keep stableId.
    // storePreAutotileGeometry writes both keys, so the stableId entry already
    // exists — just clean up the stale full-windowId entry.
    if (m_preAutotileGeometries.contains(windowId) && stableId != windowId) {
        m_preAutotileGeometries.remove(windowId);
    }
    // Convert floating state from full windowId to stableId for persistence
    if (m_floatingWindows.contains(windowId) && stableId != windowId) {
        m_floatingWindows.remove(windowId);
        m_floatingWindows.insert(stableId);
    }
    m_windowStickyStates.remove(windowId);
    m_autoSnappedWindows.remove(windowId);

    scheduleSaveState();
}

void WindowTrackingService::onLayoutChanged()
{
    // Validate zone assignments against new layout
    Layout* newLayout = m_layoutManager->activeLayout();
    if (!newLayout) {
        m_resnapBuffer.clear();
        return;
    }

    // Collect valid zone IDs from new active layout (for quick checks)
    QSet<QString> activeLayoutZoneIds;
    for (Zone* zone : newLayout->zones()) {
        activeLayoutZoneIds.insert(zone->id().toString());
    }

    // Before removing stale assignments, capture (window, zonePosition) for resnap-to-new-layout.
    // When user presses the shortcut, we map zone N -> zone N (with cycling when layout has fewer zones).
    // Include BOTH m_windowZoneAssignments (tracked) AND m_pendingZoneAssignments (session-restored
    // windows that KWin placed in zones before we got windowSnapped - e.g. after login).
    //
    // LayoutManager ensures prevLayout is never null (captures current as previous on first set).
    // When prevLayout != newLayout: capture assignments to OLD layout (real switch).
    // When prevLayout == newLayout: capture assignments to CURRENT layout (startup re-apply).
    //
    // Only replace m_resnapBuffer when we capture at least one window. If user does A->B->C (snapped
    // on A, B has no windows), prev=B yields nothing - we keep the buffer from A->B so resnap on C works.
    Layout* prevLayout = m_layoutManager->previousLayout();
    const bool layoutSwitched = (prevLayout != newLayout);
    {
        QVector<ResnapEntry> newBuffer;
        QVector<Zone*> prevZones = prevLayout->zones();
        std::sort(prevZones.begin(), prevZones.end(), [](Zone* a, Zone* b) {
            return a->zoneNumber() < b->zoneNumber();
        });
        QHash<QString, int> zoneIdToPosition; // zoneId -> 1-based position
        for (int i = 0; i < prevZones.size(); ++i) {
            zoneIdToPosition[prevZones[i]->id().toString()] = i + 1;
            QString withoutBraces = prevZones[i]->id().toString(QUuid::WithoutBraces);
            if (withoutBraces != prevZones[i]->id().toString()) {
                zoneIdToPosition[withoutBraces] = i + 1;
            }
        }
        QSet<QString> addedStableIds; // avoid duplicates when window is in both live and pending

        auto addToBuffer = [&](const QString& windowIdOrStableId, const QStringList& zoneIdList,
                               const QString& screenName, int vd) {
            QString stableId = Utils::extractStableId(windowIdOrStableId);
            if (stableId.isEmpty() || isWindowFloating(windowIdOrStableId) || addedStableIds.contains(stableId)) {
                return;
            }
            // Use primary zone for position mapping
            QString zoneId = zoneIdList.isEmpty() ? QString() : zoneIdList.first();
            int pos = zoneIdToPosition.value(zoneId, 0);
            if (pos <= 0) {
                // Handle zoneselector synthetic IDs: "zoneselector-{layoutId}-{index}"
                if (zoneId.startsWith(QStringLiteral("zoneselector-"))) {
                    int lastDash = zoneId.lastIndexOf(QStringLiteral("-"));
                    if (lastDash > 0) {
                        bool ok = false;
                        int idx = zoneId.mid(lastDash + 1).toInt(&ok);
                        if (ok && idx >= 0 && idx < prevZones.size()) {
                            pos = idx + 1; // 1-based position
                        }
                    }
                }
            }
            if (pos <= 0) {
                return;
            }
            addedStableIds.insert(stableId);
            ResnapEntry entry;
            entry.windowId = stableId; // KWin effect's buildWindowMap keys by stableId
            entry.zonePosition = pos;
            entry.screenId = screenName;
            entry.virtualDesktop = vd;
            newBuffer.append(entry);
        };

        const QUuid prevLayoutId = prevLayout->id();

        // Helper to check if a window's primary zone is valid in the active layout
        auto primaryZoneInActiveLayout = [&](const QStringList& zoneIdList) {
            if (zoneIdList.isEmpty()) return false;
            return activeLayoutZoneIds.contains(zoneIdList.first());
        };

        // Helper: is a window on a screen that uses the global active layout?
        // Windows on screens with per-screen assignments that differ from the
        // new active layout are unaffected by this layout change.
        auto isAffectedByGlobalChange = [&](const QString& windowScreen) -> bool {
            if (windowScreen.isEmpty()) return true;
            Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(windowScreen);
            return !effectiveLayout || effectiveLayout == newLayout;
        };

        if (layoutSwitched) {
            // User switched layouts: capture assignments to zones from the OLD layout (not in new)
            // 1. Live assignments (windows we've tracked via windowSnapped)
            for (auto it = m_windowZoneAssignments.constBegin();
                 it != m_windowZoneAssignments.constEnd(); ++it) {
                // Skip windows on screens with per-screen layouts unaffected by this change
                QString windowScreen = m_windowScreenAssignments.value(it.key());
                if (!isAffectedByGlobalChange(windowScreen)) {
                    continue;
                }
                if (primaryZoneInActiveLayout(it.value())) {
                    continue;
                }
                addToBuffer(it.key(), it.value(), windowScreen,
                            m_windowDesktopAssignments.value(it.key(), 0));
            }

            // 2. Pending assignments (session-restored windows)
            for (auto it = m_pendingZoneAssignments.constBegin();
                 it != m_pendingZoneAssignments.constEnd(); ++it) {
                QString screenName = m_pendingZoneScreens.value(it.key());
                if (!isAffectedByGlobalChange(screenName)) {
                    continue;
                }
                if (primaryZoneInActiveLayout(it.value())) {
                    continue;
                }
                QString savedLayoutId = m_pendingZoneLayouts.value(it.key());
                if (!savedLayoutId.isEmpty()) {
                    auto savedUuid = Utils::parseUuid(savedLayoutId);
                    if (!savedUuid || *savedUuid != prevLayoutId) {
                        continue; // pending is for a different layout
                    }
                }
                int vd = m_pendingZoneDesktops.value(it.key(), 0);
                addToBuffer(it.key(), it.value(), screenName, vd);
            }
        } else {
            // Same layout (startup): capture assignments that belong to the current layout.
            // This lets resnap re-apply zone geometries for restored/pending windows.
            // 1. Live assignments in current layout
            for (auto it = m_windowZoneAssignments.constBegin();
                 it != m_windowZoneAssignments.constEnd(); ++it) {
                if (!primaryZoneInActiveLayout(it.value())) {
                    continue;
                }
                addToBuffer(it.key(), it.value(),
                            m_windowScreenAssignments.value(it.key()),
                            m_windowDesktopAssignments.value(it.key(), 0));
            }

            // 2. Pending assignments for current layout
            for (auto it = m_pendingZoneAssignments.constBegin();
                 it != m_pendingZoneAssignments.constEnd(); ++it) {
                if (!primaryZoneInActiveLayout(it.value())) {
                    continue;
                }
                QString savedLayoutId = m_pendingZoneLayouts.value(it.key());
                if (!savedLayoutId.isEmpty()) {
                    auto savedUuid = Utils::parseUuid(savedLayoutId);
                    if (!savedUuid || *savedUuid != prevLayoutId) {
                        continue;
                    }
                }
                QString screenName = m_pendingZoneScreens.value(it.key());
                int vd = m_pendingZoneDesktops.value(it.key(), 0);
                addToBuffer(it.key(), it.value(), screenName, vd);
            }
        }

        if (!newBuffer.isEmpty()) {
            m_resnapBuffer = std::move(newBuffer);
            qCInfo(lcCore) << "Resnap buffer:" << m_resnapBuffer.size()
                          << "windows (zone position -> window)";
            for (const ResnapEntry& e : m_resnapBuffer) {
                qCDebug(lcCore) << "  Zone" << e.zonePosition << "<-" << e.windowId;
            }
        }
    }

    // Remove stale assignments: check each window against its screen's effective layout
    // (not just the global active), so per-screen assignments aren't incorrectly purged.
    // Skip windows on autotile screens — their zone assignments must survive the
    // autotile period so resnapCurrentAssignments() can restore them when tiling is toggled off.
    const int currentDesktop = m_layoutManager->currentVirtualDesktop();
    const QString currentActivity = m_layoutManager->currentActivity();

    // Cache autotile status per screen to avoid redundant lookups (O(screens) instead of O(windows))
    QHash<QString, bool> screenIsAutotile;

    QStringList toRemove;
    for (auto it = m_windowZoneAssignments.constBegin();
         it != m_windowZoneAssignments.constEnd(); ++it) {
        const QStringList& zoneIdList = it.value();
        if (zoneIdList.isEmpty()) {
            toRemove.append(it.key());
            continue;
        }
        QString windowScreen = m_windowScreenAssignments.value(it.key());

        // If this screen's assignment is autotile, preserve zone assignments for resnap
        auto cached = screenIsAutotile.constFind(windowScreen);
        if (cached == screenIsAutotile.constEnd()) {
            QString assignmentId = m_layoutManager->assignmentIdForScreen(windowScreen, currentDesktop, currentActivity);
            cached = screenIsAutotile.insert(windowScreen, LayoutId::isAutotile(assignmentId));
        }
        if (*cached) {
            continue;
        }

        Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(windowScreen);
        if (!effectiveLayout) {
            toRemove.append(it.key());
            continue;
        }
        bool zoneFound = false;
        for (Zone* z : effectiveLayout->zones()) {
            if (z->id().toString() == zoneIdList.first()) {
                zoneFound = true;
                break;
            }
        }
        if (!zoneFound) {
            toRemove.append(it.key());
        }
    }

    for (const QString& windowId : toRemove) {
        unassignWindow(windowId);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// State Management (persistence handled by adaptor via KConfig)
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::scheduleSaveState()
{
    // Signal to adaptor that state changed and needs saving
    // Adaptor handles actual KConfig persistence
    Q_EMIT stateChanged();
}

void WindowTrackingService::setLastUsedZone(const QString& zoneId, const QString& screenName,
                                             const QString& zoneClass, int desktop)
{
    m_lastUsedZoneId = zoneId;
    m_lastUsedScreenName = screenName;
    m_lastUsedZoneClass = zoneClass;
    m_lastUsedDesktop = desktop;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private Helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool WindowTrackingService::isGeometryOnScreen(const QRect& geometry) const
{
    for (QScreen* screen : Utils::allScreens()) {
        QRect intersection = geometry.intersected(screen->geometry());
        if (intersection.width() >= MinVisibleWidth &&
            intersection.height() >= MinVisibleHeight) {
            return true;
        }
    }
    return false;
}

QRect WindowTrackingService::adjustGeometryToScreen(const QRect& geometry) const
{
    // Find nearest screen
    QScreen* nearest = Utils::findNearestScreen(geometry.center());
    if (!nearest) {
        return geometry;
    }

    QRect screenGeo = nearest->geometry();
    QRect adjusted = geometry;

    // Clamp to screen bounds while preserving size where possible
    if (adjusted.right() > screenGeo.right()) {
        adjusted.moveRight(screenGeo.right());
    }
    if (adjusted.left() < screenGeo.left()) {
        adjusted.moveLeft(screenGeo.left());
    }
    if (adjusted.bottom() > screenGeo.bottom()) {
        adjusted.moveBottom(screenGeo.bottom());
    }
    if (adjusted.top() < screenGeo.top()) {
        adjusted.moveTop(screenGeo.top());
    }

    return adjusted;
}

Zone* WindowTrackingService::findZoneById(const QString& zoneId) const
{
    auto uuidOpt = Utils::parseUuid(zoneId);
    if (!uuidOpt) {
        return nullptr;
    }

    // Search all layouts, not just the active one, to support per-screen layouts
    for (Layout* layout : m_layoutManager->layouts()) {
        Zone* zone = layout->zoneById(*uuidOpt);
        if (zone) {
            return zone;
        }
    }
    return nullptr;
}


} // namespace PlasmaZones
