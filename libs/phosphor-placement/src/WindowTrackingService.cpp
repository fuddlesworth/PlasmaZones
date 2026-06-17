// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPlacement/WindowTrackingService.h>
#include "placementutils.h"

#include <PhosphorZones/Layout.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorIdentity/WindowId.h>
#include "placementlogging.h"
#include <PhosphorEngine/WindowRegistry.h>
#include <QScreen>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorPlacement {

WindowTrackingService::WindowTrackingService(PhosphorZones::LayoutRegistry* layoutManager,
                                             PhosphorZones::IZoneDetector* zoneDetector,
                                             PhosphorScreens::ScreenManager* screenManager,
                                             PhosphorWorkspaces::VirtualDesktopManager* vdm,
                                             IGeometryResolver* geometryResolver, PlacementConfig config,
                                             QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_zoneDetector(zoneDetector)
    , m_geometryResolver(geometryResolver)
    , m_config(config)
    , m_virtualDesktopManager(vdm)
    , m_screenManager(screenManager)
{
    Q_ASSERT(layoutManager);
    Q_ASSERT(zoneDetector);

    // Note: No save timer needed - persistence handled by WindowTrackingAdaptor via KConfig
    // Service just emits stateChanged() signal when state changes
    //
    // PhosphorZones::Layout change handling: WindowTrackingAdaptor connects to activeLayoutChanged and calls
    // onLayoutChanged(). Do NOT connect here - duplicate invocation would clear m_resnapBuffer
    // on the second run (after assignments were already removed), causing no_windows_to_resnap.

    // Note: Persistence is handled by WindowTrackingAdaptor via KConfig.
    // The service is a pure in-memory state manager - adaptor calls
    // populateState() after loading from KConfig.
}

WindowTrackingService::~WindowTrackingService()
{
    // Note: Persistence is handled by WindowTrackingAdaptor via KConfig
    // Service is purely in-memory state management
}

QString WindowTrackingService::currentAppIdFor(const QString& anyWindowId) const
{
    if (anyWindowId.isEmpty()) {
        return QString();
    }
    if (m_windowRegistry) {
        const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(anyWindowId);
        const QString fromRegistry = m_windowRegistry->appIdFor(instanceId);
        if (!fromRegistry.isEmpty()) {
            return fromRegistry;
        }
    }
    return PhosphorIdentity::WindowId::extractAppId(anyWindowId);
}

QString WindowTrackingService::canonicalizeForLookup(const QString& rawWindowId) const
{
    if (rawWindowId.isEmpty()) {
        return rawWindowId;
    }
    if (m_windowRegistry) {
        return m_windowRegistry->canonicalizeForLookup(rawWindowId);
    }
    return rawWindowId;
}

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Zone Assignment Management
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::assignWindowToZone(const QString& windowId, const QString& zoneId, const QString& screenId,
                                               int virtualDesktop)
{
    assignWindowToZones(windowId, QStringList{zoneId}, screenId, virtualDesktop);
}

void WindowTrackingService::assignWindowToZones(const QString& windowId, const QStringList& zoneIds,
                                                const QString& screenId, int virtualDesktop)
{
    Q_ASSERT(m_snapState);
    if (!m_snapState || windowId.isEmpty() || zoneIds.isEmpty()) {
        return;
    }

    // Filter out empty/null zone IDs — callers may pass partially-valid lists
    QStringList validZoneIds;
    validZoneIds.reserve(zoneIds.size());
    for (const auto& id : zoneIds) {
        if (!id.isEmpty()) {
            validZoneIds.append(id);
        }
    }
    if (validZoneIds.isEmpty()) {
        return;
    }

    // Snapshot every axis the underlying SnapState may mutate so we can gate
    // both the change signal and the dirty mark on a real diff. A same-zone
    // different-screen/desktop call (e.g. pinned-window resnap after a desktop
    // switch, or sticky-window virtualDesktop=0 → !=0 commit) still needs the
    // DirtyZoneAssignments mark so the next save persists the new (screen,
    // desktop) tuple — otherwise the on-disk state silently rots.
    const QStringList previousZones = m_snapState->zonesForWindow(windowId);
    const QString previousScreen = m_snapState->screenForWindow(windowId);
    const int previousDesktop = m_snapState->desktopForWindow(windowId);
    const bool zoneChanged = (previousZones != validZoneIds);
    const bool screenChanged = (previousScreen != screenId);
    const bool desktopChanged = (previousDesktop != virtualDesktop);

    m_snapState->assignWindowToZones(windowId, validZoneIds, screenId, virtualDesktop);
    // Mirror SnapState::assignWindowToZones's own floating-set removal (it removes
    // the window from its m_floatingWindows) at the WTS layer. The two sets are
    // independent — assigning
    // a window to zones implicitly un-floats it in SnapState, but the WTS
    // m_floatingWindows entry would survive without this clear, leaving
    // isWindowFloating() returning true via the appId fallback even though
    // the window is now snapped. Current callers (snap-engine/src/commit.cpp
    // clearFloatingForSnap before assignWindowToZones) shadow this, but the
    // shadowing is fragile and the explicit sync here makes the cross-layer
    // contract robust.
    //
    // Remove BOTH the windowId AND the appId entry unconditionally — the
    // post-session-restore case has the appId entry present without the
    // windowId one (see m_floatingWindows comments in WindowTrackingService.h),
    // so gating the appId removal on the windowId removal succeeding would
    // miss exactly the case the comment block above describes. QSet::remove
    // on a missing key is a documented no-op so the unconditional form is
    // cost-equivalent in the no-op path.
    m_floatingWindows.remove(windowId);
    const QString appId = currentAppIdFor(windowId);
    if (appId != windowId) {
        m_floatingWindows.remove(appId);
    }

    if (zoneChanged) {
        Q_EMIT windowZoneChanged(windowId, validZoneIds.first());
    }
    if (zoneChanged || screenChanged || desktopChanged) {
        // Only the zone/screen/desktop maps changed. Narrower than DirtyAll
        // so the next save rewrites exactly one JSON field instead of all
        // ten. Gated on a real diff for the same reason the signal is — a
        // no-op assign call shouldn't churn the dirty mask and force a
        // redundant serialise on the next save.
        markDirty(DirtyZoneAssignments);
    }
}

void WindowTrackingService::unassignWindow(const QString& windowId)
{
    Q_ASSERT(m_snapState);
    if (!m_snapState)
        return;
    auto result = m_snapState->unassignWindow(windowId);
    if (!result.wasAssigned) {
        return;
    }

    Q_EMIT windowZoneChanged(windowId, QString());
    markDirty(DirtyZoneAssignments | (result.lastUsedZoneCleared ? DirtyLastUsedZone : DirtyNone));
}

QString WindowTrackingService::zoneForWindow(const QString& windowId) const
{
    Q_ASSERT(m_snapState);
    if (!m_snapState)
        return {};
    return m_snapState->zoneForWindow(windowId);
}

QStringList WindowTrackingService::zonesForWindow(const QString& windowId) const
{
    Q_ASSERT(m_snapState);
    if (!m_snapState)
        return {};
    return m_snapState->zonesForWindow(windowId);
}

QStringList WindowTrackingService::windowsInZone(const QString& zoneId) const
{
    Q_ASSERT(m_snapState);
    if (!m_snapState)
        return {};
    return m_snapState->windowsInZone(zoneId);
}

QStringList WindowTrackingService::snappedWindows() const
{
    Q_ASSERT(m_snapState);
    if (!m_snapState)
        return {};
    return m_snapState->snappedWindows();
}

int WindowTrackingService::pruneStaleAssignments(const QSet<QString>& aliveWindowIds)
{
    Q_ASSERT(m_snapState);
    if (!m_snapState)
        return 0;
    int pruned = m_snapState->pruneStaleAssignments(aliveWindowIds);

    int wtsCleaned = 0;
    auto removeHash = [&](auto& hash) {
        for (auto it = hash.begin(); it != hash.end();) {
            if (!aliveWindowIds.contains(it.key())) {
                it = hash.erase(it);
                ++wtsCleaned;
            } else {
                ++it;
            }
        }
    };
    auto removeSet = [&](auto& set) {
        for (auto it = set.begin(); it != set.end();) {
            if (!aliveWindowIds.contains(*it)) {
                it = set.erase(it);
                ++wtsCleaned;
            } else {
                ++it;
            }
        }
    };

    removeHash(m_windowStickyStates);
    // m_floatingWindows is the legacy fallback set — empty in production once the
    // per-engine float resolver/writer are wired (the engines own float state), so
    // this is a no-op there; kept for the unwired / unit-test path.
    removeSet(m_floatingWindows);

    if (m_snapEngine) {
        wtsCleaned += m_snapEngine->pruneStaleWindows(aliveWindowIds);
    }

    if (pruned > 0 || wtsCleaned > 0) {
        markDirty(DirtyZoneAssignments | DirtyPreTileGeometries | DirtyPreFloatZones | DirtyPreFloatScreens);
    }

    return pruned + wtsCleaned;
}

bool WindowTrackingService::isWindowSnapped(const QString& windowId) const
{
    Q_ASSERT(m_snapState);
    if (!m_snapState)
        return false;
    return m_snapState->isWindowSnapped(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Geometry Validation Utility
// ═══════════════════════════════════════════════════════════════════════════════

std::optional<QRect> WindowTrackingService::validateGeometryForScreen(const QRect& geo, const QString& savedScreen,
                                                                      const QString& currentScreenName) const
{
    if (!geo.isValid() || geo.width() <= 0 || geo.height() <= 0) {
        return std::nullopt;
    }

    // Cross-screen check: if the geometry was captured on a different screen than where
    // the window currently is, the absolute coordinates are wrong. Preserve the size
    // but center on the current screen. This triggers for:
    // 1. Different physical monitors (e.g. DP-1 vs HDMI-1)
    // 2. Different virtual screens on the same physical monitor (e.g. DP-1/vs:0 vs DP-1/vs:1)
    //    — the virtual screens have different geometry bounds, so coordinates are wrong.
    if (!savedScreen.isEmpty() && !currentScreenName.isEmpty()
        && !PhosphorScreens::ScreenIdentity::screensMatch(savedScreen, currentScreenName)) {
        auto* mgr = m_screenManager;
        QRect available;
        bool haveTarget = false;
        if (mgr) {
            const PhosphorScreens::PhysicalScreen target = mgr->physicalScreenFor(currentScreenName);
            if (target.isValid()) {
                haveTarget = true;
                // For virtual screens, prefer virtual screen bounds over full physical screen
                available = mgr->screenGeometry(currentScreenName).isValid()
                    ? mgr->screenAvailableGeometry(currentScreenName)
                    : mgr->actualAvailableGeometry(target);
            }
        } else if (QScreen* target = PhosphorScreens::ScreenIdentity::findByIdOrName(currentScreenName)) {
            haveTarget = true;
            available = target->availableGeometry();
        }
        if (haveTarget) {
            // Clamp size to fit within the target screen (the window may have been
            // larger than the target VS when captured on a wider screen/physical monitor).
            int w = qMin(geo.width(), available.width());
            int h = qMin(geo.height(), available.height());
            int x = available.x() + (available.width() - w) / 2;
            int y = available.y() + (available.height() - h) / 2;
            QRect adjusted(x, y, w, h);
            qCDebug(lcPlacement) << "validateGeometryForScreen: cross-screen adjustment from" << savedScreen << "to"
                                 << currentScreenName << ":" << geo << "->" << adjusted;
            return adjusted;
        }
    }

    if (isGeometryOnScreen(geo)) {
        return geo;
    }
    return adjustGeometryToScreen(geo);
}

std::optional<QRect> WindowTrackingService::validatedUnmanagedGeometry(const QString& windowId, const QString& screenId,
                                                                       bool exactOnly) const
{
    if (windowId.isEmpty()) {
        return std::nullopt;
    }
    // SINGLE source of truth for the float-back: the placement record's SHARED
    // per-screen free geometry. (The legacy per-engine m_unmanagedGeometries store
    // is no longer consulted — two parallel stores drifted and leaked the zone/tile
    // rect into float.) Free geometry is shared across modes, so snap and autotile
    // resolve the same value; prefer this screen's remembered spot, then any other
    // screen's (cross-screen validated).
    const QString appId = exactOnly ? QString() : currentAppIdFor(windowId);
    const auto rec = m_placementStore.peek(windowId, appId);
    if (!rec) {
        return std::nullopt;
    }
    const QRect exact = rec->freeGeometryFor(screenId);
    if (exact.isValid()) {
        return validateGeometryForScreen(exact, screenId, screenId);
    }
    for (auto it = rec->freeGeometryByScreen.constBegin(); it != rec->freeGeometryByScreen.constEnd(); ++it) {
        if (it.value().isValid()) {
            return validateGeometryForScreen(it.value(), it.key(), screenId);
        }
    }
    return std::nullopt;
}

void WindowTrackingService::recordFreeGeometry(const QString& windowId, const QString& screenId, const QRect& geometry,
                                               bool overwrite)
{
    if (windowId.isEmpty() || screenId.isEmpty() || !geometry.isValid()) {
        return;
    }
    // INVARIANT (WindowPlacement::freeGeometryByScreen): this map holds ONLY a
    // genuine free/floating frame and is FROZEN while the window occupies a ZONE
    // (snapped) — a snapped window's live frame IS the zone rect, so recording it
    // here would overwrite the float-back with the snapped geometry (the per-mode
    // geometry leak the unified record exists to prevent). This is the SINGLE write
    // point into the shared free geometry, so the guard lives here, not at each
    // caller. `isWindowSnapped` stays true for a floating-with-preserved-zone
    // window, so AND with `!isWindowFloating` — "snapped AND not floating" = actually
    // occupying the zone. (The autotile-tiled case is NOT gated here: a window that
    // is on an autotile screen but not yet tiled — a fresh spawn — legitimately has a
    // free frame, and "autotile mode + not floating" cannot tell that apart from a
    // tiled window. The effect's saveAndRecordPreAutotileGeometry guards the tiled
    // case at capture time instead.)
    if (isWindowSnapped(windowId) && !isWindowFloating(windowId)) {
        qCDebug(lcPlacement) << "recordFreeGeometry: refusing snapped frame for" << windowId
                             << "— float-back stays frozen while it occupies a zone";
        return;
    }
    // Same invariant for the tiled case: an actively-tiled window's frame IS
    // the tile rect. The effect's saveAndRecordPreAutotileGeometry guards its
    // own capture paths, but cannot help when the effect reloads (kwin
    // restart with the daemon alive) — its border tracking starts empty and
    // the re-announce batch would push every tiled window's zone rect here
    // with overwrite=true. The engine-backed predicate survives that reload.
    if (isWindowAutotileTiled(windowId)) {
        qCDebug(lcPlacement) << "recordFreeGeometry: refusing tiled frame for" << windowId
                             << "— float-back stays frozen while the autotile engine tiles it";
        return;
    }
    const QString appId = currentAppIdFor(windowId);
    if (appId.isEmpty()) {
        return;
    }
    if (!overwrite) {
        const auto existing = m_placementStore.peek(windowId, appId);
        if (existing && existing->freeGeometryFor(screenId).isValid()) {
            return; // first-capture-wins
        }
    }
    // A geometry-only partial: no engine slot, so record()'s merge leaves the
    // managed context (screen/desktop/activity) untouched and only updates this
    // screen's free geometry.
    PhosphorEngine::WindowPlacement p;
    p.windowId = windowId;
    p.appId = appId;
    p.screenId = screenId;
    p.freeGeometryByScreen.insert(screenId, geometry);
    if (m_placementStore.record(p)) {
        markDirty(DirtyWindowPlacements);
    }
}

void WindowTrackingService::recordFloatingClose(const QString& windowId, const QString& screenId, const QRect& geometry)
{
    if (windowId.isEmpty() || screenId.isEmpty() || !geometry.isValid()) {
        return;
    }
    // Never let a tile rect become the float-back — same invariant recordFreeGeometry
    // enforces. (An orphaned cross-screen-dragged window is floating, not tiled, so
    // this is belt-and-braces.)
    if (isWindowAutotileTiled(windowId)) {
        return;
    }
    const QString appId = currentAppIdFor(windowId);
    if (appId.isEmpty()) {
        return;
    }
    PhosphorEngine::WindowPlacement p;
    p.windowId = windowId;
    p.appId = appId;
    p.screenId = screenId;
    p.freeGeometryByScreen.insert(screenId, geometry);
    // Preserve the existing record's per-engine slots and context. Carrying a
    // non-empty engine map is what makes the store merge adopt the new screenId
    // (a geometry-only partial, like recordFreeGeometry, would leave the stale
    // managed screen in place — exactly the bug this fixes).
    if (const auto existing = m_placementStore.peek(windowId, appId)) {
        p.virtualDesktop = existing->virtualDesktop;
        p.activity = existing->activity;
        p.kind = existing->kind;
        p.engines = existing->engines;
    }
    if (p.engines.isEmpty()) {
        // No prior record (first close after a cross-screen drag): synthesize a
        // floating slot so the merge still adopts the screen. A floated restore
        // keys off screenId + freeGeometryByScreen, not the slot's engine id, so
        // the slot id is not load-bearing here.
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        p.engines.insert(QString(PhosphorEngine::WindowPlacement::snapEngineId()), slot);
    }
    if (m_placementStore.record(p)) {
        markDirty(DirtyWindowPlacements);
    }
    // Close-capture convergence: this orphaned cross-screen close is the freshest
    // authority for the app's float-back, so drop stale pure-float duplicates on
    // the same screen (see WindowPlacementStore::collapsePureFloatSiblings).
    m_placementStore.collapsePureFloatSiblings(appId, windowId);
}

void WindowTrackingService::clearFreeGeometry(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    if (m_placementStore.clearFreeGeometry(windowId)) {
        markDirty(DirtyWindowPlacements);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Floating Window State
// ═══════════════════════════════════════════════════════════════════════════════

bool WindowTrackingService::isWindowFloating(const QString& windowId) const
{
    // Per-engine answer: when the daemon has wired the resolver, the float bit
    // is the float state of the engine that owns the window's CURRENT screen
    // mode (SnapState::isFloating for Snapping, TilingState::isFloating for
    // Autotile). A window floated in autotile is NOT floating in snap, and the
    // converse, so the two engines never share a bit.
    if (m_engineFloatResolver) {
        return m_engineFloatResolver(windowId);
    }

    // Legacy fallback (unit tests / early init before engines are wired):
    // Try full window ID first (runtime - distinguishes multiple instances)
    if (m_floatingWindows.contains(windowId)) {
        return true;
    }
    // Fall back to app ID (session restore - pointer addresses change across restarts)
    QString appId = currentAppIdFor(windowId);
    return (appId != windowId && m_floatingWindows.contains(appId));
}

void WindowTrackingService::setWindowFloating(const QString& windowId, bool floating)
{
    // Use full windowId so each window instance has independent floating state
    // (appId would collide for multiple instances of the same app).
    //
    // Gate every downstream effect — snap-state mutation AND the persistence
    // schedule — on an actual state change. The earlier shape unconditionally
    // called scheduleSaveState, which ORs DirtyAll into the dirty mask and
    // defeats the delta-write design when the call was a no-op.
    //
    // Use the appId-aware `isWindowFloating` predicate (which checks both
    // the full windowId AND the session-restored appId fallback — see
    // isWindowFloating's appId branch). A naive `m_floatingWindows.contains(windowId)` would
    // return false when only the appId entry exists post-session-restore,
    // letting the early-return short-circuit the cleanup path below — the
    // appId entry would never be removed and isWindowFloating would keep
    // reporting true, breaking clearFloatingForSnap and the daemon's
    // syncAutotileFloatState callers.
    const bool wasFloating = isWindowFloating(windowId);
    if (floating == wasFloating) {
        return;
    }

    // Per-engine routing: when wired, the write lands ONLY in the engine that
    // owns the window's current screen mode — floating a window in autotile
    // must NOT set the snap-mode float bit and vice versa. The daemon's writer
    // resolves the owning engine and mutates that engine's authoritative float
    // store (SnapState / TilingState).
    if (m_engineFloatWriter) {
        m_engineFloatWriter(windowId, floating);
        return;
    }

    // Legacy fallback (unit tests / early init before engines are wired):
    // maintain the shared set + snap state directly.
    if (floating) {
        m_floatingWindows.insert(windowId);
    } else {
        m_floatingWindows.remove(windowId);
        // Also remove app ID entry (session-restored entries)
        QString appId = currentAppIdFor(windowId);
        if (appId != windowId) {
            m_floatingWindows.remove(appId);
        }
    }

    if (m_snapState) {
        m_snapState->setFloating(windowId, floating);
    }

    // Floating state is ephemeral and NOT persisted — WindowTrackingAdaptor's
    // save path never writes it, and its load path only deletes the obsolete
    // `obsoleteFloatingWindowsKey` to remove any pre-ephemeral remnant on disk.
    // Calling
    // scheduleSaveState() here used to OR DirtyAll into the dirty mask
    // and trigger a debounced full state rewrite of every OTHER persisted
    // field for nothing — every Meta+F toggle / drag-to-float would
    // unnecessarily re-serialise pre-float assignments, autotile orders,
    // pending restores, etc. Skip the schedule entirely.
}

QStringList WindowTrackingService::floatingWindows() const
{
    // Per-engine aggregation when wired: floats now live in each engine's
    // authoritative store (SnapState / TilingState), not the legacy shared set.
    if (m_engineFloatLister) {
        return m_engineFloatLister();
    }
    return m_floatingWindows.values();
}

void WindowTrackingService::unsnapForFloat(const QString& windowId)
{
    if (!m_snapState || !m_snapState->isWindowSnapped(windowId)) {
        return;
    }

    // Read zone/screen for logging BEFORE unsnapForFloat clears them.
    QStringList zoneIds = m_snapState->zonesForWindow(windowId);
    QString screenId = m_snapState->screenForWindow(windowId);

    // SnapState::unsnapForFloat saves pre-float state (windowId-keyed) and unassigns.
    auto unassignResult = m_snapState->unsnapForFloat(windowId);

    // Also write an appId-keyed entry into SnapState for session-restore fallback.
    // SnapState::unsnapForFloat only writes the windowId key; the appId alias
    // lets preFloatZone()/preFloatScreen() find the entry after a window
    // close+reopen cycle where the windowId changes but the appId persists.
    QString appId = currentAppIdFor(windowId);
    if (appId != windowId && !appId.isEmpty()) {
        m_snapState->addPreFloatZone(appId, zoneIds);
        if (!screenId.isEmpty()) {
            m_snapState->addPreFloatScreen(appId, screenId);
        }
    }
    qCInfo(lcPlacement) << "Saved pre-float zones for" << windowId << "->" << zoneIds << "screen:" << screenId;

    markDirty(DirtyPreFloatZones | DirtyPreFloatScreens);

    Q_EMIT windowZoneChanged(windowId, QString());
    markDirty(DirtyZoneAssignments | (unassignResult.lastUsedZoneCleared ? DirtyLastUsedZone : DirtyNone));

    consumePendingAssignment(windowId);
}

template<typename Func>
auto WindowTrackingService::preFloatLookup(const QString& windowId, Func&& getter) const -> decltype(getter(windowId))
{
    if (!m_snapState) {
        return {};
    }
    auto result = getter(windowId);
    if (!result.isEmpty()) {
        return result;
    }
    QString appId = currentAppIdFor(windowId);
    if (appId != windowId) {
        result = getter(appId);
    }
    return result;
}

QString WindowTrackingService::preFloatZone(const QString& windowId) const
{
    return preFloatLookup(windowId, [this](const QString& id) {
        return m_snapState->preFloatZone(id);
    });
}

QStringList WindowTrackingService::preFloatZones(const QString& windowId) const
{
    return preFloatLookup(windowId, [this](const QString& id) {
        return m_snapState->preFloatZones(id);
    });
}

QString WindowTrackingService::preFloatScreen(const QString& windowId) const
{
    return preFloatLookup(windowId, [this](const QString& id) {
        return m_snapState->preFloatScreen(id);
    });
}

void WindowTrackingService::clearPreFloatZoneForWindow(const QString& windowId)
{
    if (windowId.isEmpty() || !m_snapState) {
        return;
    }
    m_snapState->clearPreFloatZone(windowId);
}

void WindowTrackingService::clearPreFloatZone(const QString& windowId)
{
    if (!m_snapState) {
        return;
    }
    // Remove by full window ID (runtime entries)
    m_snapState->clearPreFloatZone(windowId);
    // Also remove by app ID (session-restored entries)
    QString appId = currentAppIdFor(windowId);
    if (appId != windowId) {
        m_snapState->clearPreFloatZone(appId);
    }
}

bool WindowTrackingService::clearFloatingForSnap(const QString& windowId)
{
    if (!isWindowFloating(windowId)) {
        return false;
    }
    setWindowFloating(windowId, false);
    clearPreFloatZone(windowId);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Sticky Window Handling
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::setWindowSticky(const QString& windowId, bool sticky)
{
    m_windowStickyStates[windowId] = sticky;
}

bool WindowTrackingService::isWindowSticky(const QString& windowId) const
{
    return m_windowStickyStates.value(windowId, false);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shared Helpers
// ═══════════════════════════════════════════════════════════════════════════════

// sortZonesByNumber / buildZonePositionMap removed — callers should use
// PhosphorZones::LayoutUtils directly.

// ═══════════════════════════════════════════════════════════════════════════════
// Out-of-line accessors delegating to SnapState
// ═══════════════════════════════════════════════════════════════════════════════

const QHash<QString, QStringList>& WindowTrackingService::zoneAssignments() const
{
    Q_ASSERT(m_snapState);
    return m_snapState->zoneAssignments();
}

QStringList WindowTrackingService::recordedSnapZones(const QString& windowId) const
{
    // Prefer the live, runtime assignment — it reflects this session's snaps.
    if (m_snapState) {
        const QStringList live = m_snapState->zoneAssignments().value(windowId);
        if (!live.isEmpty()) {
            return live;
        }
    }
    // Cold cache (post-restart, or after handoffRelease cleared the live map):
    // fall back to the DURABLE snap slot in the placement record. windowId is the
    // exact `appId|uuid`; KWin uuids are stable across a daemon restart, so peek's
    // exact-id branch resolves the right window (the appId fallback is for relogin).
    const auto rec = m_placementStore.peek(windowId, currentAppIdFor(windowId));
    if (rec) {
        const PhosphorEngine::EngineSlot snapSlot = rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
        if (snapSlot.state == PhosphorEngine::WindowPlacement::stateSnapped()) {
            return snapSlot.zoneIds;
        }
    }
    return {};
}

const QHash<QString, QString>& WindowTrackingService::screenAssignments() const
{
    Q_ASSERT(m_snapState);
    return m_snapState->screenAssignments();
}

const QHash<QString, int>& WindowTrackingService::desktopAssignments() const
{
    Q_ASSERT(m_snapState);
    return m_snapState->desktopAssignments();
}

// The lastUsed* accessors are read during the WTA constructor's loadState()
// call — which runs BEFORE Daemon::init wires SnapState via setSnapState().
// Returning a sentinel (empty string / 0) when SnapState isn't yet
// attached lets early-init readers (the setLastUsedZone restore in
// WindowTrackingAdaptor::loadState) pass through harmlessly instead of
// asserting and crashing the daemon on startup. The snap-engine's own lastUsedZone
// state is loaded later from KConfig through its persistence delegate
// once SnapState is wired, so the early-init read here can only ever
// produce a "no last zone yet" result anyway.
QString WindowTrackingService::lastUsedZoneId() const
{
    return m_snapState ? m_snapState->lastUsedZoneId() : QString();
}

QString WindowTrackingService::lastUsedZoneClass() const
{
    return m_snapState ? m_snapState->lastUsedZoneClass() : QString();
}

QString WindowTrackingService::lastUsedScreenName() const
{
    return m_snapState ? m_snapState->lastUsedScreenId() : QString();
}

int WindowTrackingService::lastUsedDesktop() const
{
    return m_snapState ? m_snapState->lastUsedDesktop() : 0;
}

void WindowTrackingService::retagLastUsedZoneClass(const QString& newClass)
{
    Q_ASSERT(m_snapState);
    m_snapState->retagLastUsedZoneClass(newClass);
}

const QSet<QString>& WindowTrackingService::userSnappedClasses() const
{
    Q_ASSERT(m_snapState);
    return m_snapState->userSnappedClasses();
}

void WindowTrackingService::setUserSnappedClasses(const QSet<QString>& classes)
{
    if (!m_snapState) {
        qCWarning(lcPlacement) << "setUserSnappedClasses: no SnapState — dropping" << classes.size() << "classes";
        return;
    }
    m_snapState->setUserSnappedClasses(classes);
}

const QHash<QString, QStringList>& WindowTrackingService::preFloatZoneAssignments() const
{
    static const QHash<QString, QStringList> empty;
    if (!m_snapState) {
        return empty;
    }
    return m_snapState->preFloatZoneAssignments();
}

const QHash<QString, QString>& WindowTrackingService::preFloatScreenAssignments() const
{
    static const QHash<QString, QString> empty;
    if (!m_snapState) {
        return empty;
    }
    return m_snapState->preFloatScreenAssignments();
}

void WindowTrackingService::setPreFloatZoneAssignments(const QHash<QString, QStringList>& assignments)
{
    if (!m_snapState) {
        qCWarning(lcPlacement) << "setPreFloatZoneAssignments: no SnapState — dropping" << assignments.size()
                               << "entries";
        return;
    }
    m_snapState->setPreFloatZoneAssignments(assignments);
}

void WindowTrackingService::setPreFloatScreenAssignments(const QHash<QString, QString>& assignments)
{
    if (!m_snapState) {
        qCWarning(lcPlacement) << "setPreFloatScreenAssignments: no SnapState — dropping" << assignments.size()
                               << "entries";
        return;
    }
    m_snapState->setPreFloatScreenAssignments(assignments);
}

void WindowTrackingService::setActiveAssignments(const QHash<QString, QStringList>& zones,
                                                 const QHash<QString, QString>& screens,
                                                 const QHash<QString, int>& desktops)
{
    if (!m_snapState) {
        qCWarning(lcPlacement) << "setActiveAssignments: no SnapState — dropping" << zones.size() << "assignments";
        return;
    }
    m_snapState->setZoneAssignments(zones);
    m_snapState->setScreenAssignments(screens);
    m_snapState->setDesktopAssignments(desktops);
}

QRect WindowTrackingService::resolveZoneGeometry(const QStringList& zoneIds, const QString& screenId) const
{
    if (zoneIds.isEmpty()) {
        return QRect();
    }
    return (zoneIds.size() > 1) ? multiZoneGeometry(zoneIds, screenId) : zoneGeometry(zoneIds.first(), screenId);
}

} // namespace PhosphorPlacement
