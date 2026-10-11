// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Auto-snap logic and snap tracking helpers.
// Part of WindowTrackingService, split from WindowTrackingService.cpp.

#include <PhosphorPlacement/WindowTrackingService.h>
#include "placementutils.h"

#include <PhosphorZones/Layout.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include "placementlogging.h"
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QSet>
#include <QUuid>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorPlacement {

// ═══════════════════════════════════════════════════════════════════════════════
// Auto-Snap Logic
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::recordSnapIntent(const QString& windowId, bool wasUserInitiated)
{
    Q_ASSERT(hasSnapState());
    PhosphorSnapEngine::SnapState* globals = snapGlobals();
    if (!globals) {
        return;
    }
    if (wasUserInitiated) {
        QString windowClass = currentAppIdFor(windowId);
        if (!windowClass.isEmpty()) {
            globals->recordSnapIntent(windowClass, true);
            markDirty(DirtyUserSnapped);
        }
    }
}

void WindowTrackingService::updateLastUsedZone(const QString& zoneId, const QString& screenId,
                                               const QString& windowClass, int virtualDesktop)
{
    Q_ASSERT(hasSnapState());
    // Last-used is per-key: record it on the store that owns @p screenId's current
    // (screen, desktop, activity) context so a window opening on this screen later
    // restores to a zone THIS screen was snapped to. An empty screenId resolves to
    // the global holder.
    PhosphorSnapEngine::SnapState* store = snapForScreen(screenId);
    if (!store) {
        store = snapGlobals();
    }
    if (!store) {
        return;
    }
    store->updateLastUsedZone(zoneId, screenId, windowClass, virtualDesktop);
    markDirty(DirtyLastUsedZone);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Snap commit orchestration — moved out of WindowTrackingAdaptor.
//
// These methods used to live in WindowTrackingAdaptor::windowSnapped /
// windowSnappedMultiZone / windowUnsnapped, where they orchestrated a
// sequence of WTS primitive calls plus a D-Bus signal emit. That made
// WTA a partial snap engine rather than a thin facade. The orchestration
// is pure state-management work that belongs on WTS; WTA retains its
// D-Bus slot entry points but forwards to these methods and relays the
// WTS signals to its own D-Bus signals at connection wiring time.
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::markAsAutoSnapped(const QString& windowId)
{
    Q_ASSERT(hasSnapState());
    if (windowId.isEmpty()) {
        return;
    }
    // Prefer the window's owning store; a window with none parks the flag on the
    // global holder. The is/clear paths scan every store, so it is found wherever
    // it landed. A caller marking a window it is about to commit marks AFTER the
    // commit: the first placement evicts the global holder's copy.
    PhosphorSnapEngine::SnapState* store = snapForWindow(windowId);
    if (!store) {
        store = snapGlobals();
    }
    if (store) {
        store->markAsAutoSnapped(windowId);
    }
}

bool WindowTrackingService::isAutoSnapped(const QString& windowId) const
{
    Q_ASSERT(hasSnapState());
    for (const PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        if (state->isAutoSnapped(windowId)) {
            return true;
        }
    }
    return false;
}

bool WindowTrackingService::clearAutoSnapped(const QString& windowId)
{
    Q_ASSERT(hasSnapState());
    bool cleared = false;
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        cleared |= state->clearAutoSnapped(windowId);
    }
    return cleared;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Snap-state resolver wiring and access helpers
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::setSnapStateResolver(SnapStateResolver resolver)
{
    const bool detaching = !resolver.globals;
    m_snapResolver = std::move(resolver);
    if (detaching) {
        // An EMPTY resolver is a detach, and a detach discards the hold for
        // the same reason setSnapState(nullptr) does: keeping it would flush a
        // set captured for the store that just went away into whatever store
        // is wired next. The two detach paths are documented as equivalent, so
        // they must actually behave that way.
        m_pendingUserSnappedClasses.reset();
        m_pendingLastUsedZone.reset();
        return;
    }
    // A load that arrived before the resolver was wired parked its classes
    // and last-used zone; now that a store exists they can land. See
    // setUserSnappedClasses and setLastUsedZone.
    flushPendingUserSnappedClasses();
    flushPendingLastUsedZone();
}

void WindowTrackingService::setSnapState(PhosphorSnapEngine::SnapState* state)
{
    if (!state) {
        m_snapResolver = SnapStateResolver{};
        // A detach DISCARDS the hold. Keeping it would flush a set captured
        // for the store that just went away into whatever store is wired
        // next — visible in tests that reuse one service across cases.
        m_pendingUserSnappedClasses.reset();
        m_pendingLastUsedZone.reset();
        return;
    }
    SnapStateResolver resolver;
    resolver.forWindow = [state](const QString&) {
        return state;
    };
    resolver.forWindowOnScreen = [state](const QString&, const QString&, int) {
        return state;
    };
    resolver.forScreen = [state](const QString&) {
        return state;
    };
    resolver.globals = [state]() {
        return state;
    };
    resolver.allStates = [state]() {
        return QList<PhosphorSnapEngine::SnapState*>{state};
    };
    resolver.forgetWindow = [](const QString&) { };
    m_snapResolver = std::move(resolver);
    flushPendingUserSnappedClasses();
    flushPendingLastUsedZone();
}

SnapStateResolver snapStateResolverFor(PhosphorSnapEngine::SnapEngine* engine)
{
    SnapStateResolver r;
    r.forWindow = [e = QPointer(engine)](const QString& id) -> PhosphorSnapEngine::SnapState* {
        return e ? e->stateForWindow(id) : nullptr;
    };
    r.forWindowOnScreen = [e = QPointer(engine)](const QString& id, const QString& screenId,
                                                 int desktop) -> PhosphorSnapEngine::SnapState* {
        return e ? e->stateForWindowOnScreen(id, screenId, desktop) : nullptr;
    };
    r.forScreen = [e = QPointer(engine)](const QString& screenId) -> PhosphorSnapEngine::SnapState* {
        return e ? static_cast<PhosphorSnapEngine::SnapState*>(e->stateForScreen(screenId)) : nullptr;
    };
    r.globals = [e = QPointer(engine)]() -> PhosphorSnapEngine::SnapState* {
        return e ? e->globalState() : nullptr;
    };
    r.allStates = [e = QPointer(engine)]() -> QList<PhosphorSnapEngine::SnapState*> {
        return e ? e->allSnapStates() : QList<PhosphorSnapEngine::SnapState*>{};
    };
    r.forgetWindow = [e = QPointer(engine)](const QString& id) {
        if (e) {
            e->forgetWindow(id);
        }
    };
    r.holdsWindow = [e = QPointer(engine)](const QString& id, const PhosphorSnapEngine::SnapState* state) {
        return e ? e->holdsWindowInState(id, state) : false;
    };
    r.keyFor = [e = QPointer(engine)](
                   const PhosphorSnapEngine::SnapState* state) -> std::optional<PhosphorEngine::PlacementStateKey> {
        return e ? e->keyForState(state) : std::nullopt;
    };
    return r;
}

void WindowTrackingService::setSnapEngine(PhosphorEngine::PlacementEngineBase* engine)
{
    m_snapEngine = engine;
}

PhosphorEngine::PlacementEngineBase* WindowTrackingService::snapEngine() const
{
    return m_snapEngine.data();
}

PhosphorSnapEngine::SnapState* WindowTrackingService::snapForWindow(const QString& windowId) const
{
    return m_snapResolver.forWindow ? m_snapResolver.forWindow(windowId) : nullptr;
}

bool WindowTrackingService::snapHoldsWindow(const QString& windowId, const PhosphorSnapEngine::SnapState* state) const
{
    if (m_snapResolver.holdsWindow) {
        return m_snapResolver.holdsWindow(windowId, state);
    }
    return state && state == snapForWindow(windowId);
}

PhosphorSnapEngine::SnapState* WindowTrackingService::snapForWindowOnScreen(const QString& windowId,
                                                                            const QString& screenId, int desktop)
{
    return m_snapResolver.forWindowOnScreen ? m_snapResolver.forWindowOnScreen(windowId, screenId, desktop) : nullptr;
}

PhosphorSnapEngine::SnapState* WindowTrackingService::snapForScreen(const QString& screenId) const
{
    return m_snapResolver.forScreen ? m_snapResolver.forScreen(screenId) : nullptr;
}

PhosphorSnapEngine::SnapState* WindowTrackingService::snapGlobals() const
{
    return m_snapResolver.globals ? m_snapResolver.globals() : nullptr;
}

QList<PhosphorSnapEngine::SnapState*> WindowTrackingService::snapAllStates() const
{
    return m_snapResolver.allStates ? m_snapResolver.allStates() : QList<PhosphorSnapEngine::SnapState*>{};
}

void WindowTrackingService::setManagedFramePredicate(ManagedFramePredicate predicate)
{
    m_managedFramePredicate = std::move(predicate);
}

bool WindowTrackingService::isManagedFrame(const QString& windowId, const QRect& frame) const
{
    if (windowId.isEmpty() || !frame.isValid()) {
        return false;
    }
    // Every store the window is a member of: a window on several desktops sits
    // on the zone it holds on another as much as on the one in view, and a
    // floated window is still on the zone it floated from until it moves.
    for (const PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        if (!state || !snapHoldsWindow(windowId, state)) {
            continue;
        }
        const QString screen = state->screenId().isEmpty() ? state->screenForWindow(windowId) : state->screenId();
        for (const QStringList& zones : {state->zonesForWindow(windowId), state->preFloatZones(windowId)}) {
            if (!zones.isEmpty() && resolveZoneGeometry(zones, screen) == frame) {
                return true;
            }
        }
    }
    return m_managedFramePredicate && m_managedFramePredicate(windowId, frame);
}

bool WindowTrackingService::occupiesZoneInView(const QString& windowId) const
{
    // The LIVE form (F487): a zone remembered for a context another engine now
    // tiles, or for a desktop not shown, is memory, and the window's frame there
    // can be a genuine free one.
    const PhosphorSnapEngine::SnapState* owner = snapForWindow(windowId);
    if (!owner || !owner->isWindowSnapped(windowId) || owner->isFloating(windowId)) {
        return false;
    }
    const QString screen = owner->screenForWindow(windowId);
    if (screen.isEmpty() || snapForScreen(screen) != owner) {
        return false;
    }
    return !m_snapEngine || m_snapEngine->isActiveOnScreen(screen);
}

bool WindowTrackingService::hasSnapState() const
{
    // Resolve, don't just check the arm is installed. The globals lambda
    // captures a QPointer, so it self-nulls when the engine dies while the
    // arm stays callable — and every Q_ASSERT(hasSnapState()) then passed in
    // debug while the paired release path took its null branch. The assert
    // now asserts what the code below it actually needs.
    return m_snapResolver.globals && m_snapResolver.globals() != nullptr;
}

} // namespace PhosphorPlacement
