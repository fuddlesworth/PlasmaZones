// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Window lifecycle, layout change handling, state management, and private helpers.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include <PhosphorPlacement/WindowTrackingService.h>
#include "placementutils.h"
#include "placementvalidation_p.h"

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/VirtualScreen.h>
#include "placementlogging.h"
#include <QScreen>
#include <QUuid>
#include <climits>

namespace PhosphorPlacement {

namespace {

static QRect clampToRect(const QRect& geometry, const QRect& bounds)
{
    QRect adjusted = geometry;
    if (adjusted.right() > bounds.right()) {
        adjusted.moveRight(bounds.right());
    }
    if (adjusted.left() < bounds.left()) {
        adjusted.moveLeft(bounds.left());
    }
    if (adjusted.bottom() > bounds.bottom()) {
        adjusted.moveBottom(bounds.bottom());
    }
    if (adjusted.top() < bounds.top()) {
        adjusted.moveTop(bounds.top());
    }
    return adjusted;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Window Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::windowClosed(const QString& windowId, PhosphorEngine::WindowKind kind)
{
    if (!hasSnapState())
        return;
    PhosphorSnapEngine::SnapState* snapState = snapForWindow(windowId);

    // Query the registry-aware helper so a window that renamed mid-session
    // (Electron/CEF) lands in the restore queue under its CURRENT class,
    // not the first-seen one.
    QString appId = currentAppIdFor(windowId);

    // Persist the zone assignment to pending BEFORE removing from active tracking.
    // This ensures the window can be restored to its zone when reopened.
    // BUT: Don't persist if the window is floating - floating windows should stay floating
    // and not be auto-snapped when reopened.
    QStringList zoneIds = snapState ? snapState->zonesForWindow(windowId) : QStringList{};
    QString zoneId = zoneIds.isEmpty() ? QString() : zoneIds.first();
    // Check floating with full windowId first, fallback to appId
    bool isFloating = isWindowFloating(windowId);
    if (!zoneId.isEmpty() && !zoneId.startsWith(kZoneSelectorIdPrefix)
        && !isFloating
        // A whitespace-only / whitespace-bearing appId is a corrupt window
        // identity (KWin reported a blank class). Persisting a PendingRestore
        // under it pollutes the restore queue with a key no real window
        // matches cleanly — and a blank " " key is then consumed
        // indiscriminately by every later blank-class window. Drop it.
        && PhosphorIdentity::WindowId::isValidAppId(appId)) {
        const QString screenId = snapState ? snapState->screenForWindow(windowId) : QString();
        // SnapState::desktopForWindow returns 0 (all-desktops / unknown
        // sentinel) for untracked windows. The disabled-context predicate
        // short-circuits its desktop-disabled check on desktop <= 0, so
        // sticky and untracked windows fall through the monitor-disabled
        // gate only — intentional: a sticky window isn't "on" any one
        // desktop. The restore path treats 0 as "don't constrain on
        // desktop" too, so a value of 0 carried into the PendingRestore
        // entry won't migrate the window to a wrong desktop on reopen.
        const int desktop = snapState ? snapState->desktopForWindow(windowId) : 0;

        // Disabled-context gate (discussion #461 item 2). When the closing
        // window lives on a monitor/desktop the user disabled snap for,
        // recording a PendingRestore turns into a delayed footgun: either
        // the same app reopens on the disabled screen and the snap restore
        // path (gated on destination only) drags it to the saved zone, or
        // KWin rehomes the window onto a surviving monitor after the
        // disabled screen sleeps and the same restore fires there. The fix
        // is to not record the entry at all — the snap-restore path
        // already returns noSnap when the queue is empty.
        //
        // The predicate only runs when we have a screenId to evaluate. If
        // SnapState pruned the window's screen before windowClosed ran
        // (screen disconnect race) the gate cannot apply, so we persist
        // unconditionally — pre-3.0 behaviour for untracked windows.
        const bool gateApplies = !screenId.isEmpty() && m_shouldTrackPredicate;
        if (gateApplies && !m_shouldTrackPredicate(screenId, desktop)) {
            qCDebug(lcPlacement) << "Skipped PendingRestore for closed window" << appId
                                 << "-- disabled context, screen:" << screenId << "desktop:" << desktop;
        } else {
            if (screenId.isEmpty()) {
                qCDebug(lcPlacement) << "PendingRestore gate: empty screenId for closed window" << appId
                                     << "-- persisting unconditionally";
            }
            PendingRestore entry;
            entry.zoneIds = zoneIds;
            entry.screenId = screenId;
            entry.virtualDesktop = desktop;
            entry.windowKind = kind;

            // Save the layout ID to ensure we only restore if the same layout is active
            // This prevents restoring windows to wrong zones when layouts have been changed
            // Use resolveLayoutForScreen() for proper multi-screen support
            PhosphorZones::Layout* contextLayout =
                m_layoutManager ? m_layoutManager->resolveLayoutForScreen(screenId) : nullptr;
            if (contextLayout) {
                entry.layoutId = contextLayout->id().toString();
            }

            // Save zone numbers for fallback when zone UUIDs get regenerated on layout edit
            QList<int> zoneNumbers;
            for (const QString& zId : zoneIds) {
                PhosphorZones::Zone* z = findZoneById(zId);
                if (z)
                    zoneNumbers.append(z->zoneNumber());
            }
            entry.zoneNumbers = zoneNumbers;

            m_pendingRestoreQueues[appId].append(entry);

            qCInfo(lcPlacement) << "Persisted zone" << zoneId << "for closed window" << appId << "screen:" << screenId
                                << "desktop:" << desktop << "layout:"
                                << (contextLayout ? contextLayout->id().toString() : QStringLiteral("none"))
                                << "zoneNumbers:" << zoneNumbers;
        }
    }

    // Order matters: zoneIds/screenId/desktop are read above BEFORE this call
    // clears them in SnapState. Do not move reads below this line.
    if (snapState) {
        snapState->windowClosed(windowId);
    }
    if (m_snapResolver.forgetWindow) {
        m_snapResolver.forgetWindow(windowId);
    }

    // No manual full-windowId→appId float-back copy is needed: the unified record's
    // freeGeometry rides on the record itself, which is stored in its appId bucket,
    // so the appId fallback (peek/take) finds it on reopen automatically.
    // The authoritative engine float bit was already cleared by
    // snapState->windowClosed above (and AutotileEngine::windowClosed clears
    // its own); here we only clear the LEGACY fallback float set + its appId
    // aliases — floating is a runtime-only state that must not carry over when
    // the window is reopened. Without this, closing a floated window and
    // reopening it would inherit the float state (via appId fallback), causing
    // a spurious "floated" OSD and preventing auto-snap.
    m_floatingWindows.remove(windowId);
    if (appId != windowId) {
        m_floatingWindows.remove(appId);
    }
    // Also clear pre-float zone/screen assignments since float state is gone.
    // SnapState is the authoritative store; clear both windowId and appId keys.
    clearPreFloatZone(windowId);
    // Remove sticky-window tracking outright — do NOT migrate to appId. The
    // sticky map is keyed on the canonical (first-seen) composite, so remove
    // under it (issue #628).
    m_windowStickyStates.remove(canonicalizeForLookup(windowId));

    scheduleSaveState();
}

void WindowTrackingService::onLayoutChanged()
{
    if (!hasSnapState() || !m_layoutManager)
        return;

    // Validate zone assignments against new layout.
    // NOTE: Do NOT early-return when the global activeLayout() is null. With virtual
    // screens, individual screens may have per-screen layouts via resolveLayoutForScreen().
    // The per-window stale-assignment loop further down resolves layouts
    // per-screen via resolveLayoutForScreen(), so a null global layout does not
    // mean "no layouts anywhere".
    PhosphorZones::Layout* newLayout = m_layoutManager->activeLayout();

    // Before removing stale assignments, capture (window, zonePosition) for resnap-to-new-layout.
    // The resnap maps a window's primary zone N -> zone N in the new layout; a window whose
    // position exceeds the new layout's zone count is restored to its pre-tile geometry (see
    // SnapEngine::calculateResnapFromPreviousLayout).
    // Include BOTH m_windowZoneAssignments (tracked) AND m_pendingRestoreQueues (session-restored
    // windows that KWin placed in zones before we got windowSnapped - e.g. after login).
    //
    // PhosphorZones::LayoutRegistry ensures prevLayout is never null (captures current as previous on first set).
    // When prevLayout != newLayout: capture assignments to OLD layout (real switch).
    // When prevLayout == newLayout: capture assignments to CURRENT layout (startup re-apply).
    //
    // Only replace m_resnapBuffer when we capture at least one window. If user does A->B->C (snapped
    // on A, B has no windows), prev=B yields nothing - we keep the buffer from A->B so resnap on C works.
    PhosphorZones::Layout* prevLayout = m_layoutManager->previousLayout();
    // Resnap-buffer construction requires a previous layout to map zone
    // positions from. On first launch (no prevLayout) skip just the buffer
    // build — but DO NOT return: the stale-assignment cleanup further down
    // still has to run, otherwise session-restored windows whose zoneIds
    // reference a since-deleted layout would stay in the snap store forever
    // (the only other purge path is windowClosed, which doesn't fire for
    // assignments restored at startup).
    const bool layoutSwitched = prevLayout && (prevLayout != newLayout);
    if (prevLayout) {
        qCDebug(lcPlacement) << "onLayoutChanged: newLayout="
                             << (newLayout ? newLayout->name() : QStringLiteral("null"))
                             << "prevLayout=" << prevLayout->name() << "switched=" << layoutSwitched
                             << "snappedWindows=" << snappedWindows().size();
    } else {
        qCDebug(lcPlacement) << "onLayoutChanged: no previous layout (first launch); skipping resnap-buffer "
                                "build, running stale-assignment cleanup only";
    }
    if (prevLayout) {
        QVector<ResnapEntry> newBuffer;

        // Build the position map from EVERY loaded layout, not just
        // prevLayout. The window's stored zoneIds came from whatever
        // layout was the cascade winner at the time it snapped — which
        // can differ from prevLayout when the user has per-screen /
        // per-context entries. Restricting to prevLayout's zones broke
        // the resnap path on promote edits: clearing shadows shifts the
        // cascade winner from a per-context entry (whose layout differs
        // from the global active) to the screen-level slot, but the
        // window is still in the old per-context layout's zones. With
        // only prevLayout in the map, the lookup misses, addToBuffer
        // returns early, and the resnap buffer ends up empty.
        // Zone UUIDs are unique across all layouts, so this is safe.
        const QHash<QString, int> globalZoneIdToPosition =
            PhosphorZones::LayoutUtils::buildGlobalZonePositionMap(m_layoutManager->layouts());
        const int globalPrevZoneCount = prevLayout->zones().size();

        // Dedup: full windowId for live assignments (supports multi-instance apps),
        // appId for pending entries (avoids double-counting live + pending for same window)
        QSet<QString> addedIds;

        auto addToBuffer = [&](const QString& windowIdOrStableId, const QStringList& zoneIdList,
                               const QString& screenId, int vd) {
            // Skip ALL floating windows. Floating persists across mode toggles —
            // floating windows should stay at their current position, not be resnapped.
            if (windowIdOrStableId.isEmpty() || isWindowFloating(windowIdOrStableId)) {
                return;
            }
            if (addedIds.contains(windowIdOrStableId)) {
                return;
            }

            // Use the all-layouts position map built above. The previous
            // per-screen-layout branch resolved the screen's layout AFTER
            // the change had landed, which yielded the new layout and made
            // the lookup miss for OLD zoneIds. The all-layouts map handles
            // every layout the window could have snapped to. The @p screenId
            // parameter is still consumed below when constructing the
            // ResnapEntry.

            // Use primary zone for position mapping
            QString zoneId = zoneIdList.isEmpty() ? QString() : zoneIdList.first();
            int pos = globalZoneIdToPosition.value(zoneId, 0);
            if (pos <= 0) {
                // Handle zoneselector synthetic IDs: "zoneselector-{layoutId}-{index}"
                if (zoneId.startsWith(kZoneSelectorIdPrefix)) {
                    int lastDash = zoneId.lastIndexOf(QLatin1Char('-'));
                    if (lastDash > 0) {
                        bool ok = false;
                        int idx = zoneId.mid(lastDash + 1).toInt(&ok);
                        if (ok && idx >= 0 && idx < globalPrevZoneCount) {
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
            // Also track appId so pending entries don't duplicate live ones.
            // Registry-aware so a renamed window dedups against its CURRENT class.
            QString appId = currentAppIdFor(windowIdOrStableId);
            if (!appId.isEmpty() && appId != windowIdOrStableId) {
                addedIds.insert(appId);
            }
            ResnapEntry entry;
            entry.windowId = windowIdOrStableId;
            entry.zonePosition = pos;
            entry.screenId = screenId;
            entry.virtualDesktop = vd;
            newBuffer.append(entry);
        };

        const QUuid prevLayoutId = prevLayout->id();

        // Resolve the effective new layout for a given screen. Per-screen
        // assignments take precedence; windows with no screen (or screens
        // without an explicit assignment) fall back to the active layout.
        auto resolveNewLayoutForScreen = [&](const QString& screenId) -> PhosphorZones::Layout* {
            if (!screenId.isEmpty()) {
                PhosphorZones::Layout* perScreen = m_layoutManager->resolveLayoutForScreen(screenId);
                if (perScreen)
                    return perScreen;
            }
            return newLayout;
        };

        if (layoutSwitched) {
            // User switched layouts: capture assignments to zones from the OLD layout (not in new)
            // 1. Live assignments (windows we've tracked via windowSnapped)
            forEachZoneAssignedWindow(
                [&](const QString& windowId, const QStringList& zoneIds, const QString& windowScreen, int desktop) {
                    PhosphorZones::Layout* effectiveLayout = resolveNewLayoutForScreen(windowScreen);
                    if (anyZoneExistsInLayout(zoneIds, effectiveLayout)) {
                        return;
                    }
                    addToBuffer(windowId, zoneIds, windowScreen, desktop);
                });

            // 2. Pending assignments (session-restored windows)
            for (auto it = m_pendingRestoreQueues.constBegin(); it != m_pendingRestoreQueues.constEnd(); ++it) {
                for (const PendingRestore& entry : it.value()) {
                    PhosphorZones::Layout* effectiveLayout = resolveNewLayoutForScreen(entry.screenId);
                    if (anyZoneExistsInLayout(entry.zoneIds, effectiveLayout)) {
                        continue;
                    }
                    if (!entry.layoutId.isEmpty()) {
                        auto savedUuid = parseUuid(entry.layoutId);
                        if (!savedUuid || *savedUuid != prevLayoutId) {
                            continue; // pending is for a different layout
                        }
                    }
                    addToBuffer(it.key(), entry.zoneIds, entry.screenId, entry.virtualDesktop);
                }
            }
        } else {
            // Same layout (startup): capture assignments that belong to their screen's
            // effective layout. This lets resnap re-apply zone geometries for both
            // global and per-screen layout windows.
            // 1. Live assignments — check each window against its own screen's layout
            forEachZoneAssignedWindow(
                [&](const QString& windowId, const QStringList& zoneIds, const QString& windowScreen, int desktop) {
                    PhosphorZones::Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(windowScreen);
                    if (!anyZoneExistsInLayout(zoneIds, effectiveLayout)) {
                        return;
                    }
                    addToBuffer(windowId, zoneIds, windowScreen, desktop);
                });

            // 2. Pending assignments — check against each entry's screen's layout
            for (auto it = m_pendingRestoreQueues.constBegin(); it != m_pendingRestoreQueues.constEnd(); ++it) {
                for (const PendingRestore& entry : it.value()) {
                    PhosphorZones::Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(entry.screenId);
                    if (!anyZoneExistsInLayout(entry.zoneIds, effectiveLayout)) {
                        continue;
                    }
                    if (!entry.layoutId.isEmpty()) {
                        auto savedUuid = parseUuid(entry.layoutId);
                        if (!savedUuid || *savedUuid != prevLayoutId) {
                            continue;
                        }
                    }
                    addToBuffer(it.key(), entry.zoneIds, entry.screenId, entry.virtualDesktop);
                }
            }
        }

        if (!newBuffer.isEmpty()) {
            m_resnapBuffer = std::move(newBuffer);
            qCInfo(lcPlacement) << "Resnap buffer:" << m_resnapBuffer.size() << "windows (zone position -> window)";
            for (const ResnapEntry& e : m_resnapBuffer) {
                qCDebug(lcPlacement) << "Zone" << e.zonePosition << "<-" << e.windowId;
            }
        }
    }

    // Remove stale assignments: check each window against its screen's effective layout
    // (not just the global active), so per-screen assignments aren't incorrectly purged.
    // Skip windows on autotile screens — their zone assignments must survive the
    // autotile period so resnapCurrentAssignments() can restore them when tiling is toggled off.
    // Skip windows on OTHER virtual desktops — their zone assignments belong to that
    // desktop's layout and must not be purged when the current desktop's layout changes.
    const QString currentActivity = m_layoutManager->currentActivity();

    // Cache autotile status per screen to avoid redundant lookups (O(screens) instead of O(windows))
    QHash<QString, bool> screenIsAutotile;

    QStringList toRemove;
    // Multi-zone windows where SOME zones survived the layout change: we
    // keep the window, but rewrite the assignment to drop the dangling zone
    // ids. Without this, multiZoneGeometry / zonesForWindow downstream would
    // keep seeing invalid uuids and either return zero rects for them or
    // (worse) fold them into geometry queries that silently no-op.
    struct RewriteTarget
    {
        QStringList zones;
        QString screenId;
        int desktop = 0;
    };
    QHash<QString, RewriteTarget> toRewrite;
    // Collect-then-mutate: forEachZoneAssignedWindow iterates the live stores, so
    // the unassign / re-assign below runs after the visitation completes. The
    // desktop is captured per window here so the rewrite pass needs no second
    // lookup against state that the removal pass may have already changed.
    forEachZoneAssignedWindow(
        [&](const QString& windowId, const QStringList& zoneIdList, const QString& windowScreen, int windowDesktop) {
            if (zoneIdList.isEmpty()) {
                toRemove.append(windowId);
                return;
            }

            // Preserve zone assignments for windows on other desktops. Desktop 0
            // means "all desktops" (pinned window) — always process those.
            //
            // Ordering note: this desktop gate is BEFORE the autotile-screen gate
            // below. Both gates ultimately preserve the assignment (by returning),
            // so order is observationally irrelevant — but the intent is "windows
            // on other desktops are preserved categorically, autotile preservation
            // is a separate axis that only matters for windows whose desktop is
            // current." Don't reorder these without checking that the new ordering
            // still preserves the union {other-desktop OR autotile-screen}.
            //
            // Per-output virtual desktops (#648): "other desktop" is relative to the
            // window's OWN screen's current desktop, not the global current. NOT the
            // shared desktopMatchesFilter helper: this gate must also preserve when
            // the screen's current desktop is unknown (0), where the helper's
            // filter-disabled semantics would process the window instead.
            const int currentDesktop = m_layoutManager->currentVirtualDesktopForScreen(windowScreen);
            if (windowDesktop != 0 && windowDesktop != currentDesktop) {
                return;
            }

            // If this screen's assignment is autotile, preserve zone assignments for resnap
            auto cached = screenIsAutotile.constFind(windowScreen);
            if (cached == screenIsAutotile.constEnd()) {
                QString assignmentId =
                    m_layoutManager->assignmentIdForScreen(windowScreen, currentDesktop, currentActivity);
                cached = screenIsAutotile.insert(windowScreen, PhosphorLayout::LayoutId::isAutotile(assignmentId));
            }
            if (*cached) {
                return;
            }

            PhosphorZones::Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(windowScreen);
            if (!effectiveLayout) {
                toRemove.append(windowId);
                return;
            }
            if (allZonesExistInLayout(zoneIdList, effectiveLayout)) {
                return; // fully valid, nothing to do
            }
            // Partial or full invalidity: rebuild the surviving subset. Empty
            // result means the whole window lost its zones → mark for unassign.
            QStringList survivingZones;
            survivingZones.reserve(zoneIdList.size());
            for (const QString& zid : zoneIdList) {
                const auto uuid = parseUuid(zid);
                if (uuid && effectiveLayout->zoneById(*uuid)) {
                    survivingZones.append(zid);
                }
            }
            if (survivingZones.isEmpty()) {
                toRemove.append(windowId);
            } else {
                toRewrite.insert(windowId, {survivingZones, windowScreen, windowDesktop});
            }
        });

    for (const QString& windowId : toRemove) {
        unassignWindow(windowId);
    }
    for (auto it = toRewrite.constBegin(); it != toRewrite.constEnd(); ++it) {
        const RewriteTarget& target = it.value();
        assignWindowToZones(it.key(), target.zones, target.screenId, target.desktop);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// State Management (persistence handled by adaptor via KConfig)
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::scheduleSaveState(DirtyMask fields)
{
    // Mark the supplied fields dirty and wake the adaptor's save timer.
    // Default DirtyAll preserves pre-Phase-3 semantics for call sites that
    // haven't been updated to declare which fields they mutate.
    markDirty(fields);
}

void WindowTrackingService::markDirty(DirtyMask fields)
{
    // OR into the persistent mask so the next saveState() knows which
    // JSON maps it needs to rewrite. Always emit stateChanged so the
    // adaptor's debounced save timer is kicked — even if the caller
    // passed DirtyNone (e.g. ephemeral state that wants to wake the timer
    // for indirect reasons), the adaptor does the right thing when the
    // eventual saveState() sees an empty mask.
    m_dirtyMask |= fields;
    Q_EMIT stateChanged();
}

WindowTrackingService::DirtyMask WindowTrackingService::takeDirty()
{
    const DirtyMask current = m_dirtyMask;
    m_dirtyMask = DirtyNone;
    return current;
}

void WindowTrackingService::clearDirty()
{
    m_dirtyMask = DirtyNone;
}

void WindowTrackingService::setLastUsedZone(const QString& zoneId, const QString& screenId, const QString& zoneClass,
                                            int desktop)
{
    // Restore onto the store that owns @p screenId's context; an empty screenId
    // (the disk restore, which persists only the zone id) lands on the global
    // holder as the single representative until the first live snap repopulates a
    // per-screen store.
    PhosphorSnapEngine::SnapState* store = snapForScreen(screenId);
    if (!store) {
        store = snapGlobals();
    }
    if (store) {
        store->restoreLastUsedZone(zoneId, screenId, zoneClass, desktop);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private Helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool WindowTrackingService::isGeometryOnScreen(const QRect& geometry) const
{
    // Check virtual screens first (covers both virtual and non-subdivided physical screens).
    // Use area-overlap semantics (not center-point containment) so windows on virtual
    // screen boundaries are handled consistently with the physical-screen fallback path.
    auto* mgr = m_screenManager;
    if (mgr) {
        const QStringList ids = mgr->effectiveScreenIds();
        for (const QString& id : ids) {
            QRect screenGeo = mgr->screenGeometry(id);
            if (!screenGeo.isValid()) {
                continue;
            }
            const QRect intersection = geometry.intersected(screenGeo);
            if (intersection.width() >= MinVisibleWidth && intersection.height() >= MinVisibleHeight) {
                return true;
            }
        }
        return false;
    }

    // Fallback: physical screens only (no PhosphorScreens::ScreenManager available)
    for (QScreen* screen : QGuiApplication::screens()) {
        QRect intersection = geometry.intersected(screen->geometry());
        if (intersection.width() >= MinVisibleWidth && intersection.height() >= MinVisibleHeight) {
            return true;
        }
    }
    return false;
}

QRect WindowTrackingService::adjustGeometryToScreen(const QRect& geometry) const
{
    // Try virtual/effective screens first via PhosphorScreens::ScreenManager
    auto* mgr = m_screenManager;
    if (mgr) {
        const QStringList ids = mgr->effectiveScreenIds();
        const QPoint center = geometry.center();
        QRect nearestGeo;
        int minDist = INT_MAX;

        for (const QString& id : ids) {
            QRect screenGeo = mgr->screenGeometry(id);
            if (!screenGeo.isValid()) {
                continue;
            }
            // Manhattan distance from center to screen center
            QPoint diff = center - screenGeo.center();
            int dist = qAbs(diff.x()) + qAbs(diff.y());
            if (dist < minDist) {
                minDist = dist;
                nearestGeo = screenGeo;
            }
        }

        if (nearestGeo.isValid()) {
            return clampToRect(geometry, nearestGeo);
        }
    }

    // Fallback: physical screens only
    QScreen* nearest = findNearestScreen(geometry.center());
    if (!nearest) {
        return geometry;
    }

    return clampToRect(geometry, nearest->geometry());
}

void WindowTrackingService::validateLastUsedZone(const QString& targetScreen)
{
    if (!m_layoutManager) {
        return;
    }
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(targetScreen);
    if (!layout) {
        // No layout resolves for this screen, so nothing here can PROVE a zone
        // is stale — and an unprovable claim must not be destructive. Falling
        // through would clear the last-used on every store pointing at the
        // screen, which is the same "no zone info, don't guess" posture the
        // free-geometry loop takes when it cannot resolve a target.
        return;
    }
    // Last-used is per-key: clear the last-used on any store that points at
    // @p targetScreen but whose zone no longer exists in that screen's layout.
    bool cleared = false;
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        const QString lastZoneId = state->lastUsedZoneId();
        if (lastZoneId.isEmpty() || state->lastUsedScreenId() != targetScreen) {
            continue;
        }
        const auto uuidOpt = parseUuid(lastZoneId);
        if (uuidOpt && layout->zoneById(*uuidOpt)) {
            continue;
        }
        state->restoreLastUsedZone({}, {}, {}, 0);
        cleared = true;
    }
    // Mark dirty HERE rather than at each call site. The clear is an in-memory
    // mutation, and a caller that does not otherwise schedule a save (the
    // already-valid-VS branch of the lastUsedScreenId loop is exactly that
    // case: it migrates nothing, so it sets no anyStateMigrated) would drop it
    // on the floor and reload the stale zone from disk on the next start.
    if (cleared) {
        markDirty(DirtyLastUsedZone);
    }
}

QString WindowTrackingService::resolveEffectiveScreenId(const QString& screenId) const
{
    if (!PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        return screenId;
    }

    auto* smgr = m_screenManager;
    if (!smgr) {
        return screenId;
    }

    const QStringList effectiveIds = smgr->effectiveScreenIds();
    if (effectiveIds.contains(screenId)) {
        return screenId;
    }

    // The stored virtual screen no longer exists. Try to find another virtual screen
    // on the same physical monitor, so the window stays in the virtual-screen domain
    // (screensMatch() returns false for physical-vs-virtual comparisons).
    QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
    const QStringList vsIds = smgr->virtualScreenIdsFor(physId);
    if (!vsIds.isEmpty()) {
        // Find the virtual screen with the nearest index to the old one,
        // so windows migrate to the geometrically closest region rather
        // than always landing on the first virtual screen.
        QString nearest = findNearestVirtualScreen(vsIds, PhosphorIdentity::VirtualScreenId::extractIndex(screenId));
        qCInfo(lcPlacement) << "Virtual screen" << screenId << "no longer exists, falling back to" << nearest
                            << "on same physical monitor" << physId;
        return nearest;
    }

    qCWarning(lcPlacement) << "Virtual screen" << screenId << "no longer exists, falling back to physical screen"
                           << physId;
    return physId;
}

PhosphorZones::Zone* WindowTrackingService::findZoneById(const QString& zoneId) const
{
    auto uuidOpt = parseUuid(zoneId);
    if (!uuidOpt) {
        return nullptr;
    }

    return findZoneInAllLayouts(*uuidOpt).zone;
}

WindowTrackingService::ZoneLookupResult WindowTrackingService::findZoneInAllLayouts(const QUuid& zoneUuid) const
{
    // Guard locally like every other m_layoutManager consumer in this file
    // (onLayoutChanged, validateLastUsedZone) — the API does not promise a
    // non-null manager.
    if (!m_layoutManager) {
        return {};
    }
    // Search all layouts, not just the active one, to support per-screen layouts
    for (PhosphorZones::Layout* layout : m_layoutManager->layouts()) {
        PhosphorZones::Zone* zone = layout->zoneById(zoneUuid);
        if (zone) {
            return {zone, layout};
        }
    }
    return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// Wiring, accessors, and state getters/setters (trivial forwarders)
// ═══════════════════════════════════════════════════════════════════════════════

QObject* WindowTrackingService::asQObject()
{
    return this;
}

void WindowTrackingService::setWindowRegistry(PhosphorEngine::WindowRegistry* registry)
{
    m_windowRegistry = registry;
}

PhosphorEngine::WindowPlacementStore& WindowTrackingService::placementStore()
{
    return m_placementStore;
}

const PhosphorEngine::WindowPlacementStore& WindowTrackingService::placementStore() const
{
    return m_placementStore;
}

void WindowTrackingService::setShouldTrackPredicate(ShouldTrackPredicate predicate)
{
    m_shouldTrackPredicate = std::move(predicate);
}

void WindowTrackingService::setAutotileModePredicate(AutotileModePredicate predicate)
{
    m_autotileModePredicate = std::move(predicate);
}

bool WindowTrackingService::isWindowInAutotileMode(const QString& windowId) const
{
    return m_autotileModePredicate && m_autotileModePredicate(windowId);
}

void WindowTrackingService::setAutotileTiledPredicate(AutotileTiledPredicate predicate)
{
    m_autotileTiledPredicate = std::move(predicate);
}

bool WindowTrackingService::isWindowAutotileTiled(const QString& windowId) const
{
    return m_autotileTiledPredicate && m_autotileTiledPredicate(windowId);
}

PhosphorEngine::WindowRegistry* WindowTrackingService::windowRegistry() const
{
    return m_windowRegistry;
}

PhosphorScreens::ScreenManager* WindowTrackingService::screenManager() const
{
    return m_screenManager;
}

void WindowTrackingService::setEngineFloatResolver(EngineFloatResolver resolver)
{
    m_engineFloatResolver = std::move(resolver);
}

void WindowTrackingService::setEngineFloatWriter(EngineFloatWriter writer)
{
    m_engineFloatWriter = std::move(writer);
}

void WindowTrackingService::setEngineFloatLister(EngineFloatLister lister)
{
    m_engineFloatLister = std::move(lister);
}

void WindowTrackingService::clearResnapBuffer()
{
    m_resnapBuffer.clear();
}

const QHash<QString, QList<WindowTrackingService::PendingRestore>>& WindowTrackingService::pendingRestoreQueues() const
{
    return m_pendingRestoreQueues;
}

QVector<WindowTrackingService::ResnapEntry> WindowTrackingService::takeResnapBuffer()
{
    return std::exchange(m_resnapBuffer, {});
}

void WindowTrackingService::setPendingRestoreQueues(const QHash<QString, QList<PendingRestore>>& queues)
{
    m_pendingRestoreQueues = queues;
}

void WindowTrackingService::setFloatingWindows(const QSet<QString>& windows)
{
    m_floatingWindows = windows;
}

WindowTrackingService::DirtyMask WindowTrackingService::peekDirty() const
{
    return m_dirtyMask;
}

} // namespace PhosphorPlacement
