// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTileEngine/NavigationController.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorTiles/AutotileConstants.h>
#include "tileenginelogging.h"
#include <PhosphorGeometry/DirectionalNeighbor.h>
#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>

#include <algorithm>

namespace PhosphorTileEngine {

namespace PerScreenKeys = PhosphorEngine::PerScreenKeys;

namespace {
/// "Forward" in cycle/entry-extreme terms: right and down step toward the end
/// of the order, left and up toward the start. Shared by the order-based
/// cycling fallbacks and the cross-desktop entry-extreme selection.
bool isForwardDirection(const QString& direction)
{
    return direction == QLatin1String("right") || direction == QLatin1String("down");
}
} // namespace

NavigationController::NavigationController(AutotileEngine* engine)
    : m_engine(engine)
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// Focus/window cycling
// ═══════════════════════════════════════════════════════════════════════════════

void NavigationController::focusNext()
{
    emitFocusRequestAtIndex(1);
}

void NavigationController::focusPrevious()
{
    emitFocusRequestAtIndex(-1);
}

void NavigationController::focusMaster()
{
    QString screenId;
    PhosphorTiles::TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state);
    if (windows.isEmpty()) {
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("focus_master"), QStringLiteral("no_windows"),
                                            QString(), QString(), screenId);
        return;
    }
    emitFocusRequestAtIndex(0, true);
    Q_EMIT m_engine->navigationFeedback(true, QStringLiteral("focus_master"), QStringLiteral("master"), QString(),
                                        QString(), screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window swapping & rotation
// ═══════════════════════════════════════════════════════════════════════════════

void NavigationController::swapFocusedWithMaster()
{
    QString screenId;
    PhosphorTiles::TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state);

    if (windows.isEmpty() || !state) {
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("swap_master"), QStringLiteral("no_windows"),
                                            QString(), QString(), screenId);
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("swap_master"), QStringLiteral("no_focus"), QString(),
                                            QString(), screenId);
        return;
    }

    const bool promoted = state->moveToTiledPosition(focused, 0);
    m_engine->retileAfterOperation(screenId, promoted);

    if (promoted) {
        Q_EMIT m_engine->navigationFeedback(true, QStringLiteral("swap_master"), QStringLiteral("master"), QString(),
                                            QString(), screenId);
    } else {
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("swap_master"), QStringLiteral("already_master"),
                                            QString(), QString(), screenId);
    }
}

void NavigationController::rotateWindowOrder(bool clockwise)
{
    QString screenId;
    PhosphorTiles::TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state);

    if (windows.size() < 2 || !state) {
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("nothing_to_rotate"),
                                            QString(), QString(), screenId);
        return; // Nothing to rotate with 0 or 1 window
    }

    // Rotate the window order
    bool rotated = state->rotateWindows(clockwise);
    m_engine->retileAfterOperation(screenId, rotated);

    if (rotated) {
        QString reason = QStringLiteral("%1:%2")
                             .arg(clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise"))
                             .arg(windows.size());
        Q_EMIT m_engine->navigationFeedback(true, QStringLiteral("rotate"), reason, QString(), QString(), screenId);
    } else {
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_rotations"), QString(),
                                            QString(), screenId);
    }

    qCInfo(PhosphorTileEngine::lcTileEngine) << "Rotated windows" << (clockwise ? "clockwise" : "counterclockwise");
}

QString NavigationController::directionalNeighborWindow(PhosphorTiles::TilingState* state, const QStringList& windows,
                                                        const QString& focused, const QString& direction,
                                                        bool& outHasGeometry) const
{
    outHasGeometry = false;

    const auto dir = PhosphorGeometry::directionFromString(direction);
    if (!dir.has_value() || !state || windows.isEmpty()) {
        return QString();
    }

    // calculatedZones() is index-aligned with tiledWindows(). When the layout
    // has not been computed yet (e.g. a headless engine with no screen
    // geometry) the vectors won't match — report "no geometry" so the caller
    // falls back to order-based cycling rather than mis-selecting.
    const QVector<QRect> zones = state->calculatedZones();
    if (zones.size() != windows.size()) {
        return QString();
    }

    const int focusIdx = windows.indexOf(focused);
    if (focusIdx < 0) {
        return QString();
    }
    const QRect focusRect = zones.at(focusIdx);
    if (!focusRect.isValid()) {
        return QString();
    }
    outHasGeometry = true;

    // Candidate rects for every tiled window except the focused one, with a
    // parallel map back into `windows`.
    QList<QRectF> candidates;
    QList<int> sourceIndex;
    candidates.reserve(windows.size() - 1);
    sourceIndex.reserve(windows.size() - 1);
    for (int i = 0; i < windows.size(); ++i) {
        if (i == focusIdx) {
            continue;
        }
        candidates.append(QRectF(zones.at(i)));
        sourceIndex.append(i);
    }

    // requireOverlap: in-surface navigation only treats a window as a
    // left/right/up/down neighbour when it overlaps the focus on the
    // perpendicular axis. A purely diagonal tile (e.g. the top-right window when
    // moving "right" from a wider bottom-right tile in a tatami/pinwheel layout)
    // is NOT a neighbour — returning empty here makes the caller hit the surface
    // boundary and cross to the next output instead of swapping the window
    // up/down.
    const int pick = PhosphorGeometry::directionalNeighbor(QRectF(focusRect), candidates, *dir,
                                                           /*requireOverlap=*/true);
    if (pick < 0) {
        return QString(); // no tiled window in that direction — the surface boundary
    }
    return windows.at(sourceIndex.at(pick));
}

QRect NavigationController::rectForWindowInState(PhosphorTiles::TilingState* state, const QString& windowId) const
{
    if (!state) {
        return QRect();
    }
    const QStringList windows = state->tiledWindows();
    const QVector<QRect> zones = state->calculatedZones();
    if (zones.size() != windows.size()) {
        return QRect();
    }
    const int idx = windows.indexOf(windowId);
    if (idx < 0) {
        return QRect();
    }
    return zones.at(idx);
}

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
    // on that edge.
    int best = 0;
    for (int i = 1; i < zones.size(); ++i) {
        const QRect& r = zones.at(i);
        const QRect& b = zones.at(best);
        if (direction == QLatin1String("right") && r.x() < b.x()) {
            best = i;
        } else if (direction == QLatin1String("left") && r.x() > b.x()) {
            best = i;
        } else if (direction == QLatin1String("down") && r.y() < b.y()) {
            best = i;
        } else if (direction == QLatin1String("up") && r.y() > b.y()) {
            best = i;
        }
    }
    return windows.at(best);
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
    // focus probe. The source state exists whenever we get here (hasGeometry was
    // true), and rectForWindowInState null-guards its argument, so a miss yields
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
    // re-adds it when the neighbour is an autotile screen under its maxWindows
    // cap (onWindowAdded rejects WITHOUT inserting otherwise). Committing the
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
    // Non-creating lookup: stateForKey would CREATE and persist an empty
    // TilingState for the target desktop on a miss, leaking a state on every
    // cross-desktop focus probe to a desktop with no tiled windows.
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
    // If the target desktop on this screen is a DIFFERENT mode (snap), autotile
    // has no state there — defer to the daemon cross-mode handoff, which snaps
    // the window into the equivalent zone on the target snap desktop. The daemon
    // slot is a direct (synchronous) connection.
    if (m_engine->m_layoutManager
        && m_engine->m_layoutManager->modeForScreen(sourceScreenId, targetDesktop,
                                                    m_engine->m_context.currentActivity())
            == PhosphorZones::AssignmentEntry::Snapping) {
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

void NavigationController::swapFocusedInDirection(const QString& direction, const QString& action,
                                                  const QString& explicitWindowId)
{
    QString screenId;
    PhosphorTiles::TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state, explicitWindowId);

    // A single tiled window has no in-surface swap partner, but it CAN still
    // cross to another output / desktop — so don't bail on size < 2 here; that
    // check belongs to the order-based fallback below. Only an absent state or
    // empty surface is a hard stop.
    if (!state || windows.isEmpty()) {
        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("nothing_to_swap"), QString(), QString(),
                                            screenId);
        return;
    }

    const QString focused = !explicitWindowId.isEmpty() ? explicitWindowId : state->focusedWindow();
    if (focused.isEmpty()) {
        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("no_focus"), QString(), QString(), screenId);
        return;
    }

    bool hasGeometry = false;
    const QString targetWindow = directionalNeighborWindow(state, windows, focused, direction, hasGeometry);
    if (!targetWindow.isEmpty()) {
        const bool swapped = state->swapWindowsById(focused, targetWindow);
        m_engine->retileAfterOperation(screenId, swapped);
        Q_EMIT m_engine->navigationFeedback(swapped, action, direction, QString(), QString(), screenId);
        return;
    }

    if (hasGeometry) {
        // The focused window is at the layout edge in this direction — try the
        // adjacent output first, then the adjacent desktop, before giving up.
        if (crossOutputMove(screenId, focused, direction, action)) {
            Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("screen:") + direction, QString(),
                                                QString(), screenId);
            return;
        }
        // A SWAP is not extended across virtual desktops — exchanging with a
        // window on a desktop you can't see is meaningless; move owns "send to
        // another desktop". So only a MOVE action crosses the desktop boundary.
        if (action != QLatin1String("swap") && crossDesktopMove(screenId, focused, direction)) {
            Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("desktop:") + direction, QString(),
                                                QString(), screenId);
            return;
        }
        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("no_neighbor"), QString(), QString(),
                                            screenId);
        return;
    }

    // Geometry not computed yet: fall back to order-based neighbour with wrap.
    // This needs a partner — a single window has nothing to swap with and no
    // geometry to cross a boundary, so report nothing_to_swap.
    if (windows.size() < 2) {
        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("nothing_to_swap"), QString(), QString(),
                                            screenId);
        return;
    }
    const bool forward = isForwardDirection(direction);
    const int currentIndex = windows.indexOf(focused);
    if (currentIndex < 0) {
        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("no_focus"), QString(), QString(), screenId);
        return;
    }
    int targetIndex = forward ? currentIndex + 1 : currentIndex - 1;
    if (targetIndex < 0) {
        targetIndex = windows.size() - 1;
    } else if (targetIndex >= windows.size()) {
        targetIndex = 0;
    }
    const bool swapped = state->swapWindowsById(focused, windows.at(targetIndex));
    m_engine->retileAfterOperation(screenId, swapped);
    Q_EMIT m_engine->navigationFeedback(swapped, action, direction, QString(), QString(), screenId);
}

void NavigationController::focusInDirection(const QString& direction, const QString& action,
                                            const QString& explicitWindowId)
{
    QString screenId;
    PhosphorTiles::TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state, explicitWindowId);

    if (windows.isEmpty() || !state) {
        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("no_windows"), QString(), QString(),
                                            screenId);
        return;
    }

    const QString focused = !explicitWindowId.isEmpty() ? explicitWindowId : state->focusedWindow();

    bool hasGeometry = false;
    const QString target = directionalNeighborWindow(state, windows, focused, direction, hasGeometry);
    if (!target.isEmpty()) {
        Q_EMIT m_engine->activateWindowRequested(target);
        Q_EMIT m_engine->navigationFeedback(true, action, direction, QString(), QString(), screenId);
        return;
    }

    if (hasGeometry) {
        // No tiled window lies in this direction on this surface — try the
        // adjacent output, then the adjacent desktop, before reporting a boundary.
        const QString crossOutputTarget = crossOutputFocusTarget(screenId, focused, direction);
        if (!crossOutputTarget.isEmpty()) {
            Q_EMIT m_engine->activateWindowRequested(crossOutputTarget);
            Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("screen:") + direction, QString(),
                                                QString(), screenId);
            return;
        }
        const QString crossDesktopTarget = crossDesktopFocusTarget(screenId, direction);
        if (!crossDesktopTarget.isEmpty()) {
            // Activating a window on another desktop switches KWin to it.
            Q_EMIT m_engine->activateWindowRequested(crossDesktopTarget);
            Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("desktop:") + direction, QString(),
                                                QString(), screenId);
            return;
        }
        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("no_neighbor"), QString(), QString(),
                                            screenId);
        return;
    }

    // Geometry not computed yet: fall back to order-based cycling so navigation
    // still works on a surface whose layout has not been calculated.
    const bool forward = isForwardDirection(direction);
    const int currentIndex = qMax(0, windows.indexOf(focused));
    const int targetIndex = (currentIndex + (forward ? 1 : -1) + windows.size()) % windows.size();
    Q_EMIT m_engine->activateWindowRequested(windows.at(targetIndex));
    Q_EMIT m_engine->navigationFeedback(true, action, direction, QString(), QString(), screenId);
}

void NavigationController::moveFocusedToPosition(int position, const QString& explicitWindowId)
{
    QString screenId;
    PhosphorTiles::TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state, explicitWindowId);

    if (windows.isEmpty() || !state) {
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_windows"), QString(),
                                            QString(), screenId);
        return;
    }

    const QString focused = !explicitWindowId.isEmpty() ? explicitWindowId : state->focusedWindow();
    if (focused.isEmpty()) {
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_focus"), QString(),
                                            QString(), screenId);
        return;
    }

    // position is 1-based (from snap-to-zone-N shortcuts), convert to 0-based
    const int targetIndex = qBound(0, position - 1, windows.size() - 1);
    const bool moved = state->moveToTiledPosition(focused, targetIndex);
    m_engine->retileAfterOperation(screenId, moved);

    if (moved) {
        Q_EMIT m_engine->navigationFeedback(true, QStringLiteral("snap"), QStringLiteral("position_%1").arg(position),
                                            QString(), QString(), screenId);
    } else {
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("already_at_position"),
                                            QString(), QString(), screenId);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Split ratio adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void NavigationController::increaseMasterRatio(qreal delta)
{
    QString screenId;
    PhosphorTiles::TilingState* state = resolveActiveState(screenId);
    if (!state) {
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("master_ratio"), QStringLiteral("no_focus"),
                                            QString(), QString(), QString());
        return;
    }

    const qreal oldRatio = state->splitRatio();
    state->setSplitRatio(oldRatio + delta);
    const qreal resultRatio = state->splitRatio(); // clamped
    const bool changed = !qFuzzyCompare(1.0 + resultRatio, 1.0 + oldRatio);

    if (changed) {
        if (m_engine->hasPerScreenOverride(screenId, PerScreenKeys::SplitRatio)) {
            // This screen carries an explicit per-screen ratio override — keep it
            // in sync so the value persists across settings reloads
            // (applyPerScreenConfig reads the stored override).
            m_engine->updatePerScreenOverride(screenId, PerScreenKeys::SplitRatio, resultRatio);
        } else {
            // No override: keep the adjustment local to the active
            // screen+desktop+activity's TilingState (set above, serialized with the
            // session state). Mark it user-tuned so propagateGlobalSplitRatio leaves
            // it alone on a settings refresh, and deliberately do NOT write the
            // global config / settings — a per-desktop ratio tweak is not a new
            // global default and must not bleed to sibling screens, other desktops,
            // or other activities.
            m_engine->noteSplitRatioUserTuned(screenId);
        }

        if (m_engine->isEnabled()) {
            m_engine->retileAfterOperation(screenId, true);
        }
    }

    // Always show OSD with the clamped value — even at min/max bounds
    int pct = qRound(resultRatio * 100.0);
    QString reason = (delta >= 0 ? QStringLiteral("increased:") : QStringLiteral("decreased:")) + QString::number(pct);
    Q_EMIT m_engine->navigationFeedback(changed, QStringLiteral("master_ratio"), reason, QString(), QString(),
                                        screenId);
}

void NavigationController::decreaseMasterRatio(qreal delta)
{
    increaseMasterRatio(-delta);
}

void NavigationController::setGlobalSplitRatio(qreal ratio)
{
    ratio = std::clamp(ratio, PhosphorTiles::AutotileDefaults::MinSplitRatio,
                       PhosphorTiles::AutotileDefaults::MaxSplitRatio);
    m_engine->config()->splitRatio = ratio;
    applyToAllStates([ratio](PhosphorTiles::TilingState* state) {
        state->setSplitRatio(ratio);
    });
}

void NavigationController::setGlobalMasterCount(int count)
{
    count = std::clamp(count, PhosphorTiles::AutotileDefaults::MinMasterCount,
                       PhosphorTiles::AutotileDefaults::MaxMasterCount);
    m_engine->config()->masterCount = count;
    applyToAllStates([count](PhosphorTiles::TilingState* state) {
        state->setMasterCount(count);
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// Master count adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void NavigationController::adjustMasterCount(int delta)
{
    QString screenId;
    PhosphorTiles::TilingState* state = resolveActiveState(screenId);
    if (!state) {
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("master_count"), QStringLiteral("no_focus"),
                                            QString(), QString(), QString());
        return;
    }

    const int oldCount = state->masterCount();
    state->setMasterCount(oldCount + delta); // setMasterCount clamps internally
    const int resultCount = state->masterCount();
    const bool changed = resultCount != oldCount;

    if (changed) {
        if (m_engine->hasPerScreenOverride(screenId, PerScreenKeys::MasterCount)) {
            // Explicit per-screen override — keep it in sync so it persists across
            // settings reloads (applyPerScreenConfig reads the stored override).
            m_engine->updatePerScreenOverride(screenId, PerScreenKeys::MasterCount, resultCount);
        } else {
            // No override: keep the adjustment local to the active
            // screen+desktop+activity's TilingState (set above, serialized with the
            // session state). Mark it
            // user-tuned so propagateGlobalMasterCount leaves it alone on a refresh,
            // and deliberately do NOT write the global config / settings — a
            // per-desktop master-count tweak is not a new global default.
            m_engine->noteMasterCountUserTuned(screenId);
        }

        if (m_engine->isEnabled()) {
            m_engine->retileAfterOperation(screenId, true);
        }
    }

    // Always show OSD with the clamped value — even at bounds
    QString reason =
        (delta > 0 ? QStringLiteral("increased:") : QStringLiteral("decreased:")) + QString::number(resultCount);
    Q_EMIT m_engine->navigationFeedback(changed, QStringLiteral("master_count"), reason, QString(), QString(),
                                        screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private helpers
// ═══════════════════════════════════════════════════════════════════════════════

PhosphorTiles::TilingState* NavigationController::resolveActiveState(QString& outScreenId) const
{
    outScreenId = resolveActiveScreen();
    if (outScreenId.isEmpty()) {
        return nullptr;
    }

    const auto key = m_engine->currentKeyForScreen(outScreenId);
    return m_engine->m_states.stateForKey(key);
}

QString NavigationController::resolveActiveScreen() const
{
    if (!m_engine->m_activeScreen.isEmpty()) {
        return m_engine->m_activeScreen;
    }
    if (!m_engine->m_autotileScreens.isEmpty()) {
        return *m_engine->m_autotileScreens.begin();
    }
    return QString();
}

void NavigationController::emitFocusRequestAtIndex(int indexOffset, bool useFirst)
{
    QString screenId;
    PhosphorTiles::TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state);
    if (windows.isEmpty()) {
        return;
    }

    int targetIndex = 0;
    if (!useFirst && state) {
        const QString focused = state->focusedWindow();
        const int currentIndex = qMax(0, windows.indexOf(focused));
        targetIndex = (currentIndex + indexOffset + windows.size()) % windows.size();
    }

    Q_EMIT m_engine->activateWindowRequested(windows.at(targetIndex));
}

QStringList NavigationController::tiledWindowsForFocusedScreen(QString& outScreenId,
                                                               PhosphorTiles::TilingState*& outState,
                                                               const QString& explicitWindowId)
{
    outState = nullptr;

    // Authoritative path: when the daemon supplied a windowId (KWin's live
    // focus), find the state that actually contains that window. The engine's
    // per-state focusedWindow() tracker may be stale because focus moved
    // through floating, snapped, or never-tracked windows that don't update
    // it — using the daemon's value avoids operating on the wrong window.
    if (!explicitWindowId.isEmpty()) {
        for (auto it = m_engine->m_states.states().constBegin(); it != m_engine->m_states.states().constEnd(); ++it) {
            // Match each screen's EFFECTIVE desktop, not the raw global one: a
            // screen sticky-pinned by the "virtualdesktopsonlyonprimary" model
            // (a per-output desktop pin in m_context) keeps its TilingState on its
            // pinned desktop, so a bare `!= m_context's global desktop` would skip
            // the pinned screen and miss the explicit window living there.
            // currentKeyForScreen resolves the override; for unpinned screens it is
            // m_context's global desktop.
            if (it.key().desktop != m_engine->currentKeyForScreen(it.key().screenId).desktop
                || it.key().activity != m_engine->m_context.currentActivity()) {
                continue;
            }
            PhosphorTiles::TilingState* state = it.value();
            if (state && state->containsWindow(explicitWindowId)) {
                outScreenId = it.key().screenId;
                outState = state;
                return state->tiledWindows();
            }
        }
        // Fall through to focused-screen lookup if the explicit window isn't
        // tracked by autotile — caller will surface no_focus / no_windows.
    }

    // Use the tracked active screen (set by onWindowFocused) to avoid
    // non-deterministic QHash iteration when multiple screens have focused windows
    if (!m_engine->m_activeScreen.isEmpty()) {
        const auto key = m_engine->currentKeyForScreen(m_engine->m_activeScreen);
        auto sit = m_engine->m_states.states().constFind(key);
        if (sit != m_engine->m_states.states().constEnd()) {
            PhosphorTiles::TilingState* state = sit.value();
            if (state && !state->focusedWindow().isEmpty()) {
                outScreenId = m_engine->m_activeScreen;
                outState = state;
                return state->tiledWindows();
            }
        }
    }

    // Fallback: scan states for current desktop/activity (e.g., if m_activeScreen is stale).
    // Same sticky-pin-aware desktop match as the explicit-window scan above.
    for (auto it = m_engine->m_states.states().constBegin(); it != m_engine->m_states.states().constEnd(); ++it) {
        if (it.key().desktop != m_engine->currentKeyForScreen(it.key().screenId).desktop
            || it.key().activity != m_engine->m_context.currentActivity()) {
            continue;
        }
        PhosphorTiles::TilingState* state = it.value();
        if (state && !state->focusedWindow().isEmpty()) {
            outScreenId = it.key().screenId;
            outState = state;
            return state->tiledWindows();
        }
    }

    // No focused window found - fallback to primary screen if available
    const PhosphorScreens::PhysicalScreen primaryScreen =
        m_engine->m_screenManager ? m_engine->m_screenManager->primaryScreen() : PhosphorScreens::PhysicalScreen{};
    if (primaryScreen.isValid()) {
        outScreenId = primaryScreen.identifier;
        const auto key = m_engine->currentKeyForScreen(outScreenId);
        auto sit = m_engine->m_states.states().constFind(key);
        if (sit != m_engine->m_states.states().constEnd() && sit.value()) {
            outState = sit.value();
            return sit.value()->tiledWindows();
        }
    }

    outScreenId.clear();
    return {};
}

void NavigationController::applyToAllStates(const std::function<void(PhosphorTiles::TilingState*)>& operation)
{
    if (m_engine->m_states.states().isEmpty()) {
        return; // No states to modify
    }

    // Only apply to states for each screen's current desktop/activity. Under
    // per-output virtual desktops (#648) the desktop is resolved per-screen,
    // matching propagateGlobalSplitRatio/propagateGlobalMasterCount.
    for (auto it = m_engine->m_states.states().constBegin(); it != m_engine->m_states.states().constEnd(); ++it) {
        if (it.key().desktop == m_engine->currentKeyForScreen(it.key().screenId).desktop
            && it.key().activity == m_engine->m_context.currentActivity() && it.value()) {
            operation(it.value());
        }
    }

    if (m_engine->isEnabled()) {
        m_engine->retile();
    }
}

} // namespace PhosphorTileEngine
