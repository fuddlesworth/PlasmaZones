// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Qt headers
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <QVarLengthArray>

// Project headers
#include <PhosphorTileEngine/AutotileEngine.h>
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
    return state && !state->isFloating(windowId);
}

bool AutotileEngine::isWindowFloatingInAutotile(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    auto it = m_states.windowKeys().constFind(windowId);
    if (it == m_states.windowKeys().constEnd()) {
        return false;
    }
    const PhosphorTiles::TilingState* state = m_states.stateForKey(it.value());
    return state && state->isFloating(windowId);
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
    // updateAutotileScreens(); that first push must NOT read as a switch — or
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
    }
}

void AutotileEngine::clearCurrentDesktopForScreen(const QString& screenId)
{
    m_context.clearCurrentDesktopForScreen(screenId);
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
            const QStringList order = m_pendingInitialOrders.take(screenId);
            m_pendingOrderGeneration.remove(screenId);
            m_strictInitialOrderScreens.remove(screenId);
            PhosphorTiles::TilingState* ts = tilingStateForScreen(screenId);
            if (ts) {
                const TilingStateKey stateKey = currentKeyForScreen(screenId);
                for (const QString& windowId : order) {
                    if (!ts->containsWindow(windowId)) {
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
                        // Exact record only: pending orders are built from LIVE session
                        // ids, so a same-app sibling's floating record must not float
                        // this window (relogin restores go through insertWindow's take()).
                        if (m_windowTracker) {
                            const auto rec = m_windowTracker->placementStore().peekExact(windowId);
                            if (rec
                                && rec->slotFor(engineId()).state == PhosphorEngine::WindowPlacement::stateFloating()) {
                                ts->setFloating(windowId, true);
                            }
                        }
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
            }
        }
        scheduleRetileForScreen(screenId);
    }

    // Only prune states for the CURRENT desktop/activity. States belonging to
    // other desktops are preserved so desktop switching is a fast state swap
    // (no window release/re-add). windowsReleasedFromTiling MUST NOT fire
    // for desktop/activity transitions — only for true autotile disable.
    QStringList releasedWindows;
    // Only prune states that match the current desktop/activity AND whose screen
    // is no longer in the autotile set. States for other contexts are left
    // untouched here — by the time their desktop becomes current the screen is
    // already absent from m_autotileScreens, so this loop never sees them again;
    // they are healed per-window (windowFocused / windowOpened migration) and
    // reaped wholesale by pruneStatesForDesktop / pruneStatesForActivities when
    // their desktop or activity is destroyed.
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
            releaseScreenStateForTeardown(key.screenId, state, releasedWindows);
            // Toggle-off drops only the resolver's IN-MEMORY overrides (they are
            // re-derived from settings on re-enable); the persisted per-screen
            // settings deliberately survive — a user toggling autotile off must
            // not lose their per-monitor configuration. Contrast with the
            // orphaned-virtual-screen teardown, which purges both layers because
            // a dead VS id is never reused.
            m_configResolver->removeOverridesForScreen(key.screenId);
            m_userTunedSplitRatio.remove(key);
            m_userTunedMasterCount.remove(key);
        });
    // Clean up reverse-map entries for released windows BEFORE emitting the
    // signal. Signal handlers (signals.cpp windowsReleasedFromTiling) check zone
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

    // Clear per-screen desktop maps for removed screens — both the sticky-pin
    // override and the per-output-VD map (#648).
    for (const QString& screenId : removed) {
        m_context.removeScreen(screenId);
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
        m_retileRetryScreens.remove(screenId);
        m_retileRetryCount.remove(screenId);
        // A deferred focus request stranded by a no-op retile must not
        // survive the screen's removal: if the same screenId reconnects,
        // its first applyTiling would consume the stale entry and activate
        // a window from the previous session of that screen.
        m_pendingFocusByScreen.remove(screenId);
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

    Q_EMIT autotileScreensChanged(QStringList(m_autotileScreens.begin(), m_autotileScreens.end()), wasDesktopSwitch);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Algorithm selection
// ═══════════════════════════════════════════════════════════════════════════════

} // namespace PhosphorTileEngine
