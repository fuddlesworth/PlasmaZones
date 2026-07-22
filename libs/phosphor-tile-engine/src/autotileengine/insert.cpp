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

void AutotileEngine::emitInsertFloatStateSync(const QString& windowId, const QString& screenId)
{
    // Read-only lookup — must NOT lazily materialize a state. tilingStateForScreen
    // would create one for a known-but-stateless screen; this method only reads
    // isFloating right after a window lands in an existing state (insertWindow,
    // or the strict-initial-order seed in setAutotileScreens), so it already exists.
    PhosphorTiles::TilingState* state = m_states.stateForKey(currentKeyForScreen(screenId));
    if (!state) {
        return;
    }
    // Sync floating state to daemon. Float state is per-mode:
    // - Restored as floating from autotile's saved set → notify daemon to set WTS floating
    // - Inserted as tiled but WTS says floating (stale snap-mode float) → clear WTS floating
    //
    // Use windowFloatingStateSynced (not windowFloatingChanged): this is a
    // passive state-sync on window insertion, not a user float toggle. The
    // daemon must NOT restore pre-tile geometry here — the window was just
    // added (e.g. dropped onto an autotile VS from a snap VS) and already
    // has a valid position. Routing through windowFloatingChanged causes
    // syncAutotileFloatState to call applyGeometryForFloat, which teleports
    // the window to a cross-screen-adjusted rect and resizes it.
    if (state->isFloating(windowId)) {
        Q_EMIT windowFloatingStateSynced(windowId, true, screenId);
    } else if (m_windowTracker && m_windowTracker->isWindowFloating(windowId)) {
        Q_EMIT windowFloatingStateSynced(windowId, false, screenId);
    }
}

bool AutotileEngine::insertShouldFloat(const QString& windowId) const
{
    // A window ARRIVING from another state was already managed, so the open-time
    // "Float this app" rule has nothing to say about it — it carries the float
    // state it held on the source. Re-running the predicate here would re-float a
    // float-ruled window the user had explicitly tiled with Meta+F: the predicate
    // is a pure app-rule match that stays true for the window's whole life, so a
    // cross-output move would silently undo the user's tiling.
    if (m_migrationArrival && m_migrationArrival->windowId == windowId) {
        return m_migrationArrival->wasFloating;
    }
    // A genuine open consults the rule.
    return m_floatPredicate && m_floatPredicate(windowId);
}

bool AutotileEngine::insertWindow(const QString& windowId, const QString& screenId)
{
    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    if (!state) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "AutotileEngine::insertWindow: failed to get state for screen" << screenId;
        return false;
    }

    // Check if window already tracked in this screen's tiling state
    // Note: we check the PhosphorTiles::TilingState (not the m_states reverse map)
    // because windowOpened() records the screen mapping in m_states *before* calling
    // onWindowAdded(), so m_states.hasWindow() would always be true via that path.
    if (state->containsWindow(windowId)) {
        return false;
    }

    // Resolve appId via the registry so mid-session class mutations (Emby and
    // friends) land in the correct restore bucket. Falls back to parsing the
    // canonical windowId when no registry is attached (unit tests).
    const QString appId = currentAppIdFor(windowId);
    const bool hasStableAppId = !appId.isEmpty() && (appId != windowId);

    // Check if this window has a pre-seeded position from zone-ordered transition.
    // Take a value copy of the pending list — the erase below invalidates iterators/refs.
    bool inserted = false;
    auto pendingIt = m_pendingInitialOrders.find(screenId);
    if (pendingIt != m_pendingInitialOrders.end()) {
        const QStringList pendingOrder = pendingIt.value(); // copy, not reference (BUG-1 fix)
        int desiredPos = pendingOrder.indexOf(windowId);
        // An exact windowId match means KWin held this window across the
        // daemon's lifetime gap (i.e. only the daemon reloaded; the window's
        // compositor-assigned identity is unchanged). The saved position is
        // therefore an authoritative restoration target, not yesterday's
        // historical hint — treat it as strict below so daemon-reload bursts
        // restore the prior layout even when arrivals are out of sequence.
        const bool exactWindowIdMatch = (desiredPos >= 0);

        // Fallback: match by appId when exact windowId not found (KWin restart
        // changes UUIDs, so saved windowIds have stale suffixes). FIFO consumption
        // prevents multi-instance apps from all matching the first entry.
        if (desiredPos < 0 && hasStableAppId) {
            for (int i = 0; i < pendingOrder.size(); ++i) {
                // Compare using currentAppIdFor so both sides resolve to the
                // latest class — an entry saved before a rename still matches.
                if (currentAppIdFor(pendingOrder.at(i)) == appId && !state->containsWindow(pendingOrder.at(i))) {
                    desiredPos = i;
                    // Replace stale UUID in the live map so it won't match again
                    m_pendingInitialOrders[screenId][i] = windowId;
                    qCDebug(PhosphorTileEngine::lcTileEngine)
                        << "AppId fallback matched" << windowId << "to pending position" << i;
                    break;
                }
            }
        }

        if (desiredPos >= 0) {
            // Count ALL pre-seeded windows (including floating) with lower desired position
            // already in state. addWindow() inserts into m_windowOrder which includes both
            // tiled and floating windows, so the offset must account for all of them.
            int insertAt = 0;
            for (int i = 0; i < desiredPos; ++i) {
                const QString& earlier = pendingOrder.at(i);
                if (state->containsWindow(earlier)) {
                    ++insertAt;
                }
            }
            // Strict ordering applies in two cases:
            //   1. Mode transition via setInitialWindowOrder — the daemon
            //      pre-computed an order from the prior mode's zones and
            //      intentionally wants it preserved.
            //   2. Cross-session restore where the arriving windowId matches
            //      the saved entry exactly. Exact match means KWin retained
            //      the window across the gap (i.e. only the daemon
            //      reloaded), so the saved position is a real restoration
            //      target — pushing live entries to honor it just rebuilds
            //      yesterday's layout, which is the user's intent on a
            //      daemon reload.
            //
            // Advisory ordering applies to cross-session arrivals matched by
            // appId fallback (UUID drift after a KWin restart, or a new
            // window today that happens to share an app class with a saved
            // entry). For those, the saved position is yesterday's hint
            // rather than today's reality — honor it only when it appends
            // at the current tail. If it would push existing windows, fall
            // through to insertPosition so the user's "After existing" /
            // "After focused" / "As main window" setting wins for new
            // arrivals.
            const bool strict = m_strictInitialOrderScreens.contains(screenId) || exactWindowIdMatch;
            if (strict || insertAt >= state->windowCount()) {
                state->addWindow(windowId, insertAt);
                inserted = true;
                qCDebug(PhosphorTileEngine::lcTileEngine)
                    << "Inserted pre-seeded window" << windowId << "at position=" << insertAt
                    << "desired=" << desiredPos << (strict ? "(strict)" : "(advisory)");
            } else {
                qCDebug(PhosphorTileEngine::lcTileEngine)
                    << "Advisory pre-seeded window" << windowId << "at desired=" << desiredPos
                    << "would push existing windows (insertAt=" << insertAt << " < windowCount=" << state->windowCount()
                    << ") — falling back to insertPosition";
            }
        }
        // Clean up pending order when all pre-seeded windows have been inserted (or closed)
        if (inserted) {
            cleanupPendingOrderIfResolved(screenId);
        }
    }

    // Close/reopen restore takes precedence over the insert-position config: a
    // window closed while FLOATING reopens at its floated geometry, STILL
    // FLOATING — never inserted into the tile layout (marked floating in
    // TilingState; onWindowAdded then emits windowFloatingStateSynced → daemon
    // passive float-sync, NO geometry teleport). inserted=true → the tile-insert
    // paths below are all skipped; the function tail still records
    // m_states and returns true.
    // Close/reopen restore from the unified placement store: ONE record per window
    // holds both engines' slots + the shared per-screen free geometry. Take it once
    // and branch on the autotile slot — a FLOATING slot restores the window floating
    // (consumed only when the record's screen matches the opening screen or is
    // empty; the GEOMETRY move below uses ONLY the screen-local recorded rect for
    // restoreScreen — no cross-screen fallback); a TILED slot restores it at its saved
    // order in the SAME context (index-based — best-effort if neighbours moved;
    // wasFloating is not relevant since the slot state IS the intent). Re-record
    // bound to the live windowId so the snap slot + per-screen free geometry survive
    // and a second instance of the same app takes the next FIFO entry.
    const TilingStateKey currentKey = currentKeyForScreen(screenId);
    if (!inserted && hasStableAppId && m_windowTracker) {
        using PhosphorEngine::WindowPlacement;
        auto rec = m_windowTracker->placementStore().take(windowId, appId, [&](const WindowPlacement& p) {
            const PhosphorEngine::EngineSlot s = p.slotFor(engineId());
            if (s.state == WindowPlacement::stateFloating()) {
                return p.screenId.isEmpty() || p.screenId == screenId;
            }
            if (s.state == WindowPlacement::stateTiled()) {
                return p.screenId == currentKey.screenId && p.virtualDesktop == currentKey.desktop
                    && p.activity == currentKey.activity;
            }
            return false;
        });
        if (rec) {
            // Re-record bound to the LIVE windowId so the autotile slot + per-screen
            // free/float geometry survive the reopen. KWin assigns a NEW uuid at
            // logout/login, so the record matches by appId FIFO (not uuid-exact);
            // without re-binding, a FIFO reopen consumes the record and the window
            // loses its remembered float-back. Re-binding appends under the live uuid
            // (newest in the appId bucket), so a SECOND instance of the same app still
            // takes an OLDER sibling record first on its own reopen — multi-instance
            // FIFO distribution is preserved.
            const PhosphorEngine::EngineSlot slot = rec->slotFor(engineId());
            const QString restoreScreen = rec->screenId.isEmpty() ? screenId : rec->screenId;
            rec->windowId = windowId;
            m_windowTracker->placementStore().record(*rec);
            if (slot.state == WindowPlacement::stateFloating()) {
                state->addWindow(windowId);
                state->setFloating(windowId, true);
                inserted = true;
                // SCREEN-LOCAL recorded position only — deliberately NOT the
                // anyFreeGeometry() cross-screen fallback (mirroring snap's
                // resolveWindowRestore). The free geometry is in global compositor
                // coordinates; applying a rect captured on a DIFFERENT screen while
                // the float tracking points at restoreScreen would teleport the
                // window to a third monitor with the state saying otherwise — a
                // visible/state desync. No recorded rect for restoreScreen → nothing
                // meaningful to restore, so the move is skipped.
                const QRect freeGeo = rec->freeGeometryFor(restoreScreen);
                // The window is marked floating unconditionally above; the geometry
                // MOVE is gated on the floated-position-restore opt-in (daemon-wired
                // autotileRestoreFloatedWindowsOnLogin setting + per-window
                // RestorePosition rule). When the predicate is unset (tests / no
                // daemon) the move always fires, preserving historical behaviour.
                const bool restorePosition = !m_restorePositionPredicate || m_restorePositionPredicate(windowId);
                if (freeGeo.isValid() && restorePosition) {
                    Q_EMIT geometryRestoreRequested(windowId, freeGeo, restoreScreen);
                }
                qCInfo(PhosphorTileEngine::lcTileEngine)
                    << "insertWindow: float-restore for" << windowId << "to" << freeGeo << "on" << restoreScreen
                    << "move=" << (freeGeo.isValid() && restorePosition);
            } else {
                const int savedPos = slot.order;
                const int clampedPos = savedPos < 0 ? state->windowCount() : qMin(savedPos, state->windowCount());
                state->addWindow(windowId, clampedPos);
                inserted = true;
                qCDebug(PhosphorTileEngine::lcTileEngine)
                    << "insertWindow: restored" << windowId << "from placement store at position=" << clampedPos
                    << "(saved=" << savedPos << ")";
            }
        }
    }

    if (!inserted) {
        // Insert based on config preference
        insertWindowByConfigOrder(state, windowId, screenId);
    }

    // Float restore is handled entirely by the record take() above (a floating
    // autotile slot inserts floating + applies the shared free geometry). No
    // parallel saved-floating set — the WindowPlacement record is the single
    // source of truth for cross-mode float state.

    // Float the window when its insert state says so: a matched "Float this app"
    // rule on a genuine open, or the live float state a migrating window carried
    // across (insertShouldFloat). Either way it is inserted above (so it stays
    // managed and Meta+F can re-tile it), then marked floating here, identical to
    // a manual float toggle. Guarded on not-already-floating so the
    // placement-record float-restore branch above is not re-applied. onWindowAdded
    // then emits windowFloatingStateSynced so the daemon mirrors the state.
    if (!state->isFloating(windowId) && insertShouldFloat(windowId)) {
        state->setFloating(windowId, true);
    }

    m_states.setKeyForWindow(windowId, currentKey);
    return true;
}

void AutotileEngine::insertWindowByConfigOrder(PhosphorTiles::TilingState* state, const QString& windowId,
                                               const QString& screenId)
{
    // Per-screen resolution: a per-screen config override or a context
    // SetInsertPosition rule wins over the global config for this window's screen.
    // The screen is passed in (NOT derived via screenForWindow) because the window
    // is not yet keyed at either call site — screenForWindow would fall back to the
    // primary screen and both ignore a non-primary override and spam a warning.
    switch (effectiveInsertPosition(screenId)) {
    case AutotileConfig::InsertPosition::End:
        state->addWindow(windowId);
        break;
    case AutotileConfig::InsertPosition::AfterFocused:
        state->insertAfterFocused(windowId);
        break;
    case AutotileConfig::InsertPosition::AsMaster:
        state->addWindow(windowId);
        state->moveToFront(windowId);
        break;
    }
}

void AutotileEngine::removeWindow(const QString& windowId)
{
    m_windowMinSizes.remove(windowId);
    m_overflow.clearOverflow(windowId);
    const TilingStateKey key = m_states.takeWindow(windowId);
    if (key.screenId.isEmpty()) {
        return;
    }

    PhosphorTiles::TilingState* state = m_states.stateForKey(key);
    if (state) {
        // No position is saved here. The window's autotiled placement (its position)
        // is captured into the unified WindowPlacementStore by the common close hook
        // (WindowTrackingAdaptor::windowClosed → capturePlacement) BEFORE this
        // removal runs, and by the save-time snapshot for still-open windows. The
        // reopen consumes that record in insertWindow().
        state->removeWindow(windowId);
    }

    // Purge closed window from pending initial orders.
    // If a pre-seeded window closes before arriving at the autotile engine,
    // the pending order would leak indefinitely without this cleanup.
    for (auto pit = m_pendingInitialOrders.begin(); pit != m_pendingInitialOrders.end();) {
        pit.value().removeAll(windowId);
        if (pit.value().isEmpty()) {
            m_pendingOrderGeneration.remove(pit.key());
            m_strictInitialOrderScreens.remove(pit.key());
            pit = m_pendingInitialOrders.erase(pit);
        } else {
            const QString screen = pit.key();
            ++pit; // advance before potential erase by helper
            cleanupPendingOrderIfResolved(screen);
        }
    }
}

} // namespace PhosphorTileEngine
