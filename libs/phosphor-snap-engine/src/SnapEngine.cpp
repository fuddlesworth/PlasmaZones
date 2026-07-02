// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorSnapEngine/snapnavigationtargets.h>
#include <PhosphorSnapEngine/INavigationStateProvider.h>
#include <PhosphorSnapEngine/IZoneAdjacencyResolver.h>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/AssignmentEntry.h>
#include "snapenginelogging.h"

namespace PhosphorSnapEngine {

using PhosphorEngine::NavigationContext;

// In production (Daemon::start) all dependencies are non-null. Headless unit
// tests deliberately pass nullptr to construct an engine with minimal parents
// for testing peripheral classes (adaptors, bridges) — every method that
// dereferences a dependency guards it locally. Do not Q_ASSERT here.
SnapEngine::SnapEngine(PhosphorZones::LayoutRegistry* layoutManager,
                       PhosphorEngine::IWindowTrackingService* windowTracker,
                       PhosphorZones::IZoneDetector* zoneDetector, PhosphorEngine::IVirtualDesktopManager* vdm,
                       QObject* parent)
    : PlacementEngineBase(parent)
    , m_layoutManager(layoutManager)
    , m_windowTracker(windowTracker)
    , m_globals(new SnapState(QString(), this))
    , m_zoneDetector(zoneDetector)
    , m_virtualDesktopManager(vdm)
{
    // The global-scalar holder lives in the per-screen map under the empty-screen
    // key so whole-store enumerations (snappedWindows / floatingWindows /
    // buildOccupiedZoneSet / the flat-map views) iterate it transparently. It
    // carries no zone/screen assignments — only the still-global last-used-zone and
    // user-snapped scalars, plus any screenless float bookkeeping.
    m_states.insertState(PhosphorEngine::PlacementStateKey{}, m_globals);
}

void SnapEngine::setWindowRegistry(PhosphorEngine::IWindowRegistry* registry)
{
    m_windowRegistry = registry;
    for (SnapState* state : m_states.states()) {
        if (state) {
            state->setWindowRegistry(registry);
        }
    }
}

QString SnapEngine::canonicalWindowId(const QString& rawWindowId) const
{
    return m_windowRegistry ? m_windowRegistry->canonicalizeForLookup(rawWindowId) : rawWindowId;
}

SnapState* SnapEngine::ensureStateForKey(const PhosphorEngine::PlacementStateKey& key)
{
    // The empty-screen key is the still-global-scalar holder (last-used-zone +
    // user-snapped classes, plus any screenless float bookkeeping). Every real
    // (screen, desktop, activity) key gets its own SnapState, lazily created and
    // parented to the engine, seeded with the shared window registry so its keys
    // canonicalize like every other store (issue #628).
    if (key.screenId.isEmpty()) {
        return m_globals;
    }
    return m_states.forKey(key, [this, &key]() -> SnapState* {
        auto* state = new SnapState(key.screenId, this);
        state->setWindowRegistry(m_windowRegistry);
        return state;
    });
}

SnapState* SnapEngine::stateForWindow(const QString& windowId)
{
    // The reverse map is authoritative: it names the per-screen store a window was
    // placed into on first snap/float. A window with no reverse-map entry falls back
    // to m_globals — the holder where screenless float bookkeeping lives (and the
    // store the single-store test convenience writes to). A genuinely untracked
    // window then reads EMPTY per-window data from the holder, observably identical
    // to the null the null-guarding callers handle, so no caller misbehaves.
    // migrateWindowToScreen deliberately bypasses this fallback (it queries the
    // reverse map directly) so an untracked window is never "migrated" out of globals.
    if (SnapState* state = m_states.forWindow(canonicalWindowId(windowId))) {
        return state;
    }
    return m_globals;
}

const SnapState* SnapEngine::stateForWindow(const QString& windowId) const
{
    if (const SnapState* state = m_states.forWindow(canonicalWindowId(windowId))) {
        return state;
    }
    return m_globals;
}

SnapState* SnapEngine::stateForWindowOnScreen(const QString& windowId, const QString& screenId)
{
    const QString canonical = canonicalWindowId(windowId);
    // An already-tracked window keeps its existing owning store — a screen-carrying
    // write only updates the per-window screen VALUE in place, it does not re-home
    // the window (cross-monitor re-homing is an explicit migrateWindowToScreen).
    if (const auto existing = m_states.windowKey(canonical)) {
        if (SnapState* state = m_states.stateForKey(*existing)) {
            return state;
        }
    }
    // First placement: derive the key from the screen, lazily create the store, and
    // record the reverse-map entry. A screenless call resolves to the global holder
    // and is NOT recorded in the reverse map (untracked stays untracked).
    const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
    SnapState* state = ensureStateForKey(key);
    if (state && !key.screenId.isEmpty()) {
        m_states.setKeyForWindow(canonical, key);
    }
    return state;
}

bool SnapEngine::migrateWindowToScreen(const QString& windowId, const QString& newScreenId)
{
    if (newScreenId.isEmpty()) {
        return false;
    }
    const QString canonical = canonicalWindowId(windowId);
    PhosphorEngine::PlacementStateKey oldKey;
    SnapState* oldState = m_states.forWindow(canonical, &oldKey);
    if (!oldState) {
        // Not tracked in the per-screen stores (e.g. a window being adopted fresh
        // from another engine via handoffReceive) — nothing to migrate.
        return false;
    }
    const PhosphorEngine::PlacementStateKey newKey = currentKeyForScreen(newScreenId);
    if (newKey == oldKey || newKey.screenId.isEmpty()) {
        return false; // same (screen, desktop, activity) context — nothing to move
    }
    SnapState* newState = ensureStateForKey(newKey);
    if (!newState || newState == oldState) {
        return false;
    }
    oldState->migrateWindowTo(newState, canonical, newScreenId);
    m_states.migrate(canonical, oldKey, newKey);
    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "SnapEngine::migrateWindowToScreen:" << canonical << "from" << oldKey.screenId << "to" << newKey.screenId;
    return true;
}

void SnapEngine::setCurrentDesktop(int desktop)
{
    m_context.setCurrentDesktop(desktop);
}

void SnapEngine::setCurrentDesktopForScreen(const QString& screenId, int desktop)
{
    m_context.setCurrentDesktopForScreen(screenId, desktop);
}

void SnapEngine::setCurrentActivity(const QString& activity)
{
    m_context.setCurrentActivity(activity);
}

void SnapEngine::forgetWindow(const QString& windowId)
{
    m_states.removeWindow(canonicalWindowId(windowId));
}

QList<SnapState*> SnapEngine::allSnapStates() const
{
    QList<SnapState*> out;
    out.reserve(m_states.stateCount());
    for (SnapState* state : m_states.states()) {
        if (state) {
            out.append(state);
        }
    }
    return out;
}

bool SnapEngine::isFloating(const QString& windowId) const
{
    if (const SnapState* state = stateForWindow(windowId)) {
        if (state->isFloating(windowId)) {
            return true;
        }
    }
    // Screenless float bookkeeping falls to the global holder, which is not in the
    // reverse map; check it explicitly so isFloating stays symmetric with setFloating.
    return m_globals && m_globals->isFloating(windowId);
}

void SnapEngine::setFloating(const QString& windowId, bool floating)
{
    // stateForWindow never returns null: a tracked window resolves to its owning
    // per-key store; an untracked one falls back to m_globals (constructed in the
    // ctor), which holds the screen-agnostic float bookkeeping the former single
    // store kept. Unfloating an untracked window is a no-op there.
    stateForWindow(windowId)->setFloating(windowId, floating);
}

QStringList SnapEngine::floatingWindows() const
{
    QStringList out;
    for (SnapState* state : m_states.states()) {
        if (state) {
            out += state->floatingWindows();
        }
    }
    return out;
}

QString SnapEngine::zoneForWindow(const QString& windowId) const
{
    if (const SnapState* state = stateForWindow(windowId)) {
        return state->zoneForWindow(windowId);
    }
    return {};
}

void SnapEngine::syncGlobalLastUsedForRemovedZones(const QStringList& removedZones)
{
    if (removedZones.isEmpty()) {
        return;
    }
    // Last-used is per-key: sweep every store (per-screen + the global holder) so a
    // removed zone clears whichever context recorded it as last-used.
    for (SnapState* state : m_states.states()) {
        if (!state) {
            continue;
        }
        const QString lastUsed = state->lastUsedZoneId();
        if (!lastUsed.isEmpty() && removedZones.contains(lastUsed)) {
            state->restoreLastUsedZone({}, {}, {}, 0);
        }
    }
}

QSet<int> SnapEngine::desktopsWithActiveState() const
{
    QSet<int> out;
    const auto& states = m_states.states();
    for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
        out.insert(it.key().desktop);
    }
    return out;
}

void SnapEngine::pruneStatesForDesktop(int removedDesktop)
{
    // removedDesktop is a real (>= 1) destroyed desktop, so the global holder
    // (empty screenId, desktop 0) never matches; the !screenId.isEmpty() guard makes
    // that explicit. Drop every per-key store on the desktop, its reverse-map
    // entries, and the per-output desktop-map entries naming it.
    const auto matches = [removedDesktop](const PhosphorEngine::PlacementStateKey& key) {
        return !key.screenId.isEmpty() && key.desktop == removedDesktop;
    };
    m_states.removeStatesIf(
        [&](const PhosphorEngine::PlacementStateKey& key, SnapState*) {
            return matches(key);
        },
        [](const PhosphorEngine::PlacementStateKey&, SnapState* state) {
            state->deleteLater();
        });
    m_states.removeWindowsIf([&](const QString&, const PhosphorEngine::PlacementStateKey& key) {
        return matches(key);
    });
    m_context.pruneDesktop(removedDesktop);
}

void SnapEngine::pruneStatesForActivities(const QStringList& validActivities)
{
    const QSet<QString> valid(validActivities.begin(), validActivities.end());
    // The global holder has an empty activity, so !activity.isEmpty() excludes it.
    const auto matches = [&valid](const PhosphorEngine::PlacementStateKey& key) {
        return !key.activity.isEmpty() && !valid.contains(key.activity);
    };
    m_states.removeStatesIf(
        [&](const PhosphorEngine::PlacementStateKey& key, SnapState*) {
            return matches(key);
        },
        [](const PhosphorEngine::PlacementStateKey&, SnapState* state) {
            state->deleteLater();
        });
    m_states.removeWindowsIf([&](const QString&, const PhosphorEngine::PlacementStateKey& key) {
        return matches(key);
    });
}

void SnapEngine::pruneStatesForRemovedScreen(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        return;
    }
    // Match every virtual sub-screen of the removed physical monitor: samePhysical
    // strips the "/vs:N" suffix before comparing. The global holder (empty screenId)
    // never matches.
    const auto matches = [&physicalScreenId](const PhosphorEngine::PlacementStateKey& key) {
        return !key.screenId.isEmpty()
            && PhosphorIdentity::VirtualScreenId::samePhysical(key.screenId, physicalScreenId);
    };
    m_states.removeStatesIf(
        [&](const PhosphorEngine::PlacementStateKey& key, SnapState*) {
            return matches(key);
        },
        [](const PhosphorEngine::PlacementStateKey&, SnapState* state) {
            state->deleteLater();
        });
    m_states.removeWindowsIf([&](const QString&, const PhosphorEngine::PlacementStateKey& key) {
        return matches(key);
    });
    m_context.removeScreensIf([&physicalScreenId](const QString& screenId) {
        return PhosphorIdentity::VirtualScreenId::samePhysical(screenId, physicalScreenId);
    });
}

const SnapState* SnapEngine::lastUsedStateForScreen(const QString& screenId) const
{
    const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
    if (!key.screenId.isEmpty()) {
        if (const SnapState* state = m_states.stateForKey(key); state && !state->lastUsedZoneId().isEmpty()) {
            return state;
        }
    }
    return m_globals;
}

PhosphorEngine::ISnapSettings* SnapEngine::snapSettings() const
{
    return dynamic_cast<PhosphorEngine::ISnapSettings*>(engineSettings());
}

// Out-of-line so unique_ptr<SnapNavigationTargetResolver> can destroy the
// pimpl-style owned resolver without its full type being visible in the
// header (forward-declared in SnapEngine.h).
SnapEngine::~SnapEngine() = default;

void SnapEngine::markWindowReported(const QString& windowId)
{
    if (!windowId.isEmpty()) {
        m_effectReportedWindows.insert(windowId);
    }
}

int SnapEngine::pruneStaleWindows(const QSet<QString>& aliveWindowIds)
{
    int pruned = PlacementEngineBase::pruneStaleWindows(aliveWindowIds);
    for (auto it = m_effectReportedWindows.begin(); it != m_effectReportedWindows.end();) {
        if (!aliveWindowIds.contains(*it)) {
            it = m_effectReportedWindows.erase(it);
            ++pruned;
        } else {
            ++it;
        }
    }
    return pruned;
}

void SnapEngine::setAutotileEngine(PhosphorEngine::IPlacementEngine* engine)
{
    auto* obj = dynamic_cast<QObject*>(engine);
    Q_ASSERT(!engine || obj);
    if (m_autotileEngineObj) {
        disconnect(m_autotileEngineObj, &QObject::destroyed, this, nullptr);
    }
    m_autotileEngineObj = obj;
    m_autotileEngineTyped = engine;
    if (obj) {
        connect(obj, &QObject::destroyed, this, [this]() {
            m_autotileEngineTyped = nullptr;
        });
    }
}

void SnapEngine::setZoneAdjacencyResolver(IZoneAdjacencyResolver* resolver)
{
    m_zoneAdjacencyResolver = resolver;
    // Push the resolver into the target resolver if it exists yet.
    // The target resolver constructs lazily on first navigation call;
    // if that hasn't happened yet, ensureTargetResolver() will pick up
    // m_zoneAdjacencyResolver when it first runs.
    if (m_targetResolver) {
        m_targetResolver->setZoneAdjacencyResolver(resolver);
    }
    // Guard against out-of-order destruction: null the raw pointer if the
    // underlying QObject is destroyed before SnapEngine. The interface is
    // not a QObject, but every production implementor (ZoneDetectionAdaptor)
    // is — dynamic_cast recovers the QObject identity for the connection.
    if (auto* qobj = dynamic_cast<QObject*>(resolver)) {
        connect(qobj, &QObject::destroyed, this, [this]() {
            m_zoneAdjacencyResolver = nullptr;
            if (m_targetResolver) {
                m_targetResolver->setZoneAdjacencyResolver(nullptr);
            }
        });
    }
}

void SnapEngine::setCrossSurfaceResolver(PhosphorEngine::ICrossSurfaceResolver* resolver)
{
    m_crossSurfaceResolver = resolver;
    // Push into the target resolver if it has been constructed; otherwise
    // ensureTargetResolver() picks it up on first navigation.
    if (m_targetResolver) {
        m_targetResolver->setCrossSurfaceResolver(resolver);
    }
}

SnapNavigationTargetResolver* SnapEngine::ensureTargetResolver(const QString& action)
{
    if (m_targetResolver) {
        return m_targetResolver.get();
    }
    if (!m_windowTracker || !m_layoutManager) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "ensureTargetResolver: missing deps "
                                                    << "windowTracker=" << static_cast<void*>(m_windowTracker)
                                                    << "layoutManager=" << static_cast<void*>(m_layoutManager);
        if (!action.isEmpty()) {
            // Surface a specific reason so the OSD doesn't silently swallow
            // the shortcut. engine_unavailable is the canonical tag for
            // "engine couldn't be built" — distinct from no_window /
            // invalid_direction / excluded etc.
            Q_EMIT navigationFeedback(false, action, QStringLiteral("engine_unavailable"), QString(), QString(),
                                      QString());
        }
        return nullptr;
    }
    // Feedback callback forwards into SnapEngine's own navigationFeedback
    // signal — SnapAdaptor relays that to WindowTrackingAdaptor's D-Bus
    // navigationFeedback signal, so external consumers see the same
    // wire format as when the resolver lived on WTA.
    m_targetResolver = std::make_unique<SnapNavigationTargetResolver>(
        m_windowTracker, m_layoutManager, m_zoneAdjacencyResolver,
        [this](bool success, const QString& action, const QString& reason, const QString& sourceZoneId,
               const QString& targetZoneId, const QString& screenId) {
            Q_EMIT navigationFeedback(success, action, reason, sourceZoneId, targetZoneId, screenId);
        });
    m_targetResolver->setCrossSurfaceResolver(m_crossSurfaceResolver);
    // The resolver lacks the current (desktop, activity) context needed to read a
    // neighbour output's mode; supply it so move/swap cross-output paths defer an
    // autotile neighbour to the cross-mode handoff instead of snapping onto it.
    m_targetResolver->setNeighbourAutotileProvider([this](const QString& screenId) {
        return m_layoutManager
            && m_layoutManager->modeForScreen(screenId, currentVirtualDesktopForScreen(screenId), currentActivity())
            == PhosphorZones::AssignmentEntry::Autotile;
    });
    return m_targetResolver.get();
}

void SnapEngine::setNavigationStateProvider(INavigationStateProvider* provider)
{
    m_navState = provider;
    // Guard against out-of-order destruction: null the raw pointer if the
    // underlying QObject is destroyed before SnapEngine.
    if (auto* qobj = dynamic_cast<QObject*>(provider)) {
        connect(qobj, &QObject::destroyed, this, [this]() {
            m_navState = nullptr;
        });
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// IPlacementEngine — lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

bool SnapEngine::isActiveOnScreen(const QString& screenId) const
{
    // SnapEngine is active on any screen where AutotileEngine is NOT active.
    // Guard via QPointer: if the QObject was destroyed, m_autotileEngineTyped is stale.
    if (m_autotileEngineObj && m_autotileEngineTyped) {
        return !m_autotileEngineTyped->isActiveOnScreen(screenId);
    }
    return true; // No autotile engine → all screens use snapping
}

// windowOpened is implemented in snapengine/lifecycle.cpp

void SnapEngine::windowClosed(const QString& windowId)
{
    m_effectReportedWindows.remove(windowId);
}

void SnapEngine::windowFocused(const QString& windowId, const QString& screenId)
{
    Q_UNUSED(windowId)
    m_lastActiveScreenId = screenId;
}

// toggleWindowFloat and setWindowFloat are implemented in snapengine/float.cpp
// Navigation entry points (focusInDirection, moveFocusedInDirection,
// swapFocusedInDirection, moveFocusedToPosition, pushFocusedToEmptyZone,
// restoreFocusedWindow, toggleFocusedFloat, cycleFocus,
// rotateWindowsInLayout, resnapCurrentAssignments, resnapToNewLayout)
// live in snapengine/navigation_actions.cpp and call back into
// INavigationStateProvider (m_navState) for fallback target resolution
// and compositor-layer state. The resnap-by-layout-switch pipeline
// (calculateResnapEntriesFromAutotileOrder, snapAllWindows etc.) lives
// in snapengine/navigation.cpp unchanged.

// SnapEngine::assignToZones was removed — its two callers (windowOpened
// in lifecycle.cpp, unfloatToZone in float.cpp) now go through
// SnapEngine::commitSnap / commitMultiZoneSnap which run the
// full snap orchestration (clear floating, assign zone, emit state
// change). The raw-assign path was the last thin wrapper that bypassed
// the orchestration layer.

void SnapEngine::saveState()
{
    if (m_saveFn) {
        m_saveFn();
    }
}

void SnapEngine::loadState()
{
    if (m_loadFn) {
        m_loadFn();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// IPlacementEngine — navigation overrides (thin delegates)
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::rotateWindows(bool clockwise, const NavigationContext& ctx)
{
    rotateWindowsInLayout(clockwise, ctx.screenId);
}

void SnapEngine::reapplyLayout(const NavigationContext& /*ctx*/)
{
    resnapToNewLayout();
}

void SnapEngine::reapplyManagedWindowAppearance()
{
    if (!m_windowTracker) {
        return;
    }
    // Re-emit the current zone geometry for every snapped, non-floating window.
    // The compositor routes a non-empty-zoneId applyGeometryRequested through
    // its snap-commit path (markWindowSnapped), which re-hides the title bar and
    // redraws the snap border. The window is already in its zone, so the
    // compositor's applyWindowGeometry no-ops the move — this only re-drives the
    // chrome the compositor dropped on bridge reconnect. No zone reassignment.
    // Iterate every per-screen store so windows on all monitors are refreshed.
    for (SnapState* state : m_states.states()) {
        if (!state) {
            continue;
        }
        const QStringList snapped = state->snappedWindows();
        for (const QString& windowId : snapped) {
            if (state->isFloating(windowId)) {
                continue;
            }
            const QStringList zoneIds = state->zonesForWindow(windowId);
            if (zoneIds.isEmpty()) {
                continue;
            }
            const QString screenId = state->screenForWindow(windowId);
            if (screenId.isEmpty()) {
                continue;
            }
            const QRect geo = m_windowTracker->resolveZoneGeometry(zoneIds, screenId);
            if (!geo.isValid()) {
                continue;
            }
            Q_EMIT applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), zoneIds.first(),
                                          screenId, false);
        }
    }
}

void SnapEngine::snapAllWindows(const NavigationContext& ctx)
{
    snapAllWindows(ctx.screenId);
}

void SnapEngine::pushToEmptyZone(const NavigationContext& ctx)
{
    pushFocusedToEmptyZone(ctx);
}

// ═══════════════════════════════════════════════════════════════════════════════
// IPlacementEngine — state access
//
// Resolves the per-(screen,desktop,activity) SnapState for a screen via the
// shared ScreenContextTracker + PerScreenStates. An empty screenId resolves to
// the global-scalar holder. The non-const overload lazily creates the store; the
// const overload never creates (returns nullptr for an as-yet-unseen screen).
// ═══════════════════════════════════════════════════════════════════════════════

PhosphorEngine::IPlacementState* SnapEngine::stateForScreen(const QString& screenId)
{
    return ensureStateForKey(currentKeyForScreen(screenId));
}

const PhosphorEngine::IPlacementState* SnapEngine::stateForScreen(const QString& screenId) const
{
    const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
    if (key.screenId.isEmpty()) {
        return m_globals;
    }
    return m_states.stateForKey(key);
}

} // namespace PhosphorSnapEngine
