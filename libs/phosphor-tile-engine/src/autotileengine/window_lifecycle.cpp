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

void AutotileEngine::windowOpened(const QString& rawWindowId, const QString& screenId, int minWidth, int minHeight)
{
    if (!warnIfEmptyWindowId(rawWindowId, "windowOpened")) {
        return;
    }
    // First observation of this window — canonicalize locks the canonical key
    // used by every internal map from here on. Subsequent arrivals with a
    // mutated appId (Electron/CEF apps) resolve back to the same string so
    // m_states / PhosphorTiles::TilingState / m_windowMinSizes stay consistent.
    const QString windowId = canonicalizeWindowId(rawWindowId);

    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "windowOpened:" << windowId << "screen=" << screenId << "minSize=" << minWidth << "x" << minHeight;

    // Cross-engine coordination (the reciprocal of SnapEngine::resolveWindowRestore's
    // recorded-screen ownership gate). On FIRST observation, if this window carries a
    // SNAPPED placement record whose RECORDED screen is itself in snapping mode, snap
    // will restore it cross-screen to that monitor — it only landed on this autotile
    // screen because KWin's session restore placed it here. Autotile must NOT track or
    // tile it, or both engines would claim the same window (snap moves it to its snap
    // monitor while autotile tiles it here). Bail BEFORE m_states is set so
    // autotile leaves no trace. The peek does NOT consume the record — snap's restore
    // is the consumer. A snapped record whose recorded screen is THIS (autotile) screen
    // is not in snapping mode, so the check fails and autotile keeps the window — the
    // screen's own mode owns it, symmetric with snap's same-screen defer.
    //
    // Restricted to windows NOT already autotile-tracked: a window autotile already
    // manages (re-emitted on a runtime screen/desktop move, or explicitly handed off)
    // is autotile's — its snap slot is then frozen cross-mode memory, not a pending
    // restore. Deferring such a window would both yank a live tile and strand a ghost
    // in its current TilingState (the cross-screen cleanup below is skipped on an early
    // return). Only a first-observation open — the login/session-restore case — races
    // snap, and that is the only case this guard fires for.
    //
    // Gated on snappingPreferred() (the global snap toggle): when snapping is disabled,
    // SnapEngine::resolveWindowRestore returns early (isEnabled() false) and will NEVER
    // claim the window, so deferring here would strand it unmanaged. In that state
    // autotile keeps the window and tiles it normally.
    if (!screenId.isEmpty() && m_windowTracker && m_layoutManager && m_layoutManager->snappingPreferred()
        && !m_states.hasWindow(windowId)) {
        const QString appId = currentAppIdFor(windowId);
        if (!appId.isEmpty() && appId != windowId) {
            // Shared predicate with snap's reciprocal gate
            // (SnapEngine::resolveWindowRestore) — both engines run
            // PhosphorEngine::pendingCrossScreenSnapRestore over the same record
            // fields, so a window is never both deferred-and-claimed or
            // both-skipped.
            const auto snapCrossRestorePending = [&](const PhosphorEngine::WindowPlacement& p) {
                return PhosphorEngine::pendingCrossScreenSnapRestore(
                    p, screenId, [&](const QString& rec, int desktop, const QString& activity) {
                        return m_layoutManager->modeForScreen(rec, desktop, activity)
                            == PhosphorZones::AssignmentEntry::Mode::Snapping;
                    });
            };
            if (m_windowTracker->placementStore().peek(windowId, appId, snapCrossRestorePending).has_value()) {
                qCInfo(PhosphorTileEngine::lcTileEngine)
                    << "windowOpened:" << windowId << "on autotile screen" << screenId
                    << "defers to snap — carries a cross-screen snap restore";
                return;
            }
        }
    }

    // If the window is already tracked on a DIFFERENT screen (e.g., dragged from
    // VS2 to VS1), remove it from the old screen's PhosphorTiles::TilingState first. Without this,
    // the window remains in the old PhosphorTiles::TilingState as a ghost entry — the old screen
    // retiles around a window that's no longer there, and zone assignments stay stale.
    if (!screenId.isEmpty()) {
        const TilingStateKey newKey = currentKeyForScreen(screenId);
        auto existingIt = m_states.windowKeys().constFind(windowId);
        if (existingIt != m_states.windowKeys().constEnd() && existingIt.value() != newKey) {
            const TilingStateKey oldKey = existingIt.value();
            PhosphorTiles::TilingState* oldState = m_states.stateForKey(oldKey);
            if (oldState && oldState->containsWindow(windowId)) {
                // Use the algorithm's lifecycle hook for clean removal
                // (e.g., dwindle-memory needs to update its split tree).
                PhosphorTiles::TilingAlgorithm* oldAlgo = effectiveAlgorithm(oldKey.screenId);
                if (oldAlgo && oldAlgo->supportsLifecycleHooks()) {
                    const int idx = oldState->tiledWindows().indexOf(windowId);
                    if (idx >= 0) {
                        oldAlgo->onWindowRemoved(oldState, idx);
                    }
                }
                oldState->removeWindow(windowId);
                qCInfo(PhosphorTileEngine::lcTileEngine) << "windowOpened: removed" << windowId << "from old screen"
                                                         << oldKey.screenId << "before adding to" << screenId;
                scheduleRetileForScreen(oldKey.screenId);
            }
        }
        m_states.setKeyForWindow(windowId, newKey);
    }

    // Store window minimum size from KWin (used by enforceMinSizes)
    if (minWidth > 0 || minHeight > 0) {
        storeWindowMinSize(windowId, minWidth, minHeight);
    }

    onWindowAdded(windowId);
}

void AutotileEngine::windowMinSizeUpdated(const QString& rawWindowId, int minWidth, int minHeight)
{
    if (!warnIfEmptyWindowId(rawWindowId, "windowMinSizeUpdated")) {
        return;
    }
    const QString windowId = canonicalizeWindowId(rawWindowId);

    qCDebug(PhosphorTileEngine::lcTileEngine)
        << "windowMinSizeUpdated:" << windowId << "minSize=" << minWidth << "x" << minHeight;

    if (!storeWindowMinSize(windowId, minWidth, minHeight)) {
        return; // No change
    }

    // Retile the screen this window is on
    const auto stateKey = m_states.keyForWindow(windowId);
    const QString screenId = stateKey.screenId;
    if (!screenId.isEmpty() && m_states.containsKey(stateKey)) {
        scheduleRetileForScreen(screenId);
    }
}

bool AutotileEngine::storeWindowMinSize(const QString& rawWindowId, int minWidth, int minHeight)
{
    const QString windowId = canonicalizeWindowId(rawWindowId);
    // Cap min-sizes against the screen geometry to prevent a single window's
    // min-size from overwhelming the split ratio. Without this cap, a transiently
    // inflated min-size (e.g., from a browser loading media) can dominate the
    // master/stack split and get stuck at ~90% or full width.
    const auto stateKey = m_states.keyForWindow(windowId);
    const QString screenId = stateKey.screenId;
    if (!screenId.isEmpty()) {
        const QRect screen = screenGeometry(screenId);
        if (screen.isValid()) {
            const int maxMinW = static_cast<int>(screen.width() * PhosphorTiles::AutotileDefaults::MaxSplitRatio);
            const int maxMinH = static_cast<int>(screen.height() * PhosphorTiles::AutotileDefaults::MaxSplitRatio);
            minWidth = qMin(qMax(0, minWidth), maxMinW);
            minHeight = qMin(qMax(0, minHeight), maxMinH);
        }
    }

    const QSize newMin(qMax(0, minWidth), qMax(0, minHeight));
    const QSize oldMin = m_windowMinSizes.value(windowId, QSize(0, 0));

    if (newMin == oldMin) {
        return false; // No change
    }

    if (newMin.width() > 0 || newMin.height() > 0) {
        m_windowMinSizes[windowId] = newMin;
        qCInfo(PhosphorTileEngine::lcTileEngine)
            << "storeWindowMinSize:" << windowId << "min=" << newMin << "old=" << oldMin;
    } else {
        m_windowMinSizes.remove(windowId);
    }

    if (Q_UNLIKELY(PhosphorTileEngine::lcTileEngine().isDebugEnabled()) && !screenId.isEmpty()) {
        const QRect screen = screenGeometry(screenId);
        if (screen.isValid()) {
            qCDebug(PhosphorTileEngine::lcTileEngine)
                << "storeWindowMinSize: cap="
                << static_cast<int>(screen.width() * PhosphorTiles::AutotileDefaults::MaxSplitRatio) << "x"
                << static_cast<int>(screen.height() * PhosphorTiles::AutotileDefaults::MaxSplitRatio)
                << "screen=" << screen.size();
        }
    }
    return true;
}

void AutotileEngine::dropClosedWindowFromDragPreview(const QString& windowId)
{
    if (!m_dragInsertPreview) {
        return;
    }
    if (m_dragInsertPreview->windowId == windowId) {
        // Dragged window gone mid-preview — drop the preview entirely.
        // Cannot "restore" or "commit" a gone window; clear and move on.
        m_dragInsertPreview.reset();
    } else if (m_dragInsertPreview->evictedWindowId == windowId) {
        // Evicted neighbour gone mid-preview — forget the eviction so
        // commit/cancel don't try to operate on it.
        m_dragInsertPreview->evictedWindowId.clear();
    }
}

void AutotileEngine::windowClosed(const QString& rawWindowId)
{
    if (!warnIfEmptyWindowId(rawWindowId, "windowClosed")) {
        return;
    }
    const QString windowId = canonicalizeWindowId(rawWindowId);

    // Drag-insert preview bookkeeping: react before onWindowRemoved tears
    // down state. Leaving a stale reference would later drive setFloating or
    // windowsBatchFloated on a dead window id.
    dropClosedWindowFromDragPreview(windowId);

    m_autotileFloatedWindows.remove(windowId);
    // Min-size cleanup must not depend on tracking: a window released from
    // tracking (autotile toggle-off, orphaned VS) and later closed would hit
    // onWindowRemoved's empty-stored-key early return and keep its entry for
    // the session — a later re-entry reporting min 0x0 never clears it
    // (windowOpened only stores when minWidth/minHeight > 0), inflating
    // enforceMinSizes constraints with a stale value.
    m_windowMinSizes.remove(windowId);
    // m_lastAppliedTileRect is deliberately RETAINED here. The effect
    // notifies autotile of a close BEFORE WindowTracking (two fire-and-forget
    // calls on the same connection, delivered in order), so the orchestrator's
    // captureWindowPlacement — and its close-path tile-rect guard — runs
    // AFTER this teardown, on a live frame that is still the tile rect for a
    // window that closed tiled. Erasing now would blind that guard and let
    // the tile rect be recorded as the reopen float-back. pruneStaleWindows
    // reclaims the entry (its sweep is independent of tracking).

    onWindowRemoved(windowId);
    // Release the canonical translation last — downstream cleanup above may
    // still need to resolve lookups keyed by this window's instance id.
    cleanupCanonical(rawWindowId);
}

void AutotileEngine::windowFocused(const QString& rawWindowId, const QString& screenId)
{
    if (!warnIfEmptyWindowId(rawWindowId, "windowFocused")) {
        return;
    }
    const QString windowId = canonicalizeWindowId(rawWindowId);

    // Detect cross-screen moves. When a window's focus moves to a different
    // screen, migrate its PhosphorTiles::TilingState membership so m_states and the
    // PhosphorTiles::TilingState remain consistent. This handles both overflow-floated windows
    // and windows that were previously migrated (preventing the Screen1->2->1
    // rapid-migration desync where the second hop was silently skipped).
    //
    // Only update m_states for windows already tracked via windowOpened().
    // The KWin effect sends focus events for ALL handleable windows (including
    // transients and non-tileable windows that pass shouldHandleWindow but fail
    // isTileableWindow). Creating entries for these phantom windows causes
    // backfillWindows() to insert them on algorithm switches, inflating the
    // tiled window count.
    const auto trackedIt = m_states.windowKeys().constFind(windowId);
    const bool tracked = trackedIt != m_states.windowKeys().constEnd();
    const TilingStateKey oldKey = tracked ? trackedIt.value() : TilingStateKey{};
    const QString oldScreen = oldKey.screenId;
    if (!screenId.isEmpty() && tracked) {
        if (oldKey.screenId == screenId) {
            // SAME SCREEN: the window has NOT moved monitors, so any key delta
            // is a context-only delta — the current desktop/activity changed
            // underneath the window, not the window's location. This fires when
            // a focus/activation event for the previously-active window lands
            // during a desktop switch: KWin re-activates the last-focused window
            // (e.g. the active window when the user left the desktop), and that
            // focus arrives with the daemon's current desktop already advanced.
            //
            // isAutotileScreen() is evaluated against the CURRENT desktop, so it
            // reads FALSE here whenever the window's own desktop differs from the
            // one just switched to — even though the screen IS autotile on the
            // window's real desktop. The old code took that false reading as
            // "window moved to a non-autotile screen" and removed it from its
            // (still-live, off-desktop) TilingState, silently dropping the window
            // from that desktop's tiling so the survivors reflowed on return.
            //
            // Never migrate or remove on a same-screen focus. Defer one event
            // loop pass: revalidateWindowContext migrates ONLY if a real
            // desktop/activity move persists after the in-flight context push
            // settles, and leaves the window untouched in its owning state when
            // the screen is still non-autotile then (the window genuinely
            // belongs to another desktop — windowDesktopsChanged owns real
            // desktop moves). The map is left untouched here so the catch-scan's
            // windowOpened ghost-removal still detects a genuine move meanwhile.
            const bool alreadyCorrect = isAutotileScreen(screenId) && currentKeyForScreen(screenId) == oldKey;
            if (!alreadyCorrect) {
                QMetaObject::invokeMethod(
                    this,
                    [this, windowId, screenId]() {
                        revalidateWindowContext(windowId, screenId);
                    },
                    Qt::QueuedConnection);
            }
        } else if (isAutotileScreen(screenId)) {
            // Genuine cross-screen move to an autotile screen: migrate now. Move
            // the reverse-map entry via the shared primitive, then run autotile's
            // own state-lifecycle migration around it.
            m_states.migrate(windowId, oldKey, currentKeyForScreen(screenId));
            migrateWindowBetweenKeys(windowId, oldKey, screenId);
        } else {
            // Genuine cross-screen move to a non-autotile screen — remove
            // tracking entirely. Leaving a stale entry pointing at a snap screen
            // causes phantom lookups and prevents clean re-entry if the window
            // returns. Drop the per-window caches too: removeWindow()/
            // windowClosed() clear these on their paths, and a lingering
            // autotile-floated marker would keep feeding the daemon's mode-flip
            // logic while a stored min-size would survive a later re-entry stale.
            m_states.removeWindow(windowId);
            m_windowMinSizes.remove(windowId);
            m_autotileFloatedWindows.remove(windowId);
            m_lastAppliedTileRect.remove(windowId);
            if (!oldScreen.isEmpty()) {
                migrateWindowBetweenKeys(windowId, oldKey, screenId);
            }
        }
    }

    onWindowFocused(windowId);
}

void AutotileEngine::releaseScreenStateForTeardown(const QString& screenId, PhosphorTiles::TilingState* state,
                                                   QStringList& releasedWindows, bool drainOverflow)
{
    // Snapshot each window's autotile slot into the unified record BEFORE the
    // PhosphorTiles::TilingState is torn down — the record is the SINGLE
    // source of truth for cross-mode state (no parallel saved-floating set).
    // capturePlacement records a USER float as floating and a tiled/overflow
    // window as tiled (so it re-tiles on re-entry); the shared free geometry
    // rides from the engine's own float-back cache.
    const QStringList tiled = state->tiledWindows();
    const QStringList floated = state->floatingWindows();
    if (m_windowTracker) {
        // Two passes instead of `floated + tiled` — this runs once per
        // context in the orphaned-VS teardown loop and the concatenation
        // would allocate a temporary list each time.
        const auto captureAll = [this](const QStringList& wids) {
            for (const QString& wid : wids) {
                auto rec = capturePlacement(wid);
                if (!rec) {
                    continue;
                }
                m_windowTracker->placementStore().record(*rec);
            }
        };
        captureAll(floated);
        captureAll(tiled);
    }
    // Drop the overflow set AFTER capture: capturePlacement's
    // overflow-vs-user-float discriminator (isOverflow) must still see this
    // screen's overflow windows, or they'd be mis-recorded as user floats and
    // stick floating instead of re-tiling on re-entry. Callers tearing down
    // SEVERAL states for the same screenId (the orphaned-VS loop spans every
    // desktop/activity context) pass drainOverflow=false and drain once per
    // screen AFTER all captures — the overflow bucket is keyed per screenId
    // only, so an in-helper drain on the first state would blind the
    // discriminator for every later state of the same screen.
    if (drainOverflow) {
        m_overflow.takeForScreen(screenId);
    }
    releasedWindows.append(tiled);
    releasedWindows.append(floated);
    m_pendingInitialOrders.remove(screenId);
    m_pendingOrderGeneration.remove(screenId);
    m_strictInitialOrderScreens.remove(screenId);
    state->deleteLater();
}

void AutotileEngine::migrateWindowBetweenKeys(const QString& windowId, const TilingStateKey& oldKey,
                                              const QString& newScreenId)
{
    PhosphorTiles::TilingState* oldState = m_states.stateForKey(oldKey);
    if (!oldState || !oldState->containsWindow(windowId)) {
        return;
    }
    // Capture the float state BEFORE the source drops the window: the re-add
    // below must carry it across (see MigrationArrival). Reading it after the
    // removal is impossible — no state holds the window in between.
    const bool wasFloating = oldState->isFloating(windowId);
    // Use the algorithm's lifecycle hook for clean removal (e.g.
    // dwindle-memory updates its split tree) — mirrors windowOpened's
    // migration path.
    PhosphorTiles::TilingAlgorithm* oldAlgo = effectiveAlgorithm(oldKey.screenId);
    if (oldAlgo && oldAlgo->supportsLifecycleHooks()) {
        const int idx = oldState->tiledWindows().indexOf(windowId);
        if (idx >= 0) {
            oldAlgo->onWindowRemoved(oldState, idx);
        }
    }
    oldState->removeWindow(windowId);
    m_overflow.migrateWindow(windowId);
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Window" << windowId << "moved from" << oldKey.screenId << "to" << newScreenId << "- migrating";
    // Close the hole the departing window left on the SOURCE screen — the
    // destination's own insert schedules a retile there, but nothing else
    // retiles the source (mirrors windowOpened's migration path).
    scheduleRetileForScreen(oldKey.screenId);
    if (isAutotileScreen(newScreenId)) {
        // Re-add to the new screen's normal flow (will be overflow-checked
        // on next retile). Mark the re-add as a migration ARRIVAL for the
        // duration of this synchronous call so insertWindow carries the
        // window's live float state across instead of re-deriving it from the
        // open-time float rule. Scope-guarded: the marker describes this one
        // re-add and must not leak into a later open, and onWindowAdded runs the
        // daemon-injected float predicate and emits into daemon slots inside the
        // window.
        QScopeGuard clearArrival([this] {
            m_migrationArrival.reset();
        });
        m_migrationArrival = MigrationArrival{windowId, wasFloating};
        onWindowAdded(windowId);
    }
    // Re-adding on a non-autotile destination would route through
    // screenForWindow()'s primary-screen fallback and re-tile a window that
    // just left autotile — the cross-engine misroute class the tracking
    // removal exists to prevent.
}

void AutotileEngine::revalidateWindowContext(const QString& windowId, const QString& screenId)
{
    // Deferred half of windowFocused's context-only key-delta handling. By
    // now any context push that was in flight when the focus event arrived
    // has been processed, so a persisting mismatch means the window REALLY
    // moved desktop/activity (the catch-scan race the full-key migration
    // exists for), not that the focus outran the push.
    auto it = m_states.windowKeys().constFind(windowId);
    if (it == m_states.windowKeys().constEnd() || !isAutotileScreen(screenId)) {
        return; // closed / untracked / screen left autotile meanwhile
    }
    const TilingStateKey oldKey = it.value();
    if (oldKey.screenId != screenId) {
        return; // a genuine cross-screen event superseded this re-check
    }
    const TilingStateKey newKey = currentKeyForScreen(screenId);
    if (newKey == oldKey) {
        return; // the context push arrived — nothing actually moved
    }
    // Move the reverse-map entry via the shared primitive, then run autotile's
    // own state-lifecycle migration (remove-from-old + retile) around it.
    m_states.migrate(windowId, oldKey, newKey);
    migrateWindowBetweenKeys(windowId, oldKey, screenId);
    // Re-record focus on the DESTINATION state: the original focus event
    // ran onWindowFocused against the old key, and the migration's
    // removeWindow just cleared that marker — without this, the window the
    // user is actively focused on stays unmarked in its owning state until
    // the next activation (mirrors the pre-deferral ordering, harmless
    // no-op when nothing relies on it).
    onWindowFocused(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private slot event handlers
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::onWindowAdded(const QString& windowId)
{
    const QString screenId = screenForWindow(windowId);
    // Computed before the first gate: BOTH gates below need it, and hoisting it
    // also keeps the daemon-injected float predicate out of an arrival's path
    // entirely (see ruleWillFloat) rather than running it inside the marker's
    // window on a path the marker says it has no say over.
    const bool isMigrationArrival = m_migrationArrival && m_migrationArrival->windowId == windowId;
    // The two disjuncts are NOT symmetric for an arrival, so they are not
    // exempted together:
    //
    // isAutotileScreen stays unconditional. It asks whether the DESTINATION can
    // hold a tile at all, which an arrival can never make true by itself — and
    // migrateWindowBetweenKeys only re-adds under its own isAutotileScreen check
    // on the same screen id (the reverse map is re-pointed at the destination
    // before the re-add, so screenForWindow returns it here), so an arrival
    // cannot reach this line with a non-autotile screen. Exempting it would buy
    // nothing and would drop the guard against ever tiling onto a snap screen.
    //
    // shouldTileWindow IS exempted for an arrival, for the reason the cap gate
    // below is: the source state has already dropped the window, so refusing here
    // strands it — removed from the source, keyed to the destination, present in
    // neither, and isWindowTiled() then reports a phantom tile forever. An
    // arrival's tileability was already settled on the source (a sticky window
    // tiled there under TreatAsNormal, or before the user made it sticky, stays
    // tiled here), its float half is carried across by insertShouldFloat, and
    // applyTiling's overflow pass floats out whatever the destination cannot
    // hold. Screens that must turn an arrival away have to refuse BEFORE the
    // removal, which is what NavigationController::crossOutputMove's
    // pre-mutation shouldTileWindow check does on the proactive path.
    if (!isAutotileScreen(screenId) || (!isMigrationArrival && !shouldTileWindow(windowId))) {
        qCDebug(PhosphorTileEngine::lcTileEngine) << "onWindowAdded: skipping" << windowId << "screen=" << screenId
                                                  << "isAutotile=" << isAutotileScreen(screenId);
        return;
    }

    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    const int maxWin = effectiveMaxWindows(screenId);
    // A window OPENING under a matched "Float this app" rule must bypass the
    // tiled-window cap: it opens floating and so consumes no tile slot
    // (tiledWindowCount excludes floats), and insertWindow marks it floating once
    // inserted. Dropping it here would leave it untracked — neither floating in
    // autotile (so the IsFloating match field stays false) nor re-tileable via
    // Meta+F.
    //
    // A migration ARRIVAL bypasses the cap outright, whatever float state it
    // carries across (see insertShouldFloat). By the time it reaches here its
    // source state has already dropped it, so refusing would strand it: removed
    // from the source, keyed to the destination, and present in neither state —
    // isWindowTiled() would then report a phantom tile forever. Accepting is
    // safe because applyTiling's overflow pass floats everything past the cap
    // back out. Screens that must turn a full destination away have to refuse
    // BEFORE the removal, which is what the proactive bare-cap guard in
    // NavigationController::crossOutputMove does.
    //
    // The predicate is skipped outright for an arrival: it is dead there (the
    // arrival exemption already carries the gate) and it is a daemon-injected
    // callback, so not running it keeps an arrival's marker window free of
    // foreign code — matching insertShouldFloat, which short-circuits it for the
    // same reason.
    const bool ruleWillFloat = !isMigrationArrival && m_floatPredicate && m_floatPredicate(windowId);
    if (state && state->tiledWindowCount() >= maxWin && !ruleWillFloat && !isMigrationArrival) {
        qCDebug(PhosphorTileEngine::lcTileEngine)
            << "Max window limit reached for screen" << screenId << "(max=" << maxWin << ")";
        // Purge this window from pending initial orders so the order doesn't
        // leak waiting for a window that will never be inserted.
        for (auto pit = m_pendingInitialOrders.begin(); pit != m_pendingInitialOrders.end(); ++pit) {
            pit.value().removeAll(windowId);
        }
        return;
    }

    const bool inserted = insertWindow(windowId, screenId);

    if (inserted) {
        emitInsertFloatStateSync(windowId, screenId);
    }

    if (inserted && m_config && m_config->focusNewWindows) {
        // Defer focus until after applyTiling emits windowsTiled. The KWin effect's
        // onComplete raises windows in tiling order; emitting focus before retile
        // causes the raise loop to bury the new window behind existing ones.
        m_pendingFocusByScreen.insert(screenId, windowId);
    }

    // Replay a focus notification that arrived before this window was tracked (see
    // m_pendingFocusReseedWindowId). Seed the focus state directly rather than
    // re-entering onWindowFocused: the scheduleRetileForScreen() below already
    // reflows the screen, and a focus-driven layout (Theater) reads focusedIndex
    // from focusedWindow() at that retile — so seeding here lands the spotlight on
    // the pre-restart active window without a second, competing retile.
    if (inserted && windowId == m_pendingFocusReseedWindowId) {
        m_pendingFocusReseedWindowId.clear();
        m_activeScreen = screenId;
        if (state) {
            state->setFocusedWindow(windowId);
        }
    }

    if (inserted) {
        // Notify algorithm via lifecycle hook before retile
        PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(screenId);
        if (algo && algo->supportsLifecycleHooks() && state) {
            const int idx = state->tiledWindows().indexOf(windowId);
            if (idx >= 0) {
                algo->onWindowAdded(state, idx);
            }
        }
        scheduleRetileForScreen(screenId);
    }
}

QString AutotileEngine::removeTrackedWindowNoRetile(const QString& windowId)
{
    const QString screenId = m_states.keyForWindow(windowId).screenId;
    if (screenId.isEmpty()) {
        return {};
    }

    // Notify algorithm via lifecycle hook before removal. Resolve the state
    // through the window's STORED key (mirrors handoffRelease /
    // migrateWindowBetweenKeys), not tilingStateForScreen(): the latter keys
    // on the CURRENT desktop/activity — for a window owned by another
    // context's state it would miss the hook on the owning state AND lazily
    // create a spurious empty TilingState for the current context.
    PhosphorTiles::TilingState* state = m_states.stateForKey(m_states.keyForWindow(windowId));
    PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(screenId);
    if (algo && algo->supportsLifecycleHooks() && state) {
        const int idx = state->tiledWindows().indexOf(windowId);
        if (idx >= 0) {
            algo->onWindowRemoved(state, idx);
        } else {
            qCDebug(PhosphorTileEngine::lcTileEngine)
                << "removeTrackedWindow: window" << windowId << "not found in tiling state — lifecycle hook skipped";
        }
    }

    removeWindow(windowId);
    return screenId;
}

void AutotileEngine::onWindowRemoved(const QString& windowId)
{
    const QString screenId = removeTrackedWindowNoRetile(windowId);
    if (screenId.isEmpty()) {
        return;
    }
    qCInfo(PhosphorTileEngine::lcTileEngine) << "onWindowRemoved:" << windowId << "screen=" << screenId;
    // Retile immediately (not deferred like onWindowAdded). Removals need instant
    // layout recalculation to avoid visible holes. Unlike additions, removals don't
    // arrive in bursts, so coalescing provides no benefit. (The batch prune path
    // in pruneStaleWindows is the exception — it retiles each affected screen once.)
    retileAfterOperation(screenId, true);
}

void AutotileEngine::onWindowFocused(const QString& windowId)
{
    PhosphorTiles::TilingState* state = stateForWindow(windowId);
    if (!state) {
        // Not an error — non-autotiled windows (dialogs, floating, etc.) report
        // focus changes too, so this is the normal case for most window activations.
        // Stash the id so that if this window is a tiled window whose tracking just
        // hasn't landed yet (daemon-restart re-announce ordering: the effect
        // re-notifies the active window during bring-up, before windowsOpenedBatch),
        // onWindowAdded can replay the focus once the window is tracked. A stash for a
        // window that never gets tiled (a genuine dialog) is inert — it only replays
        // on an exact-id add — and is overwritten by the next such notification.
        qCDebug(PhosphorTileEngine::lcTileEngine)
            << "onWindowFocused: window not tracked, stashing for reseed" << windowId;
        m_pendingFocusReseedWindowId = windowId;
        return;
    }

    // A tracked window took focus through the normal path — any earlier stash for a
    // yet-untracked window is stale now, so drop it rather than let it replay later.
    m_pendingFocusReseedWindowId.clear();

    // Track which screen has the active focus (used by tiledWindowsForFocusedScreen
    // to avoid non-deterministic QHash iteration when multiple screens have focused windows)
    const TilingStateKey windowKey = m_states.keyForWindow(windowId);
    m_activeScreen = windowKey.screenId;

    const QString previousFocus = state->focusedWindow();
    state->setFocusedWindow(windowId);

    // Most layouts place a window by its tiled index, so a focus change moves
    // nothing and there is no reason to recompute. Focus-driven layouts (e.g.
    // Theater) opt in via retilesOnFocusChange(): reflow when focus actually
    // moves to a different tiled window so the layout can follow it. The checks
    // are ordered cheap-first: the capability bool short-circuits before the
    // allocating tiledWindows() scan, which would otherwise run on every focus
    // change for every layout. Reflow only when the focused window is on this
    // screen's current context (retileAfterOperation keys on m_activeScreen's
    // current state, so an off-context focus event must not reflow a different
    // desktop's state) and only when the target is actually tiled (focusing a
    // floating window must not disturb the layout).
    if (previousFocus != windowId) {
        PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(m_activeScreen);
        if (algo && algo->retilesOnFocusChange() && windowKey == currentKeyForScreen(m_activeScreen)
            && state->tiledWindows().contains(windowId)) {
            retileAfterOperation(m_activeScreen, true);
        }
    }
}

} // namespace PhosphorTileEngine
