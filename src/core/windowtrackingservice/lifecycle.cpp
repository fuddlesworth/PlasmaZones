// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Window lifecycle, layout change handling, state management, and private helpers.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include "../windowtrackingservice.h"
#include "../constants.h"
#include "../layout.h"
#include "../zone.h"
#include "../layoutmanager.h"
#include "../virtualdesktopmanager.h"
#include "../utils.h"
#include "../logging.h"
#include <QScreen>
#include <QUuid>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Window Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::windowClosed(const QString& windowId)
{
    QString appId = Utils::extractAppId(windowId);

    // Persist the zone assignment to pending BEFORE removing from active tracking.
    // This ensures the window can be restored to its zone when reopened.
    // BUT: Don't persist if the window is floating - floating windows should stay floating
    // and not be auto-snapped when reopened.
    QStringList zoneIds = m_windowZoneAssignments.value(windowId);
    QString zoneId = zoneIds.isEmpty() ? QString() : zoneIds.first();
    // Check floating with full windowId first, fallback to appId
    bool isFloating = isWindowFloating(windowId);
    if (!zoneId.isEmpty() && !zoneId.startsWith(QStringLiteral("zoneselector-")) && !isFloating) {
        if (!appId.isEmpty()) {
            PendingRestore entry;
            entry.zoneIds = zoneIds;

            QString screenName = m_windowScreenAssignments.value(windowId);
            entry.screenName = screenName;

            int desktop = m_windowDesktopAssignments.value(windowId, 0);
            if (desktop <= 0 && m_virtualDesktopManager) {
                desktop = m_virtualDesktopManager->currentDesktop();
            }
            entry.virtualDesktop = desktop;

            // Save the layout ID to ensure we only restore if the same layout is active
            // This prevents restoring windows to wrong zones when layouts have been changed
            // Use resolveLayoutForScreen() for proper multi-screen support
            Layout* contextLayout = m_layoutManager ? m_layoutManager->resolveLayoutForScreen(screenName) : nullptr;
            if (contextLayout) {
                entry.layoutId = contextLayout->id().toString();
            }

            // Save zone numbers for fallback when zone UUIDs get regenerated on layout edit
            QList<int> zoneNumbers;
            for (const QString& zId : zoneIds) {
                Zone* z = findZoneById(zId);
                if (z)
                    zoneNumbers.append(z->zoneNumber());
            }
            entry.zoneNumbers = zoneNumbers;

            m_pendingRestoreQueues[appId].append(entry);

            qCInfo(lcCore) << "Persisted zone" << zoneId << "for closed window" << appId << "screen:" << screenName
                           << "desktop:" << desktop
                           << "layout:" << (contextLayout ? contextLayout->id().toString() : QStringLiteral("none"))
                           << "zoneNumbers:" << zoneNumbers;
        }
    }

    // Now clean up active tracking state (but NOT pre-snap geometry -
    // that persists across close/reopen for proper session restore behavior)
    m_windowZoneAssignments.remove(windowId);
    m_windowScreenAssignments.remove(windowId);
    m_windowDesktopAssignments.remove(windowId);

    // Convert pre-tile geometry from full windowId to appId for persistence
    // so that when the window reopens (with a new internal ID), the geometry
    // can still be found via appId fallback. storePreTileGeometry writes both
    // keys, so we just clean up the stale full-windowId entry.
    if (m_preTileGeometries.contains(windowId) && appId != windowId) {
        m_preTileGeometries.remove(windowId);
    }
    // Clear floating state on close — floating is a runtime-only state that
    // should not carry over when the window is reopened. Without this, closing
    // a floated window and reopening it would inherit the float state (via appId
    // fallback), causing a spurious "floated" OSD and preventing auto-snap.
    m_floatingWindows.remove(windowId);
    if (appId != windowId) {
        m_floatingWindows.remove(appId);
    }
    // Also clear pre-float zone/screen assignments since float state is gone
    m_preFloatZoneAssignments.remove(windowId);
    m_preFloatScreenAssignments.remove(windowId);
    if (appId != windowId) {
        m_preFloatZoneAssignments.remove(appId);
        m_preFloatScreenAssignments.remove(appId);
    }
    // Remove autotile-floated tracking outright — do NOT migrate to appId.
    // This set is ephemeral (not persisted); migrating to appId would create
    // a shared key that matches ALL instances of the same app, causing
    // cross-contamination when isAutotileFloated() is called for other instances.
    m_autotileFloatedWindows.remove(windowId);
    m_savedSnapFloatingWindows.remove(windowId);
    m_windowStickyStates.remove(windowId);
    m_autoSnappedWindows.remove(windowId);
    m_effectReportedWindows.remove(windowId);

    scheduleSaveState();
}

void WindowTrackingService::onLayoutChanged()
{
    // Validate zone assignments against new layout
    Layout* newLayout = m_layoutManager->activeLayout();
    if (!newLayout) {
        qCInfo(lcCore) << "onLayoutChanged: no active layout, clearing buffer";
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
    // Include BOTH m_windowZoneAssignments (tracked) AND m_pendingRestoreQueues (session-restored
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
    qCDebug(lcCore) << "onLayoutChanged: newLayout=" << newLayout->name()
                    << "prevLayout=" << (prevLayout ? prevLayout->name() : QStringLiteral("null"))
                    << "switched=" << layoutSwitched << "windowAssignments=" << m_windowZoneAssignments.size();
    {
        QVector<ResnapEntry> newBuffer;
        QVector<Zone*> prevZones = prevLayout->zones();
        std::sort(prevZones.begin(), prevZones.end(), [](Zone* a, Zone* b) {
            return a->zoneNumber() < b->zoneNumber();
        });
        QHash<QString, int> zoneIdToPosition; // zoneId -> 1-based position
        for (int i = 0; i < prevZones.size(); ++i) {
            zoneIdToPosition[prevZones[i]->id().toString()] = i + 1;
        }
        // Dedup: full windowId for live assignments (supports multi-instance apps),
        // appId for pending entries (avoids double-counting live + pending for same window)
        QSet<QString> addedIds;

        auto addToBuffer = [&](const QString& windowIdOrStableId, const QStringList& zoneIdList,
                               const QString& screenName, int vd) {
            // Skip ALL floating windows. Floating persists across mode toggles —
            // floating windows should stay at their current position, not be resnapped.
            if (windowIdOrStableId.isEmpty() || isWindowFloating(windowIdOrStableId)) {
                return;
            }
            if (addedIds.contains(windowIdOrStableId)) {
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
            // Track by exact key (full windowId for live, appId for pending)
            addedIds.insert(windowIdOrStableId);
            // Also track appId so pending entries don't duplicate live ones
            QString appId = Utils::extractAppId(windowIdOrStableId);
            if (appId != windowIdOrStableId) {
                addedIds.insert(appId);
            }
            // Collect all zone positions for multi-zone resnap
            QList<int> allPositions;
            for (const QString& zid : zoneIdList) {
                int p = zoneIdToPosition.value(zid, 0);
                if (p > 0)
                    allPositions.append(p);
            }

            ResnapEntry entry;
            entry.windowId = windowIdOrStableId;
            entry.zonePosition = pos;
            entry.allZonePositions = allPositions;
            entry.screenId = screenName;
            entry.virtualDesktop = vd;
            newBuffer.append(entry);
        };

        const QUuid prevLayoutId = prevLayout->id();

        // Helper to check if ANY of a window's zones exist in the active layout.
        // Multi-zone windows are kept as long as at least one zone is valid.
        auto anyZoneInActiveLayout = [&](const QStringList& zoneIdList) {
            for (const QString& zid : zoneIdList) {
                if (activeLayoutZoneIds.contains(zid))
                    return true;
            }
            return false;
        };

        // Helper: is a window on a screen that uses the global active layout?
        // Windows on screens with per-screen assignments that differ from the
        // new active layout are unaffected by this layout change.
        auto isAffectedByGlobalChange = [&](const QString& windowScreen) -> bool {
            if (windowScreen.isEmpty())
                return true;
            Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(windowScreen);
            return !effectiveLayout || effectiveLayout == newLayout;
        };

        if (layoutSwitched) {
            // User switched layouts: capture assignments to zones from the OLD layout (not in new)
            // 1. Live assignments (windows we've tracked via windowSnapped)
            for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
                // Skip windows on screens with per-screen layouts unaffected by this change
                QString windowScreen = m_windowScreenAssignments.value(it.key());
                if (!isAffectedByGlobalChange(windowScreen)) {
                    continue;
                }
                if (anyZoneInActiveLayout(it.value())) {
                    continue;
                }
                addToBuffer(it.key(), it.value(), windowScreen, m_windowDesktopAssignments.value(it.key(), 0));
            }

            // 2. Pending assignments (session-restored windows)
            for (auto it = m_pendingRestoreQueues.constBegin(); it != m_pendingRestoreQueues.constEnd(); ++it) {
                for (const PendingRestore& entry : it.value()) {
                    if (!isAffectedByGlobalChange(entry.screenName)) {
                        continue;
                    }
                    if (anyZoneInActiveLayout(entry.zoneIds)) {
                        continue;
                    }
                    if (!entry.layoutId.isEmpty()) {
                        auto savedUuid = Utils::parseUuid(entry.layoutId);
                        if (!savedUuid || *savedUuid != prevLayoutId) {
                            continue; // pending is for a different layout
                        }
                    }
                    addToBuffer(it.key(), entry.zoneIds, entry.screenName, entry.virtualDesktop);
                }
            }
        } else {
            // Same layout (startup): capture assignments that belong to the current layout.
            // This lets resnap re-apply zone geometries for restored/pending windows.
            // 1. Live assignments in current layout
            for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
                if (!anyZoneInActiveLayout(it.value())) {
                    continue;
                }
                addToBuffer(it.key(), it.value(), m_windowScreenAssignments.value(it.key()),
                            m_windowDesktopAssignments.value(it.key(), 0));
            }

            // 2. Pending assignments for current layout
            for (auto it = m_pendingRestoreQueues.constBegin(); it != m_pendingRestoreQueues.constEnd(); ++it) {
                for (const PendingRestore& entry : it.value()) {
                    if (!anyZoneInActiveLayout(entry.zoneIds)) {
                        continue;
                    }
                    if (!entry.layoutId.isEmpty()) {
                        auto savedUuid = Utils::parseUuid(entry.layoutId);
                        if (!savedUuid || *savedUuid != prevLayoutId) {
                            continue;
                        }
                    }
                    addToBuffer(it.key(), entry.zoneIds, entry.screenName, entry.virtualDesktop);
                }
            }
        }

        if (!newBuffer.isEmpty()) {
            m_resnapBuffer = std::move(newBuffer);
            qCInfo(lcCore) << "Resnap buffer:" << m_resnapBuffer.size() << "windows (zone position -> window)";
            for (const ResnapEntry& e : m_resnapBuffer) {
                qCDebug(lcCore) << "Zone" << e.zonePosition << "<-" << e.windowId;
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
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        const QStringList& zoneIdList = it.value();
        if (zoneIdList.isEmpty()) {
            toRemove.append(it.key());
            continue;
        }
        QString windowScreen = m_windowScreenAssignments.value(it.key());

        // If this screen's assignment is autotile, preserve zone assignments for resnap
        auto cached = screenIsAutotile.constFind(windowScreen);
        if (cached == screenIsAutotile.constEnd()) {
            QString assignmentId =
                m_layoutManager->assignmentIdForScreen(windowScreen, currentDesktop, currentActivity);
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
        // Check if ANY assigned zone exists in the effective layout (not just
        // the primary). Multi-zone windows are kept as long as at least one
        // zone is valid — matches calculateResnapFromCurrentAssignments which
        // handles multi-zone via multiZoneGeometry.
        bool zoneFound = false;
        for (Zone* z : effectiveLayout->zones()) {
            const QString zid = z->id().toString();
            for (const QString& assignedZone : zoneIdList) {
                if (zid == assignedZone) {
                    zoneFound = true;
                    break;
                }
            }
            if (zoneFound)
                break;
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

void WindowTrackingService::setLastUsedZone(const QString& zoneId, const QString& screenName, const QString& zoneClass,
                                            int desktop)
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
        if (intersection.width() >= MinVisibleWidth && intersection.height() >= MinVisibleHeight) {
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
