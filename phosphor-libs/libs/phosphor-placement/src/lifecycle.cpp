// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Window lifecycle, layout change handling, state management, and private helpers.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include <PhosphorPlacement/WindowTrackingService.h>
#include "placementutils.h"

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorScreens/VirtualScreen.h>
#include "placementlogging.h"
#include <QScreen>
#include <QUuid>
#include <climits>

namespace PhosphorPlacement {

namespace {

QRect clampToRect(const QRect& geometry, const QRect& bounds)
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
    // Nothing reads the window's kind any more. It gated the pending-restore
    // queue this close used to write, which had no reader and was removed; a
    // reopen restores from the placement store the adaptor's capture feeds.
    Q_UNUSED(kind)
    if (!hasSnapState())
        return;
    PhosphorSnapEngine::SnapState* snapState = snapForWindow(windowId);

    // Registry-aware, so a window that renamed mid-session (Electron/CEF)
    // clears the legacy float alias under its CURRENT class.
    const QString appId = currentAppIdFor(windowId);

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
    m_floatingWindows.remove(canonicalizeForLookup(windowId));
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
    pruneStaleZoneAssignments();
}

WindowTrackingService::ZonePruneResult WindowTrackingService::pruneStaleZoneAssignments()
{
    ZonePruneResult result;
    if (!hasSnapState() || !m_layoutManager)
        return result;

    const QString currentActivity = m_layoutManager->currentActivity();
    // Every zone id a registered layout holds. An id outside it names a zone
    // that exists nowhere any more (an editor delete, a deleted layout), and
    // is dropped in every context and mode, a None context included (F431).
    const QHash<QString, int> liveIds =
        PhosphorZones::LayoutUtils::buildGlobalZonePositionMap(m_layoutManager->layouts());

    // Whether snapping runs LIVE on a screen: a configured tiling mode whose
    // engine is off runs snapping and is judged like it (F112). Without a
    // snap engine (unit tests) the configured id answers.
    QHash<QString, bool> snapsLive;
    const auto runsSnapping = [&](const QString& screenId, int shownDesktop) {
        auto it = snapsLive.constFind(screenId);
        if (it == snapsLive.constEnd()) {
            bool live = false;
            if (m_snapEngine) {
                live = m_snapEngine->isActiveOnScreen(screenId);
            } else {
                const QString id = m_layoutManager->assignmentIdForScreen(screenId, shownDesktop, currentActivity);
                live = !PhosphorLayout::LayoutId::isAutotile(id) && !PhosphorLayout::LayoutId::isScrolling(id);
            }
            it = snapsLive.insert(screenId, live);
        }
        return *it;
    };

    struct Visit
    {
        QString windowId;
        QStringList survivors;
        QString screenId;
        int desktop = 0;
        QString activity;
        PhosphorSnapEngine::SnapState* store = nullptr;
    };
    QVector<Visit> visits;
    // Collect, then mutate the visited stores.
    forEachZoneAssignedWindow([&](const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                  int desktop, const QString& activity, PhosphorSnapEngine::SnapState* store) {
        QStringList survivors;
        for (const QString& zoneId : zoneIds) {
            if (liveIds.contains(zoneId)) {
                survivors.append(zoneId);
            }
        }
        // A membership in view on a screen snapping runs on also loses the
        // zones its context's layout lacks. Another desktop or activity keeps
        // them for its own layout, a tiling screen keeps them for its return
        // to snapping, and an unknown current desktop is not in view. A None
        // context (no layout) keeps every zone it holds (F264).
        const int shown = m_layoutManager->currentVirtualDesktopForScreen(screenId);
        const bool inView =
            (desktop == 0 || (shown > 0 && desktop == shown)) && activityInView(activity, currentActivity);
        if (inView && !survivors.isEmpty() && runsSnapping(screenId, shown)) {
            if (PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId)) {
                survivors.removeIf([layout](const QString& zoneId) {
                    const auto uuid = parseUuid(zoneId);
                    return !uuid || !layout->zoneById(*uuid);
                });
            }
        }
        if (!zoneIds.isEmpty() && survivors == zoneIds) {
            return;
        }
        visits.append({windowId, survivors, screenId, desktop, activity, store});
    });

    const auto holdsAnyZone = [this](const QString& windowId) {
        const PhosphorSnapEngine::SnapState* owner = snapForWindow(windowId);
        for (const PhosphorSnapEngine::SnapState* state : snapAllStates()) {
            if ((state == owner || snapHoldsWindow(windowId, state)) && !state->zonesForWindow(windowId).isEmpty()) {
                return true;
            }
        }
        return false;
    };

    for (const Visit& v : std::as_const(visits)) {
        if (v.survivors.isEmpty()) {
            // In the store that holds it, not through the primary: a
            // background store's prune used to unassign the window's zone in
            // view (F127).
            unassignFromStore(v.windowId, v.store);
            // Nothing left anywhere: the record's snap slot goes too, or the
            // next switch's populate re-admits the window from it (F567).
            if (!holdsAnyZone(v.windowId) && !v.store->isFloating(v.windowId)) {
                releaseEngineSlot(v.windowId, PhosphorEngine::WindowPlacement::snapEngineId());
            }
            if (zoneForWindow(v.windowId).isEmpty()) {
                PhosphorEngine::ZoneAssignmentEntry entry;
                entry.windowId = v.windowId;
                entry.targetScreenId = v.screenId;
                result.unsnapped.append(entry);
            }
            continue;
        }
        // A store that holds the window floating keeps its preserved zones.
        if (v.store->isFloating(v.windowId)) {
            continue;
        }
        // Written into the visited store with its own desktop. The service's
        // assign pins currentActivity()'s store, which would plant the span
        // in the activity in view (F127).
        const bool primary = v.store == snapForWindow(v.windowId);
        const QString previousPrimary = primary ? v.store->zoneForWindow(v.windowId) : QString();
        v.store->assignWindowToZones(v.windowId, v.survivors, v.screenId, v.desktop);
        if (primary && previousPrimary != v.survivors.first()) {
            Q_EMIT windowZoneChanged(v.windowId, v.survivors.first());
        }
        markDirty(DirtyZoneAssignments);
        // The batch commit pins the activity in view, so only its spans are
        // re-stated to the effect; the window moves to what is left (F442).
        if (activityInView(v.activity, currentActivity)) {
            PhosphorEngine::ZoneAssignmentEntry entry;
            entry.windowId = v.windowId;
            entry.targetZoneId = v.survivors.first();
            if (v.survivors.size() > 1) {
                entry.targetZoneIds = v.survivors;
            }
            entry.targetGeometry = resolveZoneGeometry(v.survivors, v.screenId);
            entry.targetScreenId = v.screenId;
            entry.virtualDesktop = v.desktop;
            entry.restatement = true;
            if (entry.targetGeometry.isValid()) {
                result.narrowed.append(entry);
            }
        }
    }
    return result;
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
    PhosphorScreens::ScreenManager* mgr = m_screenManager;
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

bool WindowTrackingService::geometryOverlapsScreen(const QRect& geometry, const QString& screenId) const
{
    if (!geometry.isValid() || screenId.isEmpty()) {
        return true; // Nothing to check against — fail open.
    }
    PhosphorScreens::ScreenManager* mgr = m_screenManager;
    if (!mgr) {
        return true;
    }
    // Resolve containment against the PHYSICAL output, even when the key names
    // a virtual screen. Virtual screens subdivide ONE output and share its
    // coordinate space, so the question here — do these coordinates belong to
    // the space this key names — is answered by the output. screenGeometry()
    // returns the VS SUB-RECT for a virtual id, and a floating window is by
    // definition not engine-managed, so nothing constrains it to stay inside
    // one subdivision: validating against the sub-rect turns a routine, legal
    // position into a refusal. The guard's actual purpose is catching a rect
    // filed under the wrong MONITOR, and two virtual screens on one output are
    // not two monitors, so widening to the output loses none of its value.
    const QString containmentId = PhosphorIdentity::VirtualScreenId::isVirtual(screenId)
        ? PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId)
        : screenId;
    const QRect screenGeo = mgr->screenGeometry(containmentId);
    if (!screenGeo.isValid()) {
        return true;
    }
    const QRect intersection = geometry.intersected(screenGeo);
    // Thresholds CLAMPED to the window's own size. MinVisibleWidth/Height were
    // written for isGeometryOnScreen's rescue question ("is any part of this
    // visible anywhere"), where a flat 100px floor is the right answer. The
    // question here is different, and an unclamped floor refuses every window
    // smaller than 100x100 that is sitting entirely and correctly on its own
    // screen — which silently drops the float-back capture for every small
    // utility, palette and dialog window, and then refuses to read any such
    // record already on disk.
    // Half the window's own extent, capped at the flat floor. A bare
    // min(floor, extent) would demand FULL containment for anything under the
    // floor — a 60px palette pulled half over a monitor edge would be refused
    // where a 200px window needs only 100px of overlap — so a small window
    // would be held to a stricter rule than a large one, which is the opposite
    // of the point. Halving keeps the requirement proportional at both sizes.
    const int requiredWidth = std::min(MinVisibleWidth, std::max(1, geometry.width() / 2));
    const int requiredHeight = std::min(MinVisibleHeight, std::max(1, geometry.height() / 2));
    return intersection.width() >= requiredWidth && intersection.height() >= requiredHeight;
}

QRect WindowTrackingService::adjustGeometryToScreen(const QRect& geometry) const
{
    // Try virtual/effective screens first via PhosphorScreens::ScreenManager
    PhosphorScreens::ScreenManager* mgr = m_screenManager;
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

    PhosphorScreens::ScreenManager* smgr = m_screenManager;
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

void WindowTrackingService::setAutotileModePredicate(AutotileModePredicate predicate)
{
    m_autotileModePredicate = std::move(predicate);
}

bool WindowTrackingService::isWindowInAutotileMode(const QString& windowId) const
{
    return m_autotileModePredicate && m_autotileModePredicate(windowId);
}

void WindowTrackingService::setEngineTiledPredicate(EngineTiledPredicate predicate)
{
    m_engineTiledPredicate = std::move(predicate);
}

bool WindowTrackingService::isWindowEngineTiled(const QString& windowId) const
{
    return m_engineTiledPredicate && m_engineTiledPredicate(windowId);
}

void WindowTrackingService::setModeEngineIdResolver(ModeEngineIdResolver resolver)
{
    m_modeEngineIdResolver = std::move(resolver);
}

QString WindowTrackingService::owningModeEngineId(const QString& windowId, const QString& screenId) const
{
    if (m_modeEngineIdResolver) {
        const QString resolved = m_modeEngineIdResolver(windowId, screenId);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }
    return QString(PhosphorEngine::WindowPlacement::snapEngineId());
}

std::optional<PhosphorEngine::WindowPlacement>
WindowTrackingService::claimPlacementForOpen(const QString& windowId, const QString& openingScreenId,
                                             const QString& openingEngineId)
{
    using PhosphorEngine::WindowPlacement;
    using Mode = PhosphorZones::AssignmentEntry::Mode;
    const QString appId = currentAppIdFor(windowId);
    std::function<bool(const WindowPlacement&)> restorableHere;
    if (!openingEngineId.isEmpty()) {
        const QLatin1String engine = openingEngineId == WindowPlacement::autotileEngineId()
            ? WindowPlacement::autotileEngineId()
            : (openingEngineId == WindowPlacement::scrollingEngineId() ? WindowPlacement::scrollingEngineId()
                                                                       : WindowPlacement::snapEngineId());
        const QLatin1String managed =
            engine == WindowPlacement::snapEngineId() ? WindowPlacement::stateSnapped() : WindowPlacement::stateTiled();
        const Mode mode = engine == WindowPlacement::autotileEngineId()
            ? Mode::Autotile
            : (engine == WindowPlacement::scrollingEngineId() ? Mode::Scrolling : Mode::Snapping);
        // Permissive without a layout manager (tests), like every sibling gate.
        const auto inMode = [this, mode](const QString& screenId, int desktop, const QString& activity) {
            return !m_layoutManager || m_layoutManager->modeForScreen(screenId, desktop, activity) == mode;
        };
        restorableHere = [=](const WindowPlacement& p) {
            const auto slot = p.slotFor(engine);
            if (slot.state == managed) {
                if (p.screenId.isEmpty() || p.screenId == openingScreenId) {
                    return p.screenId.isEmpty() || inMode(p.screenId, p.virtualDesktop, p.activity);
                }
                return PhosphorEngine::pendingCrossScreenManagedRestore(p, engine, managed, windowId, openingScreenId,
                                                                        inMode);
            }
            // A float is screen-local: only on this very screen, and only with a
            // free geometry the window can float back to here.
            return slot.state == WindowPlacement::stateFloating()
                && (p.screenId.isEmpty() || p.screenId == openingScreenId)
                && p.freeGeometryFor(openingScreenId).isValid();
        };
    }
    return placementStore().claimForOpen(windowId, appId, openingScreenId, restorableHere);
}

PhosphorEngine::WindowRegistry* WindowTrackingService::windowRegistry() const
{
    return m_windowRegistry;
}

PhosphorScreens::ScreenManager* WindowTrackingService::screenManager() const
{
    return m_screenManager;
}

QRect WindowTrackingService::screenAvailableGeometry(const QString& screenId) const
{
    if (!m_screenManager || screenId.isEmpty()) {
        return {};
    }
    return m_screenManager->screenAvailableGeometry(screenId);
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

QVector<WindowTrackingService::ResnapEntry> WindowTrackingService::takeResnapBuffer()
{
    return std::exchange(m_resnapBuffer, {});
}

void WindowTrackingService::markLastUsedZoneDirty()
{
    markDirty(DirtyLastUsedZone);
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
