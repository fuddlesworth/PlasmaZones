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
#include "evacueepark_p.h"
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
    installContextResolver();
}

void SnapEngine::setWindowRegistry(QObject* registryObject)
{
    // The interface passes a QObject carrying the registry; cast to the concrete
    // interface (IWindowRegistry is not itself a QObject, so dynamic_cast, matching
    // AutotileEngine). A null cast clears the registry, same as the old signature.
    auto* registry = dynamic_cast<PhosphorEngine::IWindowRegistry*>(registryObject);
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

SnapState* SnapEngine::stateForWindowOnScreen(const QString& windowId, const QString& screenId, int desktop)
{
    const QString canonical = canonicalWindowId(windowId);
    SnapState* owner = nullptr;
    if (desktop >= 1 && !screenId.isEmpty()) {
        // A PINNED desktop names the store outright: the assignment belongs
        // to that desktop's context whatever the screen is showing, and the
        // membership pass reads a store's key as the desktop its assignment
        // is for. Resolving through the primary instead wrote a RouteToDesktop
        // commit, a cross-desktop move and a background-desktop restore into
        // the VIEWED desktop's store, where the next switch released it.
        //
        // A pinned write naming a screen OTHER than the one the window is
        // tracked on is the window changing screens: a keyboard move across
        // outputs, or a drop or zone-number snap on the other monitor.
        // commitSnapImpl always pins (it falls back to the screen's current
        // desktop), so every such commit lands here. A window is on exactly
        // one screen, so it is re-homed first, the way handoffReceive does,
        // carrying its per-window state and releasing what it held on the
        // screen it left. Adding the pinned key beside the old one instead
        // left the window a member of both screens with the primary still on
        // the old one: every read answered with the zone it had left, and the
        // membership pass took the pair for a multi-desktop window and
        // re-applied the old zone, throwing the window back across monitors
        // (discussion #1124). The move lands in the pinned key itself: a
        // migrate to the screen's CURRENT key followed by this membership
        // left a pinned commit to another desktop a member of two.
        const PhosphorEngine::PlacementStateKey pinned{screenId, desktop, currentActivity()};
        if (const auto primary = m_states.windowKey(canonical); primary && primary->screenId != screenId) {
            migrateWindowToKey(windowId, pinned);
        }
        owner = ensureStateForKey(pinned);
        if (owner) {
            m_states.addMembership(canonical, pinned);
        }
    }
    // Otherwise an already-tracked window keeps its existing owning store — a
    // screen-carrying write only updates the per-window screen VALUE in place,
    // it does not re-home the window (cross-monitor re-homing is an explicit
    // migrateWindowToScreen).
    if (!owner) {
        if (const auto existing = m_states.windowKey(canonical)) {
            owner = m_states.stateForKey(*existing);
        }
    }
    if (!owner) {
        // First placement (or the reverse-map key pointed at a since-pruned store):
        // derive the key from the screen, lazily create the store, and record the
        // reverse-map entry. A screenless call resolves to the global holder and is
        // NOT recorded in the reverse map (untracked stays untracked).
        const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
        owner = ensureStateForKey(key);
        if (owner && !key.screenId.isEmpty()) {
            // ADD, not replace. A window can hold a zone on several desktops
            // at once, and setKeyForWindow would drop every membership but
            // this one — including the ones a restart's
            // seedPersistedDesktopZones just put back, which is how a
            // restored per-desktop zone silently became a single-desktop one
            // again. For a window with no membership yet this is identical to
            // the replace it supersedes.
            m_states.addMembership(canonical, key);
        }
    }
    // One owner PER CONTEXT: a window's zone/screen/desktop data must live only
    // in stores it is a member of. Re-keying moves only the reverse-map pointer —
    // PerScreenStates::migrate / setKeyForWindow deliberately "do not touch state
    // objects" — so a store it has left can retain stale data. That phantom is
    // invisible to owner-map reads (like the autotile engine's) but the snap
    // resnap's forEachZoneAssignedWindow raw-scans every store and would read the
    // phantom's stale desktop, resnapping a window off its real desktop (the
    // cross-desktop leak). Since every write path resolves its store through here
    // first, evicting at resolution time keeps the phantom from ever forming.
    //
    // MEMBERSHIP is what separates a phantom from a legitimate second home. A
    // window present on several desktops holds a membership in each, and the zone
    // it occupies there is real data the user chose — evicting it would be the
    // very overwrite that made a sticky window share one zone across every
    // desktop. A store the window is NOT a member of can only hold a leftover,
    // a flag parked on the global holder for a window with no store yet
    // included, so a mark meant to outlive a commit follows it.
    // removeWindowData is a no-op where absent, so the common single-membership
    // case still costs only a handful of empty hash lookups.
    if (owner) {
        for (auto it = m_states.states().cbegin(); it != m_states.states().cend(); ++it) {
            SnapState* state = it.value();
            if (state && state != owner && !m_states.hasMembership(canonical, it.key())) {
                state->removeWindowData(canonical);
            }
        }
    }
    return owner;
}

bool SnapEngine::migrateWindowToScreen(const QString& windowId, const QString& newScreenId)
{
    if (newScreenId.isEmpty()) {
        return false;
    }
    return migrateWindowToKey(windowId, currentKeyForScreen(newScreenId));
}

bool SnapEngine::migrateWindowToKey(const QString& windowId, const PhosphorEngine::PlacementStateKey& newKey)
{
    const QString canonical = canonicalWindowId(windowId);
    PhosphorEngine::PlacementStateKey oldKey;
    SnapState* oldState = m_states.forWindow(canonical, &oldKey);
    if (!oldState) {
        // Not tracked in the per-screen stores (e.g. a window being adopted fresh
        // from another engine via handoffReceive) — nothing to migrate.
        return false;
    }
    if (newKey == oldKey || newKey.screenId.isEmpty()) {
        return false; // same (screen, desktop, activity) context — nothing to move
    }
    SnapState* newState = ensureStateForKey(newKey);
    if (!newState || newState == oldState) {
        return false;
    }
    const bool crossScreen = oldKey.screenId != newKey.screenId;
    const bool hadResidence = !oldState->screenForWindow(canonical).isEmpty();
    const bool onAllDesktops = hadResidence && oldState->desktopForWindow(canonical) == 0;
    QStringList removed;
    bool lastUsedCleared = false;
    if (crossScreen) {
        // The zone stays behind, unassigned: it names a zone of the old
        // screen's layout, and carrying it let a read on the new screen answer
        // with a zone the window had left. The store's own last-used naming it
        // clears inside the unassign; the global representative follows below.
        if (oldState->isWindowSnapped(canonical)) {
            removed += oldState->zonesForWindow(canonical);
            lastUsedCleared |= oldState->unassignWindow(canonical).lastUsedZoneCleared;
        }
        if (m_windowTracker && oldKey.desktop >= 1) {
            m_windowTracker->forgetDesktopZones(canonical, engineId(), oldKey.desktop);
        }
        // A floating window moved to another monitor forgets the zone it
        // floated from, so an unfloat there never throws it back across.
        dropPreFloatHome(oldState, windowId);
    }
    oldState->migrateWindowTo(newState, canonical, newKey.screenId);
    m_states.migrate(canonical, oldKey, newKey);
    // The window lives where the destination key says: its desktop is
    // re-stamped to that key's (desktop 0, on all desktops, stays 0), and a
    // residence the unassign above cleared is written back on the new screen.
    if (hadResidence) {
        newState->recordResidence(canonical, newKey.screenId, onAllDesktops ? 0 : newKey.desktop);
    }
    if (!crossScreen) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine)
            << "SnapEngine::migrateWindowToKey:" << canonical << "re-keyed on" << newKey.screenId;
        return true;
    }
    // A window is on exactly one screen. The primary moved above; every OTHER
    // membership on the screen it left goes with it, or those stores keep
    // listing the window as a zone occupant there and the next membership
    // pass on that screen re-applies the leftover, dragging the window back
    // across monitors. Released rather than migrated: the destination screen
    // has its own desktops, and a zone of the old screen's layout means
    // nothing there. The persisted per-desktop map follows, since the store
    // merges it and would otherwise re-seed those zones on restore. A
    // same-screen re-key keeps them: they are the window's other desktops.
    for (const PhosphorEngine::PlacementStateKey& key : m_states.membershipsForWindow(canonical)) {
        if (key == newKey || key.screenId != oldKey.screenId) {
            continue;
        }
        lastUsedCleared |= releaseMembership(windowId, key, removed);
    }
    lastUsedCleared |= clearGlobalLastUsedIfRemoved(removed);
    if (lastUsedCleared && m_windowTracker) {
        m_windowTracker->markLastUsedZoneDirty();
    }
    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "SnapEngine::migrateWindowToScreen:" << canonical << "from" << oldKey.screenId << "to" << newKey.screenId
        << "desktop" << newKey.desktop;
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
    const QString canonical = canonicalWindowId(windowId);
    // Every member store, not the primary alone. The WTS close path clears
    // the primary store's data before calling here; the OTHER desktops' stores
    // still list the window, and with the memberships gone nothing would ever
    // resolve those stores for it again: the eviction in stateForWindowOnScreen
    // runs only when the window is next resolved, which a closed window never
    // is. forEachSnapAssignment and the occupancy queries raw-scan every store,
    // so the leftover would be a phantom occupant of its zone on that desktop.
    for (const PhosphorEngine::PlacementStateKey& key : m_states.membershipsForWindow(canonical)) {
        if (SnapState* state = m_states.stateForKey(key)) {
            state->removeWindowData(canonical);
        }
    }
    m_states.removeWindow(canonical);
}

bool SnapEngine::holdsWindowInState(const QString& windowId, const SnapState* state) const
{
    if (!state) {
        return false;
    }
    const QString canonical = canonicalWindowId(windowId);
    for (const PhosphorEngine::PlacementStateKey& key : m_states.membershipsForWindow(canonical)) {
        if (m_states.stateForKey(key) == state) {
            return true;
        }
    }
    return false;
}

std::optional<PhosphorEngine::PlacementStateKey> SnapEngine::keyForState(const SnapState* state) const
{
    if (!state) {
        return std::nullopt;
    }
    for (auto it = m_states.states().constBegin(); it != m_states.states().constEnd(); ++it) {
        if (it.value() == state && !it.key().screenId.isEmpty()) {
            return it.key();
        }
    }
    return std::nullopt;
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
    // stateForWindow never returns null (see setFloating), so no guard here.
    if (stateForWindow(windowId)->isFloating(windowId)) {
        return true;
    }
    // Screenless float bookkeeping falls to the global holder, which is not in the
    // reverse map; check it explicitly so isFloating stays symmetric with setFloating.
    // This fall-through IS load-bearing for a TRACKED window: it resolves to its own
    // store above, which never sees a screenless float. For an untracked one the
    // check above already read m_globals (stateForWindow's fallback), so this simply
    // re-reads the same holder and answers false a second time. m_globals is
    // constructed in the initializer list and never reassigned, so it needs no guard.
    return m_globals->isFloating(windowId);
}

void SnapEngine::setFloating(const QString& windowId, bool floating)
{
    // stateForWindow never returns null: a tracked window resolves to its owning
    // per-key store; an untracked one falls back to m_globals (constructed in the
    // ctor), which holds the screen-agnostic float bookkeeping the former single
    // store kept. Unfloating an untracked window is a no-op there.
    stateForWindow(windowId)->setFloating(windowId, floating);
}

void SnapEngine::setFloatingWithResidence(const QString& windowId, const QString& screenId)
{
    // Only the adopted-unsnapped case records a residence: a window present
    // on several desktops whose store in view holds no screen for it (it was
    // adopted here, never snapped here). Every other float keeps the bare
    // bit the store already models — a single-desktop float is screenless in
    // this engine by design (the capture orchestrator resolves its screen
    // from the live frame), and giving it one would route its capture
    // through the mode gate for a screen it never named.
    const QList<PhosphorEngine::PlacementStateKey> held = m_states.membershipsForWindow(canonicalWindowId(windowId));
    SnapState* owner = stateForWindow(windowId);
    if (screenId.isEmpty() || held.size() < 2 || !owner->screenForWindow(windowId).isEmpty()) {
        owner->setFloating(windowId, true);
        return;
    }
    owner->setFloatingOnScreen(windowId, screenId, currentKeyForScreen(screenId).desktop);
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
    // stateForWindow never returns null (see setFloating); an untracked window
    // reads an empty zone out of the global holder, which is what the old null
    // branch returned anyway.
    return stateForWindow(windowId)->zoneForWindow(windowId);
}

QSet<int> SnapEngine::desktopsWithActiveState() const
{
    QSet<int> out;
    const auto& states = m_states.states();
    for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
        // Skip the global holder (empty screenId): it is not a real per-screen store
        // and would otherwise always report its key's default desktop. Mirrors
        // AutotileEngine, which has no such holder.
        if (!it.key().screenId.isEmpty()) {
            out.insert(it.key().desktop);
        }
    }
    return out;
}

// pruneStatesForDesktop and pruneStatesForActivities are implemented in
// src/leave.cpp, with what a window leaving those contexts releases.

void SnapEngine::renumberDesktopsAfterRemoval(int removedDesktop)
{
    if (removedDesktop < 1) {
        return;
    }
    // Snapping has no stack to re-flow and no strip to announce, so this is
    // the whole operation: move each state and its reverse-map entries down
    // one. It still matters — a zone assignment filed under the number the
    // desktop had before would be handed to whichever desktop takes that
    // number next.
    //
    // The global holder has an empty screenId and no desktop identity, so it
    // is excluded here exactly as it is in the prune above.
    //
    // ASCENDING: the prune ran first, so removedDesktop is vacant when
    // removedDesktop+1 moves in, and each later target was vacated by the step
    // before it.
    QList<int> desktops;
    for (const int desktop : desktopsWithActiveState()) {
        if (desktop > removedDesktop) {
            desktops.append(desktop);
        }
    }
    std::sort(desktops.begin(), desktops.end());
    for (const int desktop : std::as_const(desktops)) {
        QList<PhosphorEngine::PlacementStateKey> atDesktop;
        for (auto it = m_states.states().constBegin(); it != m_states.states().constEnd(); ++it) {
            if (!it.key().screenId.isEmpty() && it.key().desktop == desktop) {
                atDesktop.append(it.key());
            }
        }
        for (const PhosphorEngine::PlacementStateKey& oldKey : std::as_const(atDesktop)) {
            const PhosphorEngine::PlacementStateKey newKey{oldKey.screenId, oldKey.desktop - 1, oldKey.activity};
            SnapState* moving = m_states.takeState(oldKey);
            if (!moving) {
                continue;
            }
            // A state already at the target is a transient placeholder from a
            // lazy lookup; snapping creates them on placement and they hold no
            // windows the reverse map has not already been told about.
            if (SnapState* existing = m_states.takeState(newKey)) {
                existing->deleteLater();
            }
            m_states.insertState(newKey, moving);
            m_states.rekeyWindows(oldKey, newKey);
        }
    }
    m_context.renumberDesktopsAfterRemoval(removedDesktop);
    // So do the desktop numbers the stores hold for each window and for their
    // last-used zone (the global holder included): left alone they name the
    // desktop that takes the old number, which the resnap stamp, the
    // per-desktop forget and the last-zone gate then act on (F145).
    for (SnapState* state : m_states.states()) {
        if (state) {
            state->renumberDesktopsAfterRemoval(removedDesktop);
        }
    }
    // The persisted per-desktop zone maps follow the same renumbering, or a
    // restart seeds a zone under a number that now belongs to another
    // desktop. The removed desktop's own entry goes with it (the prune ahead
    // of this dropped the live store). Through the tracker's wrapper so the
    // change reaches disk.
    if (m_windowTracker) {
        m_windowTracker->renumberDesktopZones(removedDesktop);
    }
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
    // The windows here are ALIVE: only their output went away, and KWin moves
    // them to a surviving one. Every membership on the output is released
    // where it lives, each desktop's zone with it (F108), and not through the
    // tracking service's unassign: that clears the window's PRIMARY store,
    // which may be on a surviving output (F254). A window left with
    // no zone anywhere is announced once as moved off its screen, the entry
    // the effect drops its snapped mark on (F427); one still snapped on a
    // surviving output keeps its zone and hears nothing. A FLOATING window is
    // announced as no longer floating there, since its float bit dies with
    // the store. Each announcement names the STATE's screen, not
    // physicalScreenId: `matches` spans the output's virtual screens, and a
    // subscriber keying on the screen must get one the window was on.
    QStringList unsnapped;
    QList<QPair<QString, QString>> floated;
    QSet<QString> floatedSeen;
    QHash<QString, QString> stateScreenOf;
    QStringList removed;
    bool lastUsedCleared = false;
    const QStringList tracked = m_states.trackedWindowIds();
    QList<PhosphorEngine::PlacementStateKey> keys;
    for (auto it = m_states.states().constBegin(); it != m_states.states().constEnd(); ++it) {
        if (matches(it.key())) {
            keys.append(it.key());
        }
    }
    for (const PhosphorEngine::PlacementStateKey& key : std::as_const(keys)) {
        if (SnapState* state = m_states.stateForKey(key)) {
            for (const QString& windowId : state->snappedWindows()) {
                if (!stateScreenOf.contains(windowId)) {
                    unsnapped.append(windowId);
                    stateScreenOf.insert(windowId, key.screenId);
                }
            }
            for (const QString& windowId : state->floatingWindows()) {
                if (!floatedSeen.contains(windowId)) {
                    floatedSeen.insert(windowId);
                    floated.append({windowId, key.screenId});
                }
            }
        }
        for (const QString& windowId : tracked) {
            if (m_states.hasMembership(windowId, key)) {
                lastUsedCleared |= releaseMembership(windowId, key, removed);
            }
        }
    }
    lastUsedCleared |= clearGlobalLastUsedIfRemoved(removed);
    if (lastUsedCleared && m_windowTracker) {
        m_windowTracker->markLastUsedZoneDirty();
    }
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
    // Announced once the stores are gone, so a subscriber reading the engine
    // back sees the prune.
    const auto snappedElsewhere = [this](const QString& windowId) {
        for (const PhosphorEngine::PlacementStateKey& key : m_states.membershipsForWindow(windowId)) {
            if (const SnapState* state = m_states.stateForKey(key); state && state->isWindowSnapped(windowId)) {
                return true;
            }
        }
        return false;
    };
    for (const QString& windowId : std::as_const(unsnapped)) {
        if (snappedElsewhere(windowId)) {
            continue;
        }
        Q_EMIT windowSnapStateChanged(
            windowId,
            PhosphorProtocol::WindowStateEntry{windowId, QString(), stateScreenOf.value(windowId), false,
                                               QStringLiteral("screen_changed"), QStringList{}, false});
    }
    // A float parked for the evacuee adoption is not announced as ending: the
    // daemon adopts the window floating where KWin put it.
    for (const auto& [windowId, stateScreenId] : std::as_const(floated)) {
        if (!m_evacueePark || !m_evacueePark->suppressFloatFalse.contains(windowId)) {
            Q_EMIT windowFloatingChanged(windowId, false, stateScreenId);
        }
    }
    if (m_evacueePark) {
        m_evacueePark->suppressFloatFalse.clear();
    }
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

int SnapEngine::pruneStaleWindows(const QSet<QString>& aliveWindowIds)
{
    // Called by WindowTrackingService::pruneStaleAssignments with the alive
    // set already canonicalized, after it has pruned every SnapState's own
    // per-window data. What is left for a window that died without a close
    // signal is its membership in the state index, which would otherwise
    // leak and keep every membership reconcile walking a dead id. Only the
    // membership goes: the per-desktop zones are the persisted record's,
    // nothing is announced, and the index is runtime-only, so the drops are
    // not counted (the caller marks persisted state dirty on a positive
    // return).
    const int pruned = PlacementEngineBase::pruneStaleWindows(aliveWindowIds);
    const QStringList tracked = m_states.trackedWindowIds();
    for (const QString& windowId : tracked) {
        if (!aliveWindowIds.contains(windowId)) {
            m_states.removeWindow(windowId);
        }
    }
    return pruned;
}

void SnapEngine::setAutotileEngine(PhosphorEngine::IPlacementEngine* engine)
{
    auto* obj = dynamic_cast<QObject*>(engine);
    Q_ASSERT(!engine || obj);
    if (m_autotileEngineObj) {
        // Same caveat setZoneAdjacencyResolver and setNavigationStateProvider
        // carry: this severs EVERY destroyed-guard this engine holds on that
        // sender, not just the one installed below. Correct only while no
        // other member connects to the same object's destroyed signal — if one
        // ever does, all three sites must move to context/handle-based
        // disconnects together.
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
    // Drop the previous resolver's destroyed guard first: left connected, its
    // eventual destruction would null the LIVE replacement below. Mirrors
    // setAutotileEngine. NOTE: this severs EVERY destroyed-guard this engine
    // holds on that sender, which is safe only while each guarded role
    // (autotile engine / adjacency resolver / nav-state provider) lives on a
    // distinct QObject — true for all production implementors today. A future
    // combined adaptor implementing two roles must split this per-role.
    if (m_zoneAdjacencyResolverObj) {
        disconnect(m_zoneAdjacencyResolverObj, &QObject::destroyed, this, nullptr);
    }
    m_zoneAdjacencyResolverObj = dynamic_cast<QObject*>(resolver);
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
    if (auto* qobj = m_zoneAdjacencyResolverObj.data()) {
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
    // neighbour output's mode; supply it so move/swap cross-output paths defer a
    // tiling neighbour to the cross-mode handoff instead of snapping onto it.
    // The question is "is the neighbour NOT snapping", not "is it autotile": with
    // three engines a Scrolling neighbour has no snap zones either, and testing
    // for Autotile alone let snap navigation move and swap windows onto a
    // scroll-owned output. The router-backed live resolver answers first when
    // wired (it carries the unclaimed-tiling-mode downgrade to Snapping, so a
    // screen no engine has claimed is correctly treated as snap); the registry
    // cascade is the fallback for the unwired case.
    m_targetResolver->setNeighbourTilingProvider([this](const QString& screenId) {
        return isTilingOutput(screenId);
    });
    // The context a move or swap lands in, asked about the window landing:
    // switched off there (the user's disabled list, the master switch) or an
    // exclusion rule there (F159, F208).
    m_targetResolver->setLandingRefusalProvider([this](const QString& windowId, const QString& screenId) {
        if (!snapsInContext(currentKeyForScreen(screenId))) {
            return QStringLiteral("disabled");
        }
        if (isWindowExcluded(windowId, screenId)) {
            return QStringLiteral("excluded");
        }
        return QString();
    });
    // Zone occupants are the windows of the context in view (F182).
    m_targetResolver->setZoneOccupantsProvider(
        [this](const QString& zoneId, const QString& screenId, const QString& excludeWindowId) {
            return windowsInZoneInView(zoneId, screenId, excludeWindowId);
        });
    return m_targetResolver.get();
}

void SnapEngine::setNavigationStateProvider(INavigationStateProvider* provider)
{
    // Same stale-destroyed-guard discipline as setZoneAdjacencyResolver.
    if (m_navStateObj) {
        disconnect(m_navStateObj, &QObject::destroyed, this, nullptr);
    }
    m_navStateObj = dynamic_cast<QObject*>(provider);
    m_navState = provider;
    // Guard against out-of-order destruction: null the raw pointer if the
    // underlying QObject is destroyed before SnapEngine.
    if (auto* qobj = m_navStateObj.data()) {
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
    // Router-backed live resolver first: with THREE engines, "not autotile"
    // no longer implies snapping — a scrolling screen must not be claimed.
    // The resolver carries the router's downgrade semantics (an unclaimed
    // tiling mode resolves to Snapping), which is exactly the live truth.
    if (m_liveModeResolver) {
        return m_liveModeResolver(screenId) == PhosphorZones::AssignmentEntry::Mode::Snapping;
    }
    // Legacy cross-wire fallback (resolver not injected, e.g. unit tests):
    // active wherever AutotileEngine is not. Guard via QPointer: if the
    // QObject was destroyed, m_autotileEngineTyped is stale.
    if (m_autotileEngineObj && m_autotileEngineTyped) {
        return !m_autotileEngineTyped->isActiveOnScreen(screenId);
    }
    return true; // No autotile engine → all screens use snapping
}

// windowOpened is implemented in src/lifecycle.cpp

void SnapEngine::windowClosed(const QString& windowId)
{
    // Snap keeps no engine-side close state. A snap window's close runs
    // through WindowTrackingService::windowClosed, which clears its SnapState
    // and its snap resolver entry, so nothing in the daemon calls this.
    Q_UNUSED(windowId)
}

std::optional<PhosphorEngine::PlacementStateKey> SnapEngine::heldKeyForWindow(const QString& windowId) const
{
    // Not stateForWindow: that falls back to the globals holder, which has no
    // desktop identity and would answer a key the reconcile could only read as
    // "left every desktop". The reverse map alone, then membership in the store
    // it names — the same two-step the tiling engines use.
    const QString canonical = canonicalWindowId(windowId);
    const auto holds = [&canonical](const SnapState* state) {
        // Membership, not a bare key: a refused placement can leave the
        // reverse map pointing at a store that holds nothing for this window.
        return state
            && (state->isWindowSnapped(canonical) || state->isFloating(canonical)
                || !state->screenForWindow(canonical).isEmpty());
    };
    // The primary first (the membership in view), then the rest. A window
    // present on several desktops is adopted into the desktop in view with a
    // membership and NO data (snapping places nothing itself), so its primary
    // store can be empty while a background store genuinely holds it; the
    // interface promises the holding key in ANY context.
    PhosphorEngine::PlacementStateKey primary;
    if (const SnapState* state = m_states.forWindow(canonical, &primary);
        state && !primary.screenId.isEmpty() && holds(state)) {
        return primary;
    }
    for (const PhosphorEngine::PlacementStateKey& key : m_states.membershipsForWindow(canonical)) {
        if (key == primary || key.screenId.isEmpty()) {
            continue;
        }
        if (holds(m_states.stateForKey(key))) {
            return key;
        }
    }
    return std::nullopt;
}

void SnapEngine::windowFocused(const QString& windowId, const QString& screenId)
{
    m_lastActiveScreenId = screenId;
    // Arm the owning store's layer-side focus memory (the switch verb's
    // remembered targets). stateForWindow never returns null by
    // construction (m_globals is built in the ctor and is the fallback), so
    // unlike the m_globals assert-and-guard sites this call is direct — an
    // untracked window resolves to m_globals, where noteFocused finds it on
    // neither layer and takes neither branch: a no-op by classification,
    // not by lookup failure.
    stateForWindow(windowId)->noteFocused(windowId);
}

// toggleWindowFloat and setWindowFloat are implemented in src/float.cpp
// Navigation entry points (focusInDirection, moveFocusedInDirection,
// swapFocusedInDirection, moveFocusedToPosition, pushFocusedToEmptyZone,
// restoreFocusedWindow, toggleFocusedFloat, cycleFocus,
// rotateWindowsInLayout, resnapCurrentAssignments, resnapToNewLayout)
// live in src/navigation_actions.cpp and call back into
// INavigationStateProvider (m_navState) for fallback target resolution
// and compositor-layer state. The resnap-by-layout-switch pipeline
// (calculateResnapEntriesFromAutotileOrder, snapAllWindows etc.) lives
// in src/navigation.cpp unchanged.

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

void SnapEngine::reapplyLayout(const NavigationContext& ctx)
{
    // The windows holding a zone in the context the screen shows. Replaying
    // the layout-switch buffer moved a window by a switch that had already
    // run, or by nothing (F398), and the current-assignments pass on its own
    // keeps a hidden single-membership window (F430).
    QSet<QString> inView;
    if (const SnapState* state = m_states.stateForKey(currentKeyForScreen(ctx.screenId))) {
        for (auto it = state->zoneAssignments().cbegin(); it != state->zoneAssignments().cend(); ++it) {
            if (!it.value().isEmpty()) {
                inView.insert(it.key());
            }
        }
    }
    if (inView.isEmpty()) {
        // An empty set means every window to resnapCurrentAssignments.
        Q_EMIT navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_windows_to_resnap"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    resnapCurrentAssignments(ctx.screenId, inView);
}

void SnapEngine::reapplyManagedWindowAppearance()
{
    if (!m_windowTracker) {
        return;
    }
    // Re-drive the snap chrome the compositor dropped on bridge reconnect: a
    // non-empty-zoneId applyGeometryRequested goes through its snap-commit
    // path (markWindowSnapped), which re-hides the title bar and redraws the
    // snap border, and a window already in its zone does not move. No zone
    // reassignment. Only what snap places IN VIEW is re-stated: a store of a
    // desktop or activity not shown, or of a screen a tiling engine runs,
    // holds memory rather than a placement, and its zone geometry would pull
    // the window into a zone it is not in (F104). So is a leftover in a store
    // the window is not a member of.
    const auto& allStates = m_states.states();
    for (auto it = allStates.constBegin(); it != allStates.constEnd(); ++it) {
        SnapState* state = it.value();
        const PhosphorEngine::PlacementStateKey& key = it.key();
        if (!state || key.screenId.isEmpty() || !(key == currentKeyForScreen(key.screenId))
            || !isActiveOnScreen(key.screenId)) {
            continue;
        }
        const QStringList snapped = state->snappedWindows();
        for (const QString& windowId : snapped) {
            if (state->isFloating(windowId) || !holdsWindowInState(windowId, state)) {
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
            Q_EMIT restatementGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), zoneIds.first(),
                                                screenId);
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
