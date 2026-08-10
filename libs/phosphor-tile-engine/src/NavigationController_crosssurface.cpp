// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Cross-surface navigation for the autotile engine: entry-window resolution
// for arriving crossings, plus the cross-output and cross-desktop focus and
// move probes. Split from NavigationController.cpp by concern (the seam the
// scroll engine keeps in engine_navigation.cpp) when the in-surface verbs
// pushed that file past the size ceiling.

#include <PhosphorTileEngine/NavigationController.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorGeometry/DirectionalNeighbor.h>
#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>

namespace PhosphorTileEngine {

QString NavigationController::entryWindowOnScreen(const QString& screenId, const QString& direction) const
{
    // Non-creating lookup: a miss must not persist an empty state.
    PhosphorTiles::TilingState* state = m_engine->m_states.stateForKey(m_engine->currentKeyForScreen(screenId));
    if (!state) {
        return QString();
    }
    const QStringList windows = state->tiledWindows();
    if (windows.isEmpty()) {
        return QString();
    }
    const QVector<QRect> zones = state->calculatedZones();
    if (zones.size() != windows.size()) {
        // Geometry not computed — first tiled window (the master) is the best
        // available entry approximation.
        return windows.first();
    }
    // The entry edge faces back toward the source: a crossing moving "right"
    // enters the target's LEFT edge, "down" its TOP, etc. Pick the extreme tile
    // on that edge via the shared primitive (edge-coordinate ranked, same pick
    // snap's first-zone-in-direction uses). An unrecognised direction is
    // rejected rather than guessed at.
    const auto travel = PhosphorGeometry::directionFromString(direction);
    if (!travel.has_value()) {
        return QString();
    }
    QList<QRectF> candidates;
    candidates.reserve(zones.size());
    for (const QRect& r : zones) {
        candidates.append(QRectF(r));
    }
    const int best = PhosphorGeometry::edgeMostRect(candidates, PhosphorGeometry::opposite(*travel));
    return best >= 0 ? windows.at(best) : QString();
}

int NavigationController::windowOrderIndexOnScreen(const QString& screenId, const QString& windowId) const
{
    PhosphorTiles::TilingState* state = m_engine->m_states.stateForKey(m_engine->currentKeyForScreen(screenId));
    if (!state) {
        return -1;
    }
    // Raw window-order index (NOT the tiled-only index): TilingState::addWindow —
    // the consumer of HandoffContext.insertIndex — inserts into windowOrder(),
    // which counts floating windows too. Returning the tiled index here would land
    // a cross-mode-swap arrival too far forward by the count of preceding floats.
    return state->windowOrder().indexOf(windowId);
}

QString NavigationController::crossOutputFocusTarget(const QString& sourceScreenId, const QString& focused,
                                                     const QString& direction) const
{
    if (!m_engine->m_crossSurfaceResolver) {
        return QString();
    }
    const QString neighbor = m_engine->m_crossSurfaceResolver->neighborOutputInDirection(sourceScreenId, direction);
    if (neighbor.isEmpty()) {
        return QString();
    }
    // Non-creating lookup: this is a const read-only focus probe. tilingStateForScreen
    // would CREATE and persist an empty TilingState for the neighbour on a miss,
    // leaking a state on every directional-focus keypress at a layout edge with
    // no neighbour window.
    PhosphorTiles::TilingState* neighborState = m_engine->m_states.stateForKey(m_engine->currentKeyForScreen(neighbor));
    if (!neighborState) {
        return QString();
    }
    const QStringList neighborWindows = neighborState->tiledWindows();
    if (neighborWindows.isEmpty()) {
        return QString();
    }

    // Entry window: the neighbour-output window nearest the crossing edge. The
    // neighbour output lies entirely in `direction` from the source, so every
    // one of its windows is a directional candidate of the focused window's
    // rect (global coordinates) — directionalNeighbor picks the closest with
    // perpendicular overlap. Fall back to the first tiled window when geometry
    // is unavailable.
    // Non-creating lookup, like the neighbour probe above: this is a read-only
    // focus probe. The geometry-edge caller reaches here with a live source
    // state (hasGeometry was true); the EMPTY-surface caller arrives with no
    // source state and an empty focused id. rectForWindowInState null-guards
    // its argument and an empty id never matches, so both shapes degrade to
    // the same first-tiled-window fallback without persisting an empty state.
    const QRect focusRect =
        rectForWindowInState(m_engine->m_states.stateForKey(m_engine->currentKeyForScreen(sourceScreenId)), focused);
    const auto dir = PhosphorGeometry::directionFromString(direction);
    const QVector<QRect> neighborZones = neighborState->calculatedZones();
    if (dir.has_value() && focusRect.isValid() && neighborZones.size() == neighborWindows.size()) {
        QList<QRectF> candidates;
        candidates.reserve(neighborZones.size());
        for (const QRect& zone : neighborZones) {
            candidates.append(QRectF(zone));
        }
        const int pick = PhosphorGeometry::directionalNeighbor(QRectF(focusRect), candidates, *dir);
        if (pick >= 0) {
            return neighborWindows.at(pick);
        }
    }
    return neighborWindows.first();
}

bool NavigationController::crossOutputMove(const QString& sourceScreenId, const QString& focused,
                                           const QString& direction, const QString& action)
{
    if (!m_engine->m_crossSurfaceResolver) {
        return false;
    }
    const QString neighbor = m_engine->m_crossSurfaceResolver->neighborOutputInDirection(sourceScreenId, direction);
    if (neighbor.isEmpty()) {
        return false;
    }
    // The resolver returns ANY connected output in the direction — it has no
    // autotile knowledge, and even an autotile destination can be full.
    // migrateWindowBetweenKeys removes the window from the source state and only
    // re-adds it when onWindowAdded accepts it there: the neighbour must be an
    // autotile screen, the window must tile on it, and the destination must be
    // under its maxWindows cap. onWindowAdded rejects WITHOUT inserting
    // otherwise. Committing the
    // move toward a destination that won't accept the window would remove it
    // from the source, re-key it to a screen with no TilingState, and strand it
    // (tracked nowhere) — the exact failure the cross-desktop path was rewritten
    // to avoid. Refuse here, BEFORE any state mutation or the
    // windowOutputMoveExpected marker (a marker emitted for a move that never
    // happens would arm a one-shot that swallows the next genuine outputChanged
    // for this window), so swapFocusedInDirection falls through to cross-desktop
    // / no_neighbor instead.
    if (!m_engine->isAutotileScreen(neighbor)) {
        // The neighbour output is a DIFFERENT tiling mode (snap). Autotile has no
        // state there, so defer to the daemon: it relinquishes the window from
        // this engine and hands it to the snap engine. A "swap" trades places with
        // the entry zone's occupant (two-way); a "move" inserts one-way into the
        // entry zone. The daemon slot is a direct (synchronous) connection, so the
        // handoff completes before this returns. Same-desktop monitor crossing →
        // targetDesktop 0 (current).
        if (action == QLatin1String("swap")) {
            Q_EMIT m_engine->crossModeSwapRequested(focused, neighbor, 0, direction);
        } else {
            Q_EMIT m_engine->crossModeMoveRequested(focused, neighbor, 0, direction);
        }
        return true;
    }
    // Autotile → autotile output crossing is ALWAYS a one-way move: there is no
    // entry-zone partner to trade back, so @p action ("swap" vs "move") does not
    // apply here (the daemon's cross-mode swap machinery exists only for the
    // different-mode snap neighbour above). Same-mode swap-across-outputs is not a
    // supported gesture; a "swap" toward another autotile output relocates the
    // window without returning a partner.
    const PhosphorEngine::TilingStateKey oldKey = m_engine->currentKeyForScreen(sourceScreenId);
    const PhosphorEngine::TilingStateKey newKey = m_engine->currentKeyForScreen(neighbor);
    // The BARE cap applies here, with no float-rule exemption. Every window that
    // reaches this point is TILED: the shouldTileWindow gate below rejects a
    // floating one, and swapFocusedInDirection only routes here after
    // directionalNeighborWindow reported geometry, which it does only for a
    // window present in the state's tiledWindows(). A tiled window occupies a
    // tile slot on the destination — it migrates tiled (insertWindow carries its
    // live float state across rather than re-running the open-time "Float this
    // app" rule) — so a full destination genuinely cannot take it. Exempting a
    // float-ruled window here, as onWindowAdded does for an OPENING window,
    // would apply open-time semantics to a live migration: the rule stays matched
    // for a window the user has since tiled with Meta+F, so the exemption would
    // wave a tiled window onto a full output. Refuse instead — swapFocusedInDirection
    // falls through to cross-desktop / no_neighbor and reports the refusal.
    if (const PhosphorTiles::TilingState* destState = m_engine->m_states.stateForKey(newKey);
        destState && destState->tiledWindowCount() >= m_engine->effectiveMaxWindows(neighbor)) {
        return false;
    }
    // The other reason onWindowAdded rejects a re-add (see its
    // isAutotileScreen || shouldTileWindow gate): a window that would not tile on
    // the destination (floating / excluded / invalid geometry). Migrating it would
    // remove it from the source and strand it — the same stranding the cap guard
    // above prevents. Refuse before any state mutation or the marker emit.
    if (!m_engine->shouldTileWindow(focused)) {
        return false;
    }
    // Re-point the window's state-key BEFORE migrating, exactly as the reactive
    // windowFocused() path does: migrateWindowBetweenKeys re-adds the window via
    // onWindowAdded() → screenForWindow(), which reads this map. Without the
    // update it would resolve back to the source screen and re-add it there.
    m_engine->m_states.setKeyForWindow(focused, newKey);
    // migrateWindowBetweenKeys removes the window from the source state (with
    // its onWindowRemoved lifecycle) and adds it on the neighbour output. It
    // schedules DEFERRED retiles for both — but those can be raced by the
    // reactive screen-change event the move triggers (observed on real
    // hardware: the source monitor failed to reflow). Retile both surfaces
    // SYNCHRONOUSLY here, exactly as the in-surface swap does, so the source's
    // reflow and the destination's placement reach the compositor within this
    // handler, before activateWindowRequested.
    // Tell the compositor the imminent physical output change for this window
    // is daemon-owned: this migration plus the two reflows below ARE the move.
    // Without this, the effect's reactive outputChanged handler re-issues
    // windowClosed/windowOpened, which (the map already points at the
    // destination) tears down this placement and strands the source's reflow.
    // Emit BEFORE the retiles so the marker is recorded ahead of the
    // tile-request apply that triggers outputChanged.
    Q_EMIT m_engine->windowOutputMoveExpected(focused, neighbor);
    m_engine->migrateWindowBetweenKeys(focused, oldKey, neighbor);
    m_engine->m_activeScreen = neighbor;
    m_engine->retileAfterOperation(sourceScreenId, true);
    m_engine->retileAfterOperation(neighbor, true);
    Q_EMIT m_engine->activateWindowRequested(focused);
    return true;
}

QString NavigationController::crossDesktopFocusTarget(const QString& sourceScreenId, const QString& direction) const
{
    if (!m_engine->m_crossSurfaceResolver) {
        return QString();
    }
    // Base the neighbour-desktop arithmetic on the source screen's EFFECTIVE
    // desktop, not the global current desktop: a screen sticky-pinned by the
    // "virtualdesktopsonlyonprimary" model (a per-output desktop pin in m_context)
    // shows — and its TilingState is keyed on — its pinned desktop, which
    // currentKeyForScreen resolves. For unpinned screens this is identical to
    // m_context's global desktop.
    const int baseDesktop = m_engine->currentKeyForScreen(sourceScreenId).desktop;
    const int targetDesktop = m_engine->m_crossSurfaceResolver->neighborDesktopInDirection(baseDesktop, direction);
    if (targetDesktop <= 0) {
        return QString();
    }
    const PhosphorEngine::TilingStateKey targetKey{sourceScreenId, targetDesktop,
                                                   m_engine->m_context.currentActivity()};
    // Non-creating lookup on purpose: a find-or-create would CREATE and persist
    // an empty TilingState for the target desktop on a miss, leaking a state on
    // every cross-desktop focus probe to a desktop with no tiled windows.
    PhosphorTiles::TilingState* targetState = m_engine->m_states.stateForKey(targetKey);
    if (!targetState) {
        return QString();
    }
    const QStringList targetWindows = targetState->tiledWindows();
    if (targetWindows.isEmpty()) {
        return QString();
    }
    // Desktops occupy the same physical space, so direction doesn't map to a
    // geometric edge across them — enter at the order extreme: first tiled
    // window stepping forward (right/down), last stepping backward.
    const bool forward = isForwardDirection(direction);
    return forward ? targetWindows.first() : targetWindows.last();
}

bool NavigationController::crossDesktopMove(const QString& sourceScreenId, const QString& focused,
                                            const QString& direction)
{
    if (!m_engine->m_crossSurfaceResolver) {
        return false;
    }
    // Base on the source screen's effective desktop (sticky-pin aware), exactly
    // as crossDesktopFocusTarget does — for unpinned screens this equals
    // m_context's global desktop.
    const int baseDesktop = m_engine->currentKeyForScreen(sourceScreenId).desktop;
    const int targetDesktop = m_engine->m_crossSurfaceResolver->neighborDesktopInDirection(baseDesktop, direction);
    if (targetDesktop <= 0) {
        return false;
    }
    // If the target desktop on this screen is a DIFFERENT mode (snapping or
    // scrolling), autotile has no state there — defer to the daemon cross-mode
    // handoff, which snaps the window into the equivalent zone on a snap desktop
    // or inserts it into the strip on a scrolling one. The daemon slot is a
    // direct (synchronous) connection.
    //
    // The question is "is the target NOT autotile", not "is it snapping": a
    // scrolling target desktop would otherwise fall into the same-mode branch
    // below and get a bare compositor desktop move onto a scroll-owned desktop
    // with no handoff, leaving the arrival to autotile's catch-scan. This is the
    // mirror image of the snap engine's cross-desktop gate.
    //
    // A NULL registry deliberately falls through to the same-mode branch:
    // an embedder that wires no LayoutRegistry has no per-desktop modes at
    // all, so every desktop is autotile by construction and the bare
    // compositor move is correct (pinned by
    // crossDesktop_moveRight_relocatesToNextDesktopAndRequestsKWinMove).
    // The shipped daemon always wires the registry, so the foreign-mode
    // fail-open this could otherwise cause is unreachable there.
    if (m_engine->m_layoutManager
        && m_engine->m_layoutManager->modeForScreen(sourceScreenId, targetDesktop,
                                                    m_engine->m_context.currentActivity())
            != PhosphorZones::AssignmentEntry::Autotile) {
        // Only a MOVE reaches here (swap doesn't cross desktops), so this is
        // always the one-way cross-mode move into the equivalent snap zone.
        Q_EMIT m_engine->crossModeMoveRequested(focused, sourceScreenId, targetDesktop, direction);
        return true;
    }
    // Same-mode (autotile) target desktop: move the window the way a NATIVE KWin
    // desktop move works: just ask the
    // compositor to move it to the target desktop, then let the existing
    // reactive machinery do the rest. When the window leaves the current
    // desktop the effect fires "moved off current desktop" → windowClosed,
    // which removes it from the source autotile state and reflows the source;
    // when the user switches to the target desktop the effect fires
    // windowOpened, which tiles it there.
    //
    // Do NOT touch the source/target TilingStates here. The previous version
    // added the window to the target state and re-pointed m_states at
    // it — but the effect's windowClosed then removed it from that very state,
    // leaving the window tracked NOWHERE: stuck decoration, broken tiling. The
    // compositor is the single source of truth for which desktop a window is on.
    Q_EMIT m_engine->windowDesktopMoveRequested(focused, targetDesktop);
    return true;
}

} // namespace PhosphorTileEngine
