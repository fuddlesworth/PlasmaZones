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
                                             PhosphorScreens::ScreenManager* screenManager,
                                             PhosphorWorkspaces::VirtualDesktopManager* vdm,
                                             IGeometryResolver* geometryResolver, PlacementConfig config,
                                             QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_geometryResolver(geometryResolver)
    , m_config(config)
    , m_virtualDesktopManager(vdm)
    , m_screenManager(screenManager)
{
    Q_ASSERT(layoutManager);

    // Live-window probe for the placement store's reopen fallback: a record
    // whose window is still open must never be consumed by a sibling's
    // reopen (see WindowPlacementStore::takeForReopen). Evaluated at consume
    // time; registry-less test services answer false, i.e. no exclusion.
    m_placementStore.setLiveInstanceProbe([this](const QString& windowId) {
        // Bare ids (no '|') answer false outright: extractInstanceId treats a
        // bare string AS the instance id, so a bare-id record would
        // fuzzy-match a live composite's uuid and become permanently
        // unconsumable by the reopen fallback.
        return windowId.contains(QLatin1Char('|')) && m_windowRegistry
            && m_windowRegistry->contains(PhosphorIdentity::WindowId::extractInstanceId(windowId));
    });

    // No save timer here: the service is an in-memory state manager whose
    // persistence is driven by the WindowTrackingAdaptor's dirty-mask
    // save/load (WindowTrackingAdaptor::saveState/loadState, debounced via
    // scheduleSaveState). The service only tracks the dirty mask (markDirty)
    // and emits stateChanged.
    //
    // Layout change handling: WindowTrackingAdaptor connects activeLayoutChanged
    // to onLayoutChanged(). The adaptor owns the triggers, so none is
    // connected here.
}

WindowTrackingService::~WindowTrackingService()
{
    // In-memory only; the adaptor owns persistence (see the ctor note).
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
    Q_ASSERT(hasSnapState());
    if (windowId.isEmpty() || zoneIds.isEmpty()) {
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

    // Resolve (and, on first placement, register) the per-screen store that owns
    // this window. A screen-carrying write is the reverse map's authoritative
    // seed point. A pinned desktop (a RouteToDesktop commit, a cross-desktop
    // move, a background-desktop restore) names that desktop's store, so the
    // assignment lives in the context it is for.
    PhosphorSnapEngine::SnapState* snapState = snapForWindowOnScreen(windowId, screenId, virtualDesktop);
    if (!snapState) {
        return;
    }

    // Snapshot every axis the underlying SnapState may mutate so we can gate
    // both the change signal and the dirty mark on a real diff. A same-zone
    // different-screen/desktop call (e.g. pinned-window resnap after a desktop
    // switch, or sticky-window virtualDesktop=0 → !=0 commit) still needs the
    // DirtyZoneAssignments mark so the next save persists the new (screen,
    // desktop) tuple — otherwise the on-disk state silently rots.
    const QStringList previousZones = snapState->zonesForWindow(windowId);
    const QString previousScreen = snapState->screenForWindow(windowId);
    const int previousDesktop = snapState->desktopForWindow(windowId);
    const bool zoneChanged = (previousZones != validZoneIds);
    const bool screenChanged = (previousScreen != screenId);
    const bool desktopChanged = (previousDesktop != virtualDesktop);

    snapState->assignWindowToZones(windowId, validZoneIds, screenId, virtualDesktop);
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
    // Remove BOTH the windowId AND the appId entry unconditionally. The
    // appId-keyed entry only arises through the legacy bulk seeder
    // (setFloatingWindows — unit tests and the pre-engine init window; no
    // production restore path feeds it any more), but gating the appId
    // removal on the windowId removal succeeding would miss exactly that
    // shape. QSet::remove on a missing key is a documented no-op so the
    // unconditional form is cost-equivalent in the no-op path.
    m_floatingWindows.remove(canonicalizeForLookup(windowId));
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
    Q_ASSERT(hasSnapState());
    unassignFromStore(windowId, snapForWindow(windowId));
}

bool WindowTrackingService::unassignFromStore(const QString& windowId, PhosphorSnapEngine::SnapState* snapState)
{
    if (!snapState)
        return false;
    // Capture the removed zones BEFORE the unassign. The window's own store clears
    // its per-key last-used inside unassignWindow; the global holder still carries
    // the representative restored from disk, so clear it too if it named a removed zone.
    const QStringList removedZones = snapState->zonesForWindow(windowId);
    // The desktop this store's assignment belongs to, read BEFORE the
    // unassign clears it. A window present on several desktops has a zone
    // per desktop persisted in its record, and the store MERGES that map on
    // capture, so an unsnap here has to forget its desktop's entry explicitly
    // or a restart snaps the window back into the zone it just left.
    const int unsnappedDesktop = snapState->desktopForWindow(windowId);
    auto result = snapState->unassignWindow(windowId);
    if (!result.wasAssigned) {
        return false;
    }
    if (unsnappedDesktop >= 1) {
        forgetDesktopZones(windowId, PhosphorEngine::WindowPlacement::snapEngineId(), unsnappedDesktop);
    }
    bool lastUsedCleared = result.lastUsedZoneCleared;
    lastUsedCleared |= clearGlobalLastUsedIfRemoved(removedZones, snapState);

    // Another store's membership (another desktop of a spanned window) is
    // not the zone the window reads as holding.
    if (snapState == snapForWindow(windowId)) {
        Q_EMIT windowZoneChanged(windowId, QString());
    }
    markDirty(DirtyZoneAssignments | (lastUsedCleared ? DirtyLastUsedZone : DirtyNone));
    return true;
}

QString WindowTrackingService::zoneForWindow(const QString& windowId) const
{
    Q_ASSERT(hasSnapState());
    const PhosphorSnapEngine::SnapState* snapState = snapForWindow(windowId);
    return snapState ? snapState->zoneForWindow(windowId) : QString();
}

QStringList WindowTrackingService::zonesForWindow(const QString& windowId) const
{
    Q_ASSERT(hasSnapState());
    const PhosphorSnapEngine::SnapState* snapState = snapForWindow(windowId);
    return snapState ? snapState->zonesForWindow(windowId) : QStringList{};
}

QString WindowTrackingService::screenForWindow(const QString& windowId) const
{
    // Delegates to the owning SnapState, which canonicalizes the id — the
    // canonicalizing point accessor external callers use instead of a raw
    // flat-map lookup.
    const PhosphorSnapEngine::SnapState* snapState = snapForWindow(windowId);
    return snapState ? snapState->screenForWindow(windowId) : QString();
}

QString WindowTrackingService::screenForWindow(const QString& windowId, const QString& defaultScreen) const
{
    const PhosphorSnapEngine::SnapState* snapState = snapForWindow(windowId);
    if (!snapState) {
        return defaultScreen;
    }
    // Return defaultScreen when the window has no usable (non-empty) screen
    // assignment. Callers (snap commit / unfloat) always record a real screen, so
    // in practice this matches a with-default map lookup while also
    // canonicalizing the id (issue #628).
    const QString screen = snapState->screenForWindow(windowId);
    return screen.isEmpty() ? defaultScreen : screen;
}

QStringList WindowTrackingService::windowsInZone(const QString& zoneId) const
{
    Q_ASSERT(hasSnapState());
    // A zone lives on one screen, but iterate every store so the query is
    // screen-agnostic (matching the former single store).
    QStringList result;
    for (const PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        result += state->windowsInZone(zoneId);
    }
    // A window on several desktops holds the zone in each store (F182).
    result.removeDuplicates();
    return result;
}

QStringList WindowTrackingService::snappedWindows() const
{
    Q_ASSERT(hasSnapState());
    QStringList result;
    for (const PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        result += state->snappedWindows();
    }
    // A window snapped on several desktops is in several stores; the D-Bus
    // consumers of this list want each window once.
    result.removeDuplicates();
    return result;
}

int WindowTrackingService::pruneStaleAssignments(const QSet<QString>& rawAliveWindowIds)
{
    Q_ASSERT(hasSnapState());
    if (!hasSnapState())
        return 0;
    // Fail closed on an empty alive set (the WindowRegistry twin carries the
    // full rationale): a premature one-shot alive report at login must not
    // wipe every persisted assignment; genuinely-closed windows are pruned
    // per-window on windowClosed.
    if (rawAliveWindowIds.isEmpty()) {
        qCWarning(lcPlacement) << "pruneStaleAssignments: refusing empty alive set";
        return 0;
    }
    // Canonicalize the alive set so it compares like-for-like against the
    // canonical-keyed stores (the WTS-owned sticky / legacy-float sets below, and
    // SnapState's maps). Otherwise a window still alive under a mutated-class
    // composite would be pruned because its stored key is the first-seen one
    // (issue #628). SnapState::pruneStaleAssignments re-canonicalizes defensively,
    // so passing the canonical set there is correct too.
    QSet<QString> aliveWindowIds;
    aliveWindowIds.reserve(rawAliveWindowIds.size());
    for (const QString& id : rawAliveWindowIds) {
        aliveWindowIds.insert(canonicalizeForLookup(id));
    }
    int pruned = 0;
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        pruned += state->pruneStaleAssignments(aliveWindowIds);
    }

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
    // Suspension classification: cleared per-window by the adaptor on unfloat
    // and windowClosed, but a window that dies WITHOUT a close signal — the
    // case this backstop exists for — would leak its entry and hand a later
    // same-canonical window a stale suspension classification. The set is
    // CANONICAL-keyed and aliveWindowIds was canonicalized above, so the shared
    // removeSet comparison is already like-for-like.
    removeSet(m_suspensionFloats);

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
    Q_ASSERT(hasSnapState());
    const PhosphorSnapEngine::SnapState* snapState = snapForWindow(windowId);
    return snapState && snapState->isWindowSnapped(windowId);
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
        PhosphorScreens::ScreenManager* mgr = m_screenManager;
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
        // A resolved target with an EMPTY available rect (mid-hotplug, a VS
        // whose geometry has not landed yet) must not produce a degenerate 0x0
        // "success" — fall through to the generic on-screen validation below,
        // same as when the target cannot be resolved at all.
        if (haveTarget && available.isValid()) {
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

std::optional<QRect> WindowTrackingService::validatedUnmanagedGeometry(const QString& windowId,
                                                                       const QString& screenId) const
{
    if (windowId.isEmpty()) {
        return std::nullopt;
    }
    // SINGLE source of truth for the float-back: the placement record's SHARED
    // per-screen free geometry. (The legacy per-engine m_unmanagedGeometries store
    // is no longer consulted — two parallel stores drifted and leaked the zone/tile
    // rect into float.) Free geometry is shared across modes, so snap and autotile
    // resolve the same value for a given screen.
    // PER-WINDOW, always. An empty appId keeps peek on its same-instance branch,
    // so a window with no usable record of its own gets nullopt rather than a
    // same-app sibling's rect. This used to be opt-in behind an exactOnly flag
    // that defaulted to sharing, and the sharing is what discussion #1028 turned
    // out to be: an app's bucket fills with dead instances at MaxPerApp, a live
    // window with no record borrows a ghost's, and the absolute coordinates in
    // it put the window on whatever monitor that ghost last occupied.
    //
    // "Restore to where this app last floated" was the argument for sharing. It
    // is a convenience with a bad failure mode, and every caller here is asking
    // the per-window question instead. A window with nothing on record simply
    // stays where it is, which is the least surprising outcome — the wording
    // the autotile path used when it opted out on its own.
    const QString appId;
    const auto rec = m_placementStore.peek(windowId, appId, [](const PhosphorEngine::WindowPlacement& p) {
        return p.anyFreeGeometry().isValid();
    });
    if (!rec) {
        return std::nullopt;
    }
    // Resolve an EMPTY screenId to the record's own screen rather than looking
    // up the empty key, which can never hit.
    //
    // The callers reach this with an empty id far more often than the name
    // suggests: WindowTrackingAdaptor's two pre-tile entry points derive it
    // from screenForWindow(), which answers through the window's owning
    // SnapState and returns empty for any window snap does not track — every
    // autotile-only and scroll-only window. With the cross-screen fallback
    // gone, an empty key is a guaranteed miss, so those windows' float-back
    // silently stopped working for reasons that had nothing to do with their
    // geometry. Falling back to the record's own screen asks the question the
    // caller meant; a record with no screen either still yields nullopt below.
    const QString resolvedScreen = screenId.isEmpty() ? rec->screenId : screenId;
    if (resolvedScreen.isEmpty()) {
        return std::nullopt;
    }
    // THIS SCREEN only. There is no cross-screen fallback: a position remembered
    // on another monitor is not a float-back for this one. The fallback used to
    // re-centre such a rect onto screenId, which sounds harmless and is not —
    // it silently relocates a window the user put here, and it only ever
    // guessed, since a window that has never floated on this screen has no
    // remembered spot here to return to. A caller that finds nothing leaves the
    // window where it is, which is both honest and the least surprising
    // outcome.
    //
    // The re-centring in validateGeometryForScreen is still reachable through
    // its other callers; what is gone is this function MANUFACTURING a
    // cross-screen case to feed it. Passing screenId on both sides keeps the
    // on-screen sanity check (a remembered rect can be off-canvas after a
    // resolution change) without any cross-screen adjustment.
    const QRect exact = rec->freeGeometryFor(resolvedScreen);
    if (!exact.isValid()) {
        return std::nullopt;
    }
    // Do not trust the KEY. A record can be mis-keyed — a rect filed under one
    // screen while its coordinates describe another — and the sanity check
    // below cannot catch that, because isGeometryOnScreen asks whether a rect
    // is on ANY screen and a mis-keyed rect is: the wrong one. It would come
    // back verbatim and move the window there, which is exactly the
    // cross-screen restore this function no longer does. Records written
    // before the guard in recordFreeGeometry exist on disk in user sessions,
    // so the read has to defend itself.
    if (!geometryOverlapsScreen(exact, resolvedScreen)) {
        qCWarning(lcPlacement) << "validatedUnmanagedGeometry: record for" << windowId << "is filed under"
                               << resolvedScreen << "but" << exact << "does not lie there — ignoring";
        return std::nullopt;
    }
    return validateGeometryForScreen(exact, resolvedScreen, resolvedScreen);
}

void WindowTrackingService::recordFreeGeometry(const QString& windowId, const QString& screenId, const QRect& geometry,
                                               bool overwrite)
{
    if (windowId.isEmpty() || screenId.isEmpty() || !geometry.isValid()) {
        return;
    }
    // The pair must agree. This map is keyed by screen and holds ABSOLUTE
    // coordinates, so filing a rect under a screen it does not lie on makes the
    // key a lie — and float restore, which is screen-local, then hands that rect
    // back for the wrong monitor and moves the window there. Mis-filing is
    // permanent until the record is evicted, so refusing is the safer
    // direction. Note it does NOT simply "self-heal on the next capture" on the
    // pre-tile path: the capture being refused is the one taken as the window
    // is tiled, and the engine-tiled guard below then refuses every later
    // capture for as long as it stays tiled — so one refusal can mean no
    // float-back for that whole tiled life. That is why the predicate has to be
    // exactly right rather than merely conservative. Warn rather than drop
    // silently: a mismatch means the caller's screenId and geometry came from
    // different moments, and the caller is what needs fixing.
    if (!geometryOverlapsScreen(geometry, screenId)) {
        qCWarning(lcPlacement) << "recordFreeGeometry: refusing" << geometry << "for" << windowId << "under" << screenId
                               << "— the geometry does not lie on that screen";
        return;
    }
    // The float-back holds ONLY a genuine free frame (the refusal model is on
    // the header's float-back docs; the other writers are recordFloatingClose
    // and the adaptor's capture). (O): a window in a zone in view, or tiled in
    // view, stands on its managed rect, so nothing is recorded while it does;
    // the engine-backed checks survive an effect reload, which the effect's own
    // capture guard does not. (M): a rect equal to a managed frame is refused
    // even for a free window, the one that just left its zone or tile. No
    // window-state refusal here: an explicit rect (a maximized window's restore
    // rect) is recorded whatever the window's state; callers that sample a
    // frame refuse a minimized or output-filling one. No size-only test either.
    if (occupiesZoneInView(windowId)) {
        qCDebug(lcPlacement) << "recordFreeGeometry: refusing the frame of" << windowId << "— it is in a zone";
        return;
    }
    if (isWindowEngineTiled(windowId)) {
        qCDebug(lcPlacement) << "recordFreeGeometry: refusing the frame of" << windowId << "— it is tiled";
        return;
    }
    if (isManagedFrame(windowId, geometry)) {
        qCDebug(lcPlacement) << "recordFreeGeometry: refusing" << geometry << "for" << windowId
                             << "— it is a managed frame";
        return;
    }
    const QString appId = currentAppIdFor(windowId);
    if (appId.isEmpty()) {
        return;
    }
    if (!overwrite) {
        // First-capture-wins is a PER-WINDOW contract: only this window's own
        // record may suppress the write. A same-app sibling's free geometry
        // must not block recording this window's first genuine spot (it would
        // never persist its own position while the sibling's record lives).
        const auto existing = m_placementStore.peekExact(windowId);
        if (existing && existing->freeGeometryFor(screenId).isValid()) {
            return; // first-capture-wins
        }
    }
    // A geometry-only partial: no engine slot, so record()'s merge leaves the
    // managed context (screen/desktop/activity) untouched and only updates this
    // screen's free geometry. For a FRESH record (no prior entry) the float
    // screen becomes the record's screenId by construction — deliberate: it
    // is the only screen the store knows for the window, and the first
    // managed capture overwrites it with the real managed context.
    PhosphorEngine::WindowPlacement p;
    p.windowId = windowId;
    p.appId = appId;
    p.screenId = screenId;
    p.freeGeometryByScreen.insert(screenId, geometry);
    if (m_placementStore.record(p)) {
        markDirty(DirtyWindowPlacements);
    }
}

void WindowTrackingService::downgradeMismatchedManagedSlots(PhosphorEngine::WindowPlacement& placement,
                                                            const QString& recordedScreenId,
                                                            const QString& closeScreenId)
{
    // A managed slot inherited from a DIFFERENT screen references that
    // screen's zones / tile order — restoring it under the new screen would
    // land the window in another screen's slots. Downgrade mismatched managed
    // slots to plain floating (the state the window actually closed in);
    // same-screen and unscreened records pass through untouched.
    if (recordedScreenId.isEmpty() || PhosphorScreens::ScreenIdentity::screensMatch(recordedScreenId, closeScreenId)) {
        return;
    }
    for (auto it = placement.engines.begin(); it != placement.engines.end(); ++it) {
        if (it->state == PhosphorEngine::WindowPlacement::stateSnapped()
            || it->state == PhosphorEngine::WindowPlacement::stateTiled()) {
            it->state = PhosphorEngine::WindowPlacement::stateFloating();
            it->zoneIds.clear();
            // The per-desktop map names that other screen's zones too. This
            // clears only the copy: record() merges the stored map back in, so
            // the caller forgets mismatchedDesktopZones() after recording, or
            // the map would be seeded into THIS screen's stores on restore.
            it->zonesByDesktop.clear();
            it->order = -1;
        }
    }
}

QList<QPair<QString, int>>
WindowTrackingService::mismatchedDesktopZones(const PhosphorEngine::WindowPlacement& placement,
                                              const QString& recordedScreenId, const QString& closeScreenId)
{
    QList<QPair<QString, int>> entries;
    if (recordedScreenId.isEmpty() || PhosphorScreens::ScreenIdentity::screensMatch(recordedScreenId, closeScreenId)) {
        return entries;
    }
    for (auto it = placement.engines.cbegin(); it != placement.engines.cend(); ++it) {
        if (it->state != PhosphorEngine::WindowPlacement::stateSnapped()
            && it->state != PhosphorEngine::WindowPlacement::stateTiled()) {
            continue;
        }
        for (auto desktop = it->zonesByDesktop.cbegin(); desktop != it->zonesByDesktop.cend(); ++desktop) {
            entries.append({it.key(), desktop.key()});
        }
    }
    return entries;
}

void WindowTrackingService::recordFloatingClose(const QString& windowId, const QString& screenId, const QRect& geometry)
{
    if (windowId.isEmpty() || screenId.isEmpty() || !geometry.isValid()) {
        return;
    }
    // Never let a tile rect OR a zone rect become the float-back — the same
    // pair of invariants recordFreeGeometry enforces. (An orphaned
    // cross-screen-dragged window is floating, so both are belt-and-braces
    // here — but the sibling carries both guards and this map is shared.)
    if (isWindowEngineTiled(windowId)) {
        return;
    }
    // NO snapped-and-not-floating twin here, unlike recordFreeGeometry:
    // this path is reached only when BOTH engines' capturePlacement already
    // declined, and the snap-assignment read can be STALE — a zone
    // assignment deliberately preserved across an autotile flip would make
    // the guard refuse the one capture (screen adoption + sibling prune)
    // nothing else performs.
    // The minimized guard DOES apply: a minimized close's frame is the
    // hidden rect, not a free position, and that read is never stale (S).
    if (m_windowRegistry && m_windowRegistry->minimizedState(windowId).value_or(false)) {
        qCDebug(lcPlacement) << "recordFloatingClose: refusing minimized frame for" << windowId;
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
    // The insert takes the pair check (P), as recordFreeGeometry does: the
    // caller pairs the effect's close-time screen with the last frame shadow,
    // two samples from two moments. It takes (M) and the output-filling half of
    // (S) too: the frame of a maximized or fullscreen window, or one still on
    // its zone or tile, is not a free position (F157).
    //
    // It gates ONLY the geometry insert, never the whole function: the screen
    // adoption, the owning-engine slot synthesis and the pure-float sibling
    // collapse below are why this path exists, and an early return would
    // forfeit all three to fix a field none of them reads. geometryOverlapsScreen
    // fails OPEN with no ScreenManager, so an embedder without one keeps
    // today's behaviour instead of silently losing every close capture.
    const bool fillsOutput = m_windowRegistry && m_windowRegistry->fillsOutputState(windowId).value_or(false);
    if (!geometryOverlapsScreen(geometry, screenId)) {
        qCWarning(lcPlacement) << "recordFloatingClose: refusing" << geometry << "for" << windowId << "under"
                               << screenId << "— the geometry does not lie on that screen; recording the rest";
    } else if (!fillsOutput && !isManagedFrame(windowId, geometry)) {
        p.freeGeometryByScreen.insert(screenId, geometry);
    }
    // Preserve the existing record's per-engine slots and context. Carrying a
    // non-empty engine map is what makes the store merge adopt the new screenId
    // (a geometry-only partial, like recordFreeGeometry, would leave the stale
    // managed screen in place — exactly the bug this fixes).
    // Same-instance record only: inheriting a same-app SIBLING's engine slots would
    // graft its snapped/tiled placement under THIS windowId — a reopen then
    // restores two windows into the sibling's zone and corrupts the per-app
    // FIFO distribution. A window with no record of its own takes the
    // synthesized-floating-slot branch below, which exists for exactly that.
    QList<QPair<QString, int>> staleDesktopZones;
    if (const auto existing = m_placementStore.peekExact(windowId)) {
        p.virtualDesktop = existing->virtualDesktop;
        p.activity = existing->activity;
        p.kind = existing->kind;
        p.engines = existing->engines;
        staleDesktopZones = mismatchedDesktopZones(p, existing->screenId, screenId);
        downgradeMismatchedManagedSlots(p, existing->screenId, screenId);
    }
    // Synthesize the OWNING engine's floating slot whenever the record lacks
    // one — not merely when the map is empty. The slot's engine id is
    // load-bearing: the tiling engines' reopen accept reads strictly their
    // OWN slot, so a record carrying only FOREIGN slots (another mode's or
    // screen's life) satisfies neither the accept nor the old isEmpty gate,
    // and the window that closed floating reopened tiled. Adding (never
    // re-keying) preserves the other engines' per-mode memory. A non-empty
    // engine map is also what makes record()'s merge adopt this close's
    // screen as the managed context.
    const QString owningEngine = owningModeEngineId(windowId, screenId);
    if (!p.engines.contains(owningEngine)) {
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        p.engines.insert(owningEngine, slot);
    }
    if (m_placementStore.record(p)) {
        markDirty(DirtyWindowPlacements);
    }
    for (const auto& [engineId, desktop] : std::as_const(staleDesktopZones)) {
        forgetDesktopZones(windowId, engineId, desktop);
    }
    // Close-capture convergence: this orphaned cross-screen close is the freshest
    // authority for the app's float-back, so drop stale pure-float duplicates on
    // the same screen (see WindowPlacementStore::collapsePureFloatSiblings). Mark
    // dirty when it pruned — the record() above may have been a no-op, leaving this
    // as the only mutation to persist.
    if (m_placementStore.collapsePureFloatSiblings(appId, windowId)) {
        markDirty(DirtyWindowPlacements);
    }
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

void WindowTrackingService::clearFreeGeometry(const QString& windowId, const QString& screenId)
{
    if (windowId.isEmpty()) {
        return;
    }
    if (screenId.isEmpty()) {
        clearFreeGeometry(windowId);
        return;
    }
    if (m_placementStore.clearFreeGeometry(windowId, screenId)) {
        markDirty(DirtyWindowPlacements);
    }
}

void WindowTrackingService::releaseEngineSlot(const QString& windowId, const QString& engineId)
{
    if (windowId.isEmpty() || engineId.isEmpty()) {
        return;
    }
    // Wrapper, not a direct store call from the engines, for the same reason
    // every other store mutation has one: the store has no dirty concept, so
    // a release that only lives in memory would leave the saved config
    // carrying the stale managed slot — and after a daemon restart a stale
    // managed slot would read as a restorable home again, defeating the
    // release across exactly the boundary it matters at.
    if (m_placementStore.releaseEngineSlot(windowId, engineId)) {
        markDirty(DirtyWindowPlacements);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Floating Window State
// ═══════════════════════════════════════════════════════════════════════════════

bool WindowTrackingService::isSuspensionFloat(const QString& windowId) const
{
    return m_suspensionFloats.contains(canonicalizeForLookup(windowId));
}

void WindowTrackingService::markSuspensionFloat(const QString& windowId)
{
    m_suspensionFloats.insert(canonicalizeForLookup(windowId));
}

void WindowTrackingService::clearSuspensionFloat(const QString& windowId)
{
    m_suspensionFloats.remove(canonicalizeForLookup(windowId));
}

bool WindowTrackingService::isWindowFloating(const QString& windowId) const
{
    // Per-engine answer: when the daemon has wired the resolver, the float bit
    // is the float state of the engine that owns the window's CURRENT screen
    // mode (SnapState::isFloating for Snapping, TilingState::isFloating for
    // Autotile, ScrollState's float set for Scrolling). A window floated in
    // one engine is NOT floating in the others, so the engines never share a
    // bit.
    if (m_engineFloatResolver) {
        return m_engineFloatResolver(windowId);
    }

    // Legacy fallback (unit tests / early init before engines are wired):
    // Try full window ID first (runtime - distinguishes multiple instances).
    // Canonical key, like the sticky map: a class-mutating window (issue
    // #628) must resolve — and be prunable — under one composite.
    if (m_floatingWindows.contains(canonicalizeForLookup(windowId))) {
        return true;
    }
    // appId fallback: only the legacy bulk seeder (setFloatingWindows — unit
    // tests / pre-engine init) writes bare-appId entries; no production
    // restore path feeds this any more.
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
    // maintain the shared set + snap state directly. Canonical keys
    // throughout (see isWindowFloating's lookup).
    if (floating) {
        m_floatingWindows.insert(canonicalizeForLookup(windowId));
    } else {
        m_floatingWindows.remove(canonicalizeForLookup(windowId));
        // Also remove app ID entry (session-restored entries)
        QString appId = currentAppIdFor(windowId);
        if (appId != windowId) {
            m_floatingWindows.remove(appId);
        }
    }

    if (PhosphorSnapEngine::SnapState* snapState = snapForWindow(windowId)) {
        snapState->setFloating(windowId, floating);
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

template<typename Func>
auto WindowTrackingService::preFloatLookup(const QString& windowId, Func&& getter) const
    -> decltype(getter(std::declval<PhosphorSnapEngine::SnapState*>(), windowId))
{
    // The window's own entry in its primary store (the membership in view
    // when it has one there): another window of the app never answers, and
    // a window on several desktops reads the desktop it is seen on (F306,
    // F312).
    PhosphorSnapEngine::SnapState* const state = snapForWindow(windowId);
    if (!state) {
        return {};
    }
    return getter(state, windowId);
}

QString WindowTrackingService::preFloatZone(const QString& windowId) const
{
    return preFloatLookup(windowId, [](PhosphorSnapEngine::SnapState* state, const QString& id) {
        return state->preFloatZone(id);
    });
}

QStringList WindowTrackingService::preFloatZones(const QString& windowId) const
{
    return preFloatLookup(windowId, [](PhosphorSnapEngine::SnapState* state, const QString& id) {
        return state->preFloatZones(id);
    });
}

QString WindowTrackingService::preFloatScreen(const QString& windowId) const
{
    return preFloatLookup(windowId, [](PhosphorSnapEngine::SnapState* state, const QString& id) {
        return state->preFloatScreen(id);
    });
}

void WindowTrackingService::clearPreFloatZone(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Every store's entry for the window: a close, a virtual-screen migration
    // and the float-sync arms forget it everywhere.
    bool removed = false;
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        removed = state->clearPreFloatZone(windowId) || removed;
    }
    // Mark dirty on a real removal so the cleared zone does not resurrect
    // from disk on the next restart (the autotile float-sync clear path has
    // no other persistence trigger).
    if (removed) {
        markDirty(DirtyPreFloatZones | DirtyPreFloatScreens);
    }
}

bool WindowTrackingService::clearFloatingForSnap(const QString& windowId)
{
    if (!isWindowFloating(windowId)) {
        return false;
    }
    setWindowFloating(windowId, false);
    // Only the store it is snapped in: another desktop's pre-float zone is
    // that desktop's to unfloat into (F312).
    if (PhosphorSnapEngine::SnapState* const state = snapForWindow(windowId);
        state && state->clearPreFloatZone(windowId)) {
        markDirty(DirtyPreFloatZones | DirtyPreFloatScreens);
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shared Helpers
// ═══════════════════════════════════════════════════════════════════════════════

// sortZonesByNumber / buildZonePositionMap removed — callers should use
// PhosphorZones::LayoutUtils directly.

// ═══════════════════════════════════════════════════════════════════════════════
// Out-of-line accessors delegating to SnapState
// ═══════════════════════════════════════════════════════════════════════════════

bool WindowTrackingService::clearGlobalLastUsedIfRemoved(const QStringList& removedZones,
                                                         const PhosphorSnapEngine::SnapState* owningStore)
{
    PhosphorSnapEngine::SnapState* globals = snapGlobals();
    if (globals && globals != owningStore && !globals->lastUsedZoneId().isEmpty()
        && removedZones.contains(globals->lastUsedZoneId())) {
        globals->restoreLastUsedZone({}, {}, {}, 0);
        return true;
    }
    return false;
}

// The lastUsed* accessors are read during the WTA constructor's loadState()
// call — which runs BEFORE Daemon::init wires the snap-state resolver via
// setSnapStateResolver(). Returning a sentinel (empty string / 0) when SnapState isn't yet
// attached lets early-init readers (the setLastUsedZone restore in
// WindowTrackingAdaptor::loadState) pass through harmlessly instead of
// asserting and crashing the daemon on startup. The snap-engine's own lastUsedZone
// state is loaded later from KConfig through its persistence delegate
// once SnapState is wired, so the early-init read here can only ever
// produce a "no last zone yet" result anyway.
PhosphorSnapEngine::SnapState* WindowTrackingService::snapRepresentativeLastUsed() const
{
    PhosphorSnapEngine::SnapState* best = nullptr;
    quint64 bestSeq = 0;
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        if (!state || state->lastUsedZoneId().isEmpty()) {
            continue;
        }
        // Seqs are unique among non-empty-last-used stores (nextLastUsedSeq() is
        // monotonic; only fromJson — dead in the live path — sets a last-used zone
        // without bumping the seq), so there is never a tie here. Strict `>` over `>=`
        // is a harmless no-tie guard, not a substitute for that uniqueness: a genuine
        // tie would still resolve by unordered iteration order.
        if (!best || state->lastUsedSeq() > bestSeq) {
            best = state;
            bestSeq = state->lastUsedSeq();
        }
    }
    return best ? best : snapGlobals();
}

QString WindowTrackingService::lastUsedZoneId() const
{
    const PhosphorSnapEngine::SnapState* rep = snapRepresentativeLastUsed();
    return rep ? rep->lastUsedZoneId() : QString();
}

QString WindowTrackingService::lastUsedZoneClass() const
{
    const PhosphorSnapEngine::SnapState* rep = snapRepresentativeLastUsed();
    return rep ? rep->lastUsedZoneClass() : QString();
}

QString WindowTrackingService::lastUsedScreenName() const
{
    const PhosphorSnapEngine::SnapState* rep = snapRepresentativeLastUsed();
    return rep ? rep->lastUsedScreenId() : QString();
}

int WindowTrackingService::lastUsedDesktop() const
{
    const PhosphorSnapEngine::SnapState* rep = snapRepresentativeLastUsed();
    return rep ? rep->lastUsedDesktop() : 0;
}

void WindowTrackingService::retagLastUsedZoneClass(const QString& newClass)
{
    // The assert has a release-build partner, unlike a bare one: with no snap
    // state, snapAllStates() is empty and lastUsedZoneClass() answers empty, so
    // the oldClass guard below returns before the loop and the loop itself
    // null-checks each store. The assert is the debug-build shout; the early
    // return is what makes a release build safe.
    Q_ASSERT(hasSnapState());
    // Last-used is per-key: retag every store whose last-used class matches the one
    // the representative currently reports (the class of the window that was
    // renamed). Stores tracking a different app's last-used are left untouched.
    const QString oldClass = lastUsedZoneClass();
    if (oldClass.isEmpty() || oldClass == newClass) {
        return;
    }
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        if (state && state->lastUsedZoneClass() == oldClass) {
            state->retagLastUsedZoneClass(newClass);
        }
    }
}

const QSet<QString>& WindowTrackingService::userSnappedClasses() const
{
    static const QSet<QString> empty;
    // The hold check runs BEFORE the state assert, not after: the hold is
    // engaged exactly when no SnapState is wired, so asserting first would
    // abort a debug build on the one path this arm exists to serve.
    //
    // The HOLD wins while it is engaged. setUserSnappedClasses stashes the
    // disk-loaded classes when no SnapState is wired yet, and saveState
    // serialises UserSnappedClasses by iterating exactly this getter — so
    // reading past the hold meant a save landing between the adaptor's
    // loadState() and setSnapStateResolver() wrote the EMPTY set back over
    // the user's auto-snap-by-class list. Reading it here closes the window
    // the stash was added to close.
    if (m_pendingUserSnappedClasses) {
        return *m_pendingUserSnappedClasses;
    }
    Q_ASSERT(hasSnapState());
    const PhosphorSnapEngine::SnapState* globals = snapGlobals();
    return globals ? globals->userSnappedClasses() : empty;
}

void WindowTrackingService::setUserSnappedClasses(const QSet<QString>& classes)
{
    PhosphorSnapEngine::SnapState* globals = snapGlobals();
    if (!globals) {
        // STASH rather than drop. The adaptor's constructor calls loadState()
        // — which lands here with the disk-loaded classes — before the daemon
        // wires the snap-state resolver, and this is the only producer for
        // this key. Dropping made recovery incidental (a second loadState via
        // the autotile engine's persistence delegate happens to run after
        // wiring); if that ordering ever changed, the classes were gone AND
        // the next save wrote the emptied set back over them. Flushed from
        // setSnapStateResolver / setSnapState.
        qCDebug(lcPlacement) << "setUserSnappedClasses: no SnapState yet — holding" << classes.size()
                             << "classes until one is wired";
        m_pendingUserSnappedClasses = classes;
        return;
    }
    m_pendingUserSnappedClasses.reset();
    globals->setUserSnappedClasses(classes);
}

void WindowTrackingService::flushPendingUserSnappedClasses()
{
    if (!m_pendingUserSnappedClasses) {
        return;
    }
    PhosphorSnapEngine::SnapState* globals = snapGlobals();
    if (!globals) {
        return;
    }
    qCInfo(lcPlacement) << "Flushing" << m_pendingUserSnappedClasses->size()
                        << "held user-snapped classes now that a SnapState is wired";
    globals->setUserSnappedClasses(*m_pendingUserSnappedClasses);
    m_pendingUserSnappedClasses.reset();
}

QRect WindowTrackingService::resolveZoneGeometry(const QStringList& zoneIds, const QString& screenId) const
{
    if (zoneIds.isEmpty()) {
        return QRect();
    }
    return (zoneIds.size() > 1) ? multiZoneGeometry(zoneIds, screenId) : zoneGeometry(zoneIds.first(), screenId);
}

} // namespace PhosphorPlacement
