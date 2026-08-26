// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Own header
#include <PhosphorTileEngine/AutotileEngine.h>

// Project headers
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorGeometry/GeometryUtils.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTileEngine/NavigationController.h>
#include <PhosphorTileEngine/PerScreenConfigResolver.h>
#include <PhosphorTiles/AlgorithmPreviewParams.h>
#include <PhosphorTiles/TilingAlgorithm.h>
// DwindleMemoryAlgorithm.h no longer needed — prepareTilingState() is virtual on PhosphorTiles::TilingAlgorithm
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "tileenginelogging.h"
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "engine_internal.h"

// Qt and std
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <QVarLengthArray>
#include <algorithm>
#include <cmath>

namespace PhosphorTileEngine {

bool AutotileEngine::isEnabled() const noexcept
{
    return !m_autotileScreens.isEmpty();
}

bool AutotileEngine::isAutotileScreen(const QString& screenId) const
{
    return m_autotileScreens.contains(screenId);
}

bool AutotileEngine::isActiveOnScreen(const QString& screenId) const
{
    return isAutotileScreen(screenId);
}

bool AutotileEngine::isWindowTiled(const QString& rawWindowId) const
{
    // Canonicalize for the lookup, symmetric with isWindowFloatingInAutotile() — both
    // are consulted from the same daemon mode-resolution path with the same id.
    const QString windowId = canonicalizeForLookup(rawWindowId);
    auto it = m_states.windowKeys().constFind(windowId);
    if (it == m_states.windowKeys().constEnd()) {
        return false;
    }
    const PhosphorTiles::TilingState* state = m_states.stateForKey(it.value());
    // Membership is required, not just a live key: windowOpened keys the
    // window BEFORE onWindowAdded can refuse it (shouldTileWindow false,
    // max-windows cap), and isFloating() answers false for a window the
    // state does not hold — without the containsWindow check a refused
    // window reads as "tiled" forever and the engine-tiled predicate then
    // refuses to record its free geometry (fails closed on a free window).
    return state && state->containsWindow(windowId) && !state->isFloating(windowId);
}

bool AutotileEngine::isWindowFloatingInAutotile(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    auto it = m_states.windowKeys().constFind(windowId);
    if (it == m_states.windowKeys().constEnd()) {
        return false;
    }
    // containsWindow for the same phantom-key reason as isWindowTiled.
    const PhosphorTiles::TilingState* state = m_states.stateForKey(it.value());
    return state && state->containsWindow(windowId) && state->isFloating(windowId);
}

QStringList AutotileEngine::allFloatingWindows() const
{
    QStringList result;
    for (auto it = m_states.states().constBegin(); it != m_states.states().constEnd(); ++it) {
        if (it.value()) {
            result += it.value()->floatingWindows();
        }
    }
    return result;
}

void AutotileEngine::rotateWindows(bool clockwise, const QString& screenId)
{
    // Prefer the caller's screen: the internal m_activeScreen tracker can
    // drift when focus moves through floating, snapped, or never-tracked
    // windows (the same reason the other NavigationContext overrides pass
    // the daemon-authoritative context through). An empty id keeps the
    // tracker-resolved behaviour.
    setActiveScreenHint(screenId);
    rotateWindowOrder(clockwise);
}

void AutotileEngine::setCurrentDesktop(int desktop)
{
    // The daemon pushes the initial desktop in start() BEFORE the first
    // updateEngineScreens(); that first push must NOT read as a switch — or
    // login with autotile enabled suppresses enabledChanged and the effect
    // treats the first autotileScreensChanged as a "desktop return", skipping
    // window notification to the daemon entirely. The tracker owns that
    // established-vs-switch arming; here we only log the actual change and OR the
    // armed flag into m_isDesktopContextSwitch (|= so a simultaneous activity
    // change's flag is not lost).
    const int previous = m_context.currentDesktop();
    const PhosphorEngine::ContextChange change = m_context.setCurrentDesktop(desktop);
    if (change.changed) {
        qCInfo(PhosphorTileEngine::lcTileEngine)
            << "Switching autotile context: desktop" << previous << "->" << desktop;
        m_isDesktopContextSwitch |= change.armSwitch;
    }
}

void AutotileEngine::setCurrentDesktopForScreen(const QString& screenId, int desktop)
{
    // PURE context swap — no state migration. The other desktop's TilingState for
    // this screen stays put and reappears when the screen returns to it; migrating
    // would destroy the per-desktop isolation that the (screen, desktop) keying
    // exists to provide. Arm the (global) desktop-switch flag exactly like
    // setCurrentDesktop so the effect's desktop-switch pass runs.
    const int previous = m_context.screenDesktop(screenId);
    const PhosphorEngine::ContextChange change = m_context.setCurrentDesktopForScreen(screenId, desktop);
    if (change.changed) {
        qCInfo(PhosphorTileEngine::lcTileEngine)
            << "Switching autotile context for screen" << screenId << "desktop" << previous << "->" << desktop;
        m_isDesktopContextSwitch |= change.armSwitch;
        // A pending initial order is keyed by bare SCREEN id but was probed —
        // and is consumed — against the screen's CONTEXT. A context change
        // between those two points would apply one desktop's saved order to
        // another desktop's layout, placing a window at a slot index that means
        // nothing there. Nothing else invalidates it: the seed survives its own
        // pass whenever one of its windows is still minimized, and its reaper
        // re-arms indefinitely for exactly that case, so it is not time-bounded
        // either.
        //
        // Dropped rather than re-keyed: the consumption sites resolve the
        // context themselves, so re-keying would mean re-deciding screen-vs-
        // context intent at seven call sites for the same outcome. Safe against
        // eating a live mode-transition seed, because the daemon seeds inside
        // updateEngineScreens, which runs strictly after this in the same
        // handler.
        clearPendingInitialOrder(screenId);
    }
}

void AutotileEngine::clearCurrentDesktopForScreen(const QString& screenId)
{
    m_context.clearCurrentDesktopForScreen(screenId);
}

void AutotileEngine::clearScreenScheduling(const QString& screenId)
{
    m_pendingRetileScreens.remove(screenId);
    m_retileRetryScreens.remove(screenId);
    m_retileRetryCount.remove(screenId);
    // A deferred focus request stranded by a no-op retile must not survive
    // the screen's removal: if the same screenId reconnects (re-subdivision
    // recreates vs:N ids), its first applyTiling would consume the stale
    // entry and activate a window from the previous session of that screen.
    m_pendingFocusByScreen.remove(screenId);
    // The initial-order seed, for the same reason and one the state teardown
    // cannot cover. That teardown only visits screens that HAVE a state at the
    // current key, and a screen seeded before any of its windows arrived never
    // built one — the seed probe is deliberately non-creating. So its seed used
    // to outlive its removal, and a strict order is consumed on re-entry as if
    // it were fresh.
    //
    // The reaper is not a backstop here: it re-arms itself indefinitely while
    // any seeded window still reads as minimized, so a removed screen could
    // hold a live timer chain as well as the stale order.
    clearPendingInitialOrder(screenId);
}

void AutotileEngine::clearPendingInitialOrder(const QString& screenId)
{
    m_pendingInitialOrders.remove(screenId);
    m_pendingOrderGeneration.remove(screenId);
    m_strictInitialOrderScreens.remove(screenId);
}

void AutotileEngine::setCurrentActivity(const QString& activity)
{
    // The established-flag (owned by the tracker, not a bare empty-string
    // sentinel on the previous value) keeps the "a" -> "" -> "b" sequence — an
    // activities-service restart hiccup — armed on the "" -> "b" leg. Here we
    // only log the actual change and OR the armed flag (|= so a simultaneous
    // desktop change's flag is not lost).
    const QString previous = m_context.currentActivity();
    const PhosphorEngine::ContextChange change = m_context.setCurrentActivity(activity);
    if (change.changed) {
        qCInfo(PhosphorTileEngine::lcTileEngine)
            << "Switching autotile context: activity" << previous << "->" << activity;
        m_isDesktopContextSwitch |= change.armSwitch;
        // Every screen's seed, for the reason the per-screen desktop switch
        // gives: activity is the other half of the context key, so this moves
        // every screen's context at once.
        const QStringList seeded = m_pendingInitialOrders.keys();
        for (const QString& screenId : seeded) {
            clearPendingInitialOrder(screenId);
        }
    }
}

void AutotileEngine::updateStickyScreenPins(const std::function<bool(const QString&)>& isWindowSticky)
{
    for (const QString& screenId : std::as_const(m_autotileScreens)) {
        const auto key = currentKeyForScreen(screenId);
        const PhosphorTiles::TilingState* state = m_states.stateForKey(key);
        if (!state) {
            continue;
        }

        const QStringList tiled = state->tiledWindows();
        const QStringList floating = state->floatingWindows();

        if (tiled.isEmpty() && floating.isEmpty()) {
            continue;
        }

        bool allSticky = true;
        for (const QString& wid : tiled) {
            if (!isWindowSticky(wid)) {
                allSticky = false;
                break;
            }
        }
        if (allSticky) {
            for (const QString& wid : floating) {
                if (!isWindowSticky(wid)) {
                    allSticky = false;
                    break;
                }
            }
        }

        if (allSticky) {
            if (!m_context.hasStickyPin(screenId)) {
                // Pin to current effective desktop (which is the desktop where
                // the PhosphorTiles::TilingState actually lives).
                m_context.setStickyPin(screenId, key.desktop);
                qCInfo(PhosphorTileEngine::lcTileEngine)
                    << "Pinning screen" << screenId << "to desktop" << key.desktop << "(all"
                    << (tiled.size() + floating.size()) << "windows sticky)";
            }
        } else {
            if (m_context.hasStickyPin(screenId)) {
                int pinnedDesktop = m_context.takeStickyPin(screenId);
                qCInfo(PhosphorTileEngine::lcTileEngine)
                    << "Unpinning screen" << screenId << "from desktop" << pinnedDesktop;

                // Migrate PhosphorTiles::TilingState from the pinned key to this
                // screen's CURRENT desktop key. The sticky-pin override was just
                // removed above, so currentKeyForScreen now resolves the screen's
                // effective desktop — its per-output virtual desktop under Plasma
                // 6.7 (#648), else the global current desktop. Identical to the
                // global current desktop when per-output desktops aren't in use.
                const int targetDesktop = currentKeyForScreen(screenId).desktop;
                if (pinnedDesktop != targetDesktop) {
                    TilingStateKey oldKey{screenId, pinnedDesktop, m_context.currentActivity()};
                    TilingStateKey newKey{screenId, targetDesktop, m_context.currentActivity()};

                    if (PhosphorTiles::TilingState* migratedState = m_states.stateForKey(oldKey)) {
                        // If a state already exists at the target key (e.g., created
                        // by tilingStateForScreen() during a transient lookup), delete it —
                        // the pinned state has the actual windows.
                        if (PhosphorTiles::TilingState* existing = m_states.takeState(newKey)) {
                            existing->deleteLater();
                        }
                        m_states.takeState(oldKey);
                        m_states.insertState(newKey, migratedState);

                        // The migrated state keeps its split ratio / master count, so
                        // carry its per-key user-tuned flags from oldKey to newKey; if
                        // it wasn't tuned, ensure newKey isn't left tuned by the
                        // replaced state deleted above.
                        if (m_userTunedSplitRatio.remove(oldKey)) {
                            m_userTunedSplitRatio.insert(newKey);
                        } else {
                            m_userTunedSplitRatio.remove(newKey);
                        }
                        if (m_userTunedMasterCount.remove(oldKey)) {
                            m_userTunedMasterCount.insert(newKey);
                        } else {
                            m_userTunedMasterCount.remove(newKey);
                        }
                        // A bag stashed under oldKey belongs to the layout that is
                        // moving, so it moves too, on the same terms as the tuned
                        // flags above. Leaving it behind would mean the layout and
                        // its script state part ways: restore never consumes an
                        // entry, so oldKey would keep a bag describing windows that
                        // now live at newKey, ready to be handed to whatever state
                        // is built there next. Safe to move because the tag is
                        // resolved per screen and both keys share a screenId.
                        if (auto oldIt = m_scriptStateStash.find(oldKey); oldIt != m_scriptStateStash.end()) {
                            StashedScriptState moved = std::move(oldIt->second);
                            m_scriptStateStash.erase(oldIt);
                            m_scriptStateStash.insert_or_assign(newKey, std::move(moved));
                        } else {
                            m_scriptStateStash.erase(newKey);
                        }

                        // Update window-to-key mapping
                        m_states.rekeyWindows(oldKey, newKey);

                        qCInfo(PhosphorTileEngine::lcTileEngine)
                            << "Migrated screen" << screenId << "state from desktop" << pinnedDesktop << "to"
                            << targetDesktop;
                    }
                }
            }
        }
    }
}

void AutotileEngine::setAutotileScreens(const QSet<QString>& screens)
{
    if (m_autotileScreens == screens) {
        // Must consume the desktop-context-switch flag even on early return.
        // Without this, a desktop switch between two desktops with the same
        // autotile screen set leaves the flag set. The NEXT setAutotileScreens
        // call (e.g. from a toggle OFF) then incorrectly reports isDesktopSwitch=true,
        // causing the effect to skip geometry/border restore on toggle OFF.
        const bool wasDesktopSwitch = m_isDesktopContextSwitch;
        m_isDesktopContextSwitch = false;
        // Discussion #219: a desktop/activity switch between two contexts with
        // an IDENTICAL autotile set still needs the compositor effect's
        // desktop-switch pass — its catch-scan re-adds windows that were moved
        // to this desktop while the user was away (the move untracked them on
        // the source desktop). Re-emit the unchanged set flagged as a desktop
        // switch. An empty set means no screen autotiles anywhere — nothing to
        // catch, skip the wakeup.
        //
        // Deliberately NO retile here, unlike the changed-set path's
        // returning-screen retile: the early return exists to keep
        // identical-set switches cheap, and re-entrant receivers rely on the
        // second call terminating without side effects. The cost is that
        // screen-geometry drift that happened while the user was on the other
        // desktop (panel added/removed) is not reconciled until the next
        // retile trigger on this desktop — availableGeometryChanged only
        // retiles the CURRENT desktop's state at change time. Accepted: the
        // drift window is panel changes made on another desktop, and the
        // first insert/close/float on this desktop heals it.
        if (wasDesktopSwitch && !m_autotileScreens.isEmpty()) {
            Q_EMIT autotileScreensChanged(QStringList(m_autotileScreens.begin(), m_autotileScreens.end()), true);
        }
        return;
    }

    const bool wasEnabled = !m_autotileScreens.isEmpty();
    const QSet<QString> added = screens - m_autotileScreens;
    const QSet<QString> removed = m_autotileScreens - screens;

    // If an active drag-insert preview touches any screen being removed (or
    // its prior screen), cancel it before states get torn down below. The
    // cancel path restores the window to its prior location; otherwise the
    // dangling preview would reference a PhosphorTiles::TilingState about to be deleted.
    if (m_dragInsertPreview) {
        const QString targetScreen = m_dragInsertPreview->targetScreenId;
        const QString priorScreen =
            m_dragInsertPreview->hadPriorState ? m_dragInsertPreview->priorKey.screenId : QString();
        if (removed.contains(targetScreen) || (!priorScreen.isEmpty() && removed.contains(priorScreen))) {
            cancelDragInsertPreview();
        }
    }

    m_autotileScreens = screens;

    // R1 fix: Retile newly-added screens without requiring pre-existing state.
    // tilingStateForScreen() creates the PhosphorTiles::TilingState lazily, so windows that arrive
    // shortly after (via KWin effect re-notification) have a state ready.
    for (const QString& screenId : added) {
        PhosphorTiles::TilingState* const addedState = tilingStateForScreen(screenId);
        // Hand back a bag rescued when this screen was toggled off. The state
        // factory attempts this too, for a context re-created later (a desktop
        // switch back to where the toggle happened), but the factory alone is not
        // enough on the re-enable path: the daemon applies per-screen config
        // BEFORE re-activating screens, and applyPerScreenConfig both creates the
        // state and then wipes its bag. That wipe compares against a resolver that
        // no longer remembers this screen's algorithm — the toggle-off dropped the
        // in-memory override — so it reads global -> override and clears a bag it
        // should not have. Re-applying here, after that wipe, is what makes the
        // round trip survive.
        //
        // ONLY into an empty bag, because unlike the factories this can be handed
        // a state that was never torn down. Toggling off while another desktop is
        // current tears down only that context, so this screen's state here may
        // still be live and holding adjustments made AFTER the stashed entry was
        // written — restore does not consume, so that entry outlives its own
        // re-enable. Writing over a live bag would revert the user's newer layout
        // to an older copy of itself. An empty bag means either a fresh state or
        // the wipe described above, which are exactly the cases this is for.
        if (addedState && addedState->scriptState().isEmpty()) {
            restoreStashedScriptState(currentKeyForScreen(screenId), addedState);
        }
        // Skip retile if windows are expected to arrive shortly (pending initial
        // order from seedAutotileOrderForScreen). The KWin effect sends windowOpened
        // D-Bus calls after receiving autotileScreensChanged, and each insertWindow
        // schedules its own retile. Retiling an empty screen here produces a wasted
        // empty windowsTiled signal + stagger generation increment, which can interfere
        // with the first real retile's animation timing.
        // For screen hotplug (no pending order), windows are already in the PhosphorTiles::TilingState
        // and the retile is needed to reflow them on the new screen.
        //
        // Skip retile when pending initial order exists (windows arriving shortly
        // via D-Bus). For desktop return with existing tiled windows, still retile
        // to ensure geometry is up-to-date (screen geometry may have changed while
        // on another desktop, e.g., panel added/removed). The effect-side borderless
        // re-application handles the visual state; the retile ensures positions match
        // the current screen geometry.
        // Only consume the pending order eagerly for STRICT entries (mode
        // transition seeded by setInitialWindowOrder — windows are already
        // open in KWin and need to be added to the autotile state with the
        // computed order BEFORE the effect's windowOpened re-announce lands,
        // so the first retile uses the seeded order; the later windowOpened
        // for an already-present window is a tracked no-op insert).
        // Advisory entries describe historical positions for windows that aren't open yet —
        // pre-seeding the state would create ghost entries the user can't
        // close, and would also override the user's insertPosition preference
        // when the windows actually do open. Leave the advisory order in
        // pendingInitialOrders for insertWindow() to consult on arrival.
        if (m_pendingInitialOrders.contains(screenId) && m_strictInitialOrderScreens.contains(screenId)) {
            const QStringList order = m_pendingInitialOrders.value(screenId);
            if (!m_windowRegistry) {
                // Without a registry the seed cannot distinguish minimized
                // windows and will tile every seeded entry — visible in
                // production as a hidden window holding a layout slot. Warn
                // loudly; headless/test engines legitimately run without one.
                qCWarning(PhosphorTileEngine::lcTileEngine)
                    << "setAutotileScreens: strict seed for" << screenId
                    << "has no window registry — minimized windows cannot be deferred";
            }
            bool hasDeferredMinimizedWindow = false;
            PhosphorTiles::TilingState* ts = tilingStateForScreen(screenId);
            if (ts) {
                const TilingStateKey stateKey = currentKeyForScreen(screenId);
                QStringList notTileable;
                for (const QString& windowId : order) {
                    // Defer on engaged-true AND on unknown (record missing):
                    // a window the registry cannot vouch for must not claim a
                    // tile — the deferral self-heals when the effect's
                    // windowOpened re-announce (sent only for visible
                    // windows) consumes the pending slot at its seeded index.
                    if (m_windowRegistry && m_windowRegistry->minimizedState(windowId).value_or(true)) {
                        hasDeferredMinimizedWindow = true;
                        continue;
                    }
                    // Same admission gate windowOpened applies: a window the
                    // daemon's order names but that must not tile (excluded
                    // app, special window type) would otherwise be seeded
                    // into the layout and — containsWindow short-circuiting
                    // its later re-announce — could never self-heal. Dropped
                    // from the pending order below so a retained order (a
                    // deferred minimized sibling) cannot wedge on it.
                    if (!shouldTileWindow(windowId)) {
                        notTileable.append(windowId);
                        continue;
                    }
                    if (!ts->containsWindow(windowId)) {
                        // Single-owner guard: a mode transition can seed a
                        // window that another context's state still owns
                        // (e.g. tracked on a different desktop by a
                        // catch-scan race). Release the old owner first —
                        // same primitive handoffReceive uses — or
                        // setKeyForWindow below re-points the reverse map
                        // and leaves a permanent ghost in the old state.
                        const TilingStateKey oldKey = m_states.keyForWindow(windowId);
                        if (!oldKey.screenId.isEmpty() && oldKey != stateKey) {
                            handoffRelease(windowId);
                            if (oldKey.screenId != screenId) {
                                scheduleRetileForScreen(oldKey.screenId);
                            }
                        }
                        ts->addWindow(windowId);
                        // Register engine tracking immediately — without the
                        // key entry, a window closing before the effect's
                        // windowOpened round-trip hits onWindowRemoved's
                        // empty-stored-key early return and stays a permanent
                        // ghost the layout retiles around.
                        m_states.setKeyForWindow(windowId, stateKey);
                        // Restore floating state from the unified record (single source
                        // of truth). Without this, windows added from pending orders lose
                        // their floating state because windowOpened's floating restore is
                        // skipped when the window already exists in the PhosphorTiles::TilingState.
                        // Same-instance record only: pending orders are built from LIVE session
                        // ids, so a same-app sibling's floating record must not float
                        // this window (relogin restores go through insertWindow's take()).
                        if (m_windowTracker) {
                            const auto rec = m_windowTracker->placementStore().peekExact(windowId);
                            if (rec
                                && rec->slotFor(engineId()).state == PhosphorEngine::WindowPlacement::stateFloating()) {
                                ts->setFloating(windowId, true);
                            }
                        }
                        // Same "Float this app" admission insertWindow applies
                        // at line ~292: a float-ruled window seeded tiled would
                        // hold a tile its open-time rule says it must not, and
                        // the containsWindow short-circuit on its re-announce
                        // means nothing later corrects it.
                        if (!ts->isFloating(windowId) && insertShouldFloat(windowId, screenId)) {
                            ts->setFloating(windowId, true);
                        }
                        // Same lifecycle hook every other insert site runs — a
                        // memory algorithm (dwindle-memory's split tree) must
                        // see seeded arrivals or its bookkeeping goes blind to
                        // them.
                        notifyAlgorithmWindowAdded(ts, screenId, windowId);
                        // Announce on the passive channel via the canonical
                        // insert-time sync (both directions: restored-floating
                        // OR seeded-tiled-over-a-stale-WTS-float-bit). The
                        // later windowOpened for this already-present window
                        // is a tracked no-op insert whose float sync is
                        // skipped, so without this the seed's float state is
                        // never broadcast — subscribers (and the adaptor's
                        // last-broadcast gate) stay stale until a daemon
                        // reconnect. The gate dedups when they already agree.
                        emitInsertFloatStateSync(windowId, screenId);
                    }
                }
                for (const QString& windowId : std::as_const(notTileable)) {
                    m_pendingInitialOrders[screenId].removeAll(windowId);
                }
                if (!hasDeferredMinimizedWindow) {
                    m_pendingInitialOrders.remove(screenId);
                    m_pendingOrderGeneration.remove(screenId);
                    m_strictInitialOrderScreens.remove(screenId);
                }
            } else {
                // Null state — the screen is known to autotile but the state
                // factory refused (virtual-screen teardown race). RETAIN the
                // pending order rather than silently discarding the computed
                // seed: insertWindow consumes it entry-by-entry when the
                // effect's windowOpened announcements land.
                qCWarning(PhosphorTileEngine::lcTileEngine) << "setAutotileScreens: no tiling state for" << screenId
                                                            << "— strict seed retained for insert-time consumption";
            }
        }
        scheduleRetileForScreen(screenId);
    }

    // Only prune states for the CURRENT desktop/activity. States belonging to
    // other desktops are preserved so desktop switching is a fast state swap
    // (no window release/re-add). windowsReleased MUST NOT fire
    // for desktop/activity transitions — only for true autotile disable.
    QStringList releasedWindows;
    QSet<QString> placementChangedScreens;
    // Only prune states that match the current desktop/activity AND whose screen
    // is no longer in the autotile set. States for other contexts are left
    // untouched here — by the time their desktop becomes current the screen is
    // already absent from m_autotileScreens, so this loop never sees them again;
    // they are healed per-window (windowFocused / windowOpened migration),
    // reaped wholesale by pruneStatesForDesktop / pruneStatesForActivities when
    // their desktop or activity is destroyed, and by
    // pruneStatesForRemovedScreen when the OUTPUT itself is unplugged (the
    // daemon calls it for all three engines; this sweep alone would leak
    // sibling-context states for a removed monitor).
    m_states.removeStatesIf(
        [&](const TilingStateKey& key, PhosphorTiles::TilingState*) {
            return key.desktop == currentKeyForScreen(key.screenId).desktop
                && key.activity == m_context.currentActivity() && removed.contains(key.screenId);
        },
        [&](const TilingStateKey& key, PhosphorTiles::TilingState* state) {
            // Rescue the script-state bag BEFORE the state dies and before the
            // override drop below moves the screen's effective algorithm. A
            // re-enable recreates this key and picks the bag back up, so a
            // toggle-off/on round trip keeps the user's manual tile adjustments
            // instead of laying out from scratch.
            stashScriptState(key, state);
            if (releaseScreenStateForTeardown(key.screenId, state, releasedWindows)) {
                placementChangedScreens.insert(key.screenId);
            }
            // Toggle-off drops only the resolver's IN-MEMORY overrides (they are
            // re-derived from settings on re-enable); the persisted per-screen
            // settings deliberately survive — a user toggling autotile off must
            // not lose their per-monitor configuration. The orphaned-virtual-
            // screen teardown follows the same rule: vs:N ids are recreated by
            // a later re-subdivision, so its persisted layer survives too.
            m_configResolver->removeOverridesForScreen(key.screenId);
            m_userTunedSplitRatio.remove(key);
            m_userTunedMasterCount.remove(key);
        });
    // Clean up reverse-map entries for released windows BEFORE emitting the
    // signal. Signal handlers (the daemon's handleEngineWindowsReleased) check zone
    // assignments and floating state — stale mappings would cause them to see
    // phantom candidates.
    for (const QString& windowId : std::as_const(releasedWindows)) {
        m_states.removeWindow(windowId);
    }

    if (!releasedWindows.isEmpty()) {
        Q_EMIT windowsReleased(releasedWindows, removed);
    }

    // Clean up any remaining overflow entries for removed screens. KNOWN
    // LIMITATION: the overflow bucket is keyed per-screenId only, while the
    // prune loop above (by design) tears down current-context states only —
    // a preserved other-desktop/activity state on a removed screen loses its
    // overflow markers here, so its save-time capturePlacement records
    // overflow-floated windows as user floats (they re-float instead of
    // re-tiling on re-enable). Accepted: fixing it requires re-keying
    // OverflowManager per (screen, context), and the window is narrow —
    // toggle-off while another context holds overflow on the same screen.
    m_overflow.clearForRemovedScreens(m_autotileScreens);

    // Clear the sticky-pin override for removed screens. The per-output-VD map
    // (#648) deliberately STAYS: these screens are leaving autotile, not going
    // away, and their desktop is compositor truth this engine cannot re-derive.
    // Dropping it fell back to the global desktop, which is set once at startup
    // — see ScreenContextTracker::releaseScreenOwnership.
    for (const QString& screenId : removed) {
        m_context.releaseScreenOwnership(screenId);
    }

    // Drop stashed bags belonging to screens that are no longer connected. They
    // can never be harvested or matched again, so without this a monitor
    // unplugged for good leaves its bag behind for the session. Gated on
    // isKnownScreen and deliberately NOT on autotile membership: a screen the
    // user merely toggled OUT of autotile is still connected and keeping its bag
    // is the entire point of the stash.
    std::erase_if(m_scriptStateStash, [this](const auto& entry) {
        return !isKnownScreen(entry.first.screenId);
    });
    // The remembered "built under" id is bookkeeping for those same bags, so it
    // is retired on the same event. Left behind it would be the stale "old" side
    // of a comparison for an id that is never coming back.
    m_configResolver->forgetRememberedAlgorithmsForUnknownScreens();

    // Clear any pending deferred retiles and retry state for removed screens
    for (auto pit = m_pendingRetileScreens.begin(); pit != m_pendingRetileScreens.end();) {
        if (!m_autotileScreens.contains(*pit)) {
            pit = m_pendingRetileScreens.erase(pit);
        } else {
            ++pit;
        }
    }
    for (const QString& screenId : removed) {
        clearScreenScheduling(screenId);
    }

    const bool nowEnabled = !m_autotileScreens.isEmpty();
    // Capture before clearing — the emit below needs the original value.
    const bool wasDesktopSwitch = m_isDesktopContextSwitch;
    m_isDesktopContextSwitch = false;

    if (wasEnabled != nowEnabled && !wasDesktopSwitch) {
        // Only emit enabledChanged for actual mode toggles, not desktop/activity
        // switch. On desktop switch the effect must NOT clear borderless/monocle/
        // stacking tracking (enabledChanged false) or re-process windows (true).
        Q_EMIT enabledChanged(nowEnabled);
    }

    // DELIBERATE ORDER: autotileScreensChanged first, placementChanged after.
    // The screens signal drives the effect's mode-transition pass; the
    // placement signals only schedule the daemon's debounced save, which must
    // snapshot state AFTER the transition's releases are all in place.
    Q_EMIT autotileScreensChanged(QStringList(m_autotileScreens.begin(), m_autotileScreens.end()), wasDesktopSwitch);
    for (const QString& screenId : std::as_const(placementChangedScreens)) {
        Q_EMIT placementChanged(screenId);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Algorithm selection
// ═══════════════════════════════════════════════════════════════════════════════

} // namespace PhosphorTileEngine
