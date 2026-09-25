// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTileEngine/NavigationController.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/TilingAlgorithm.h>
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

bool NavigationController::isForwardDirection(const QString& direction)
{
    return direction == QLatin1String("right") || direction == QLatin1String("down");
}

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
    if (windows.isEmpty() || !state) {
        // The !state term is defensive symmetry with swapFocusedWithMaster —
        // no path returns a non-empty list with a null out-state today.
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("focus_master"), QStringLiteral("no_windows"),
                                            QString(), QString(), screenId);
        return;
    }
    // Reuse the resolution above rather than calling emitFocusRequestAtIndex,
    // which would run the whole three-tier walk (a tiledWindows() list build
    // per state in the scan tier) a second time for the same press.
    activateResolvedWindowAtIndex(windows, state, 0, true);
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
    // Tiled-membership discriminator (see moveFocusedToPosition's): a
    // floating focus must not be reordered inside windowOrder as a
    // "promotion" nothing on screen shows.
    if (!windows.contains(focused)) {
        const QString reason =
            state->containsWindow(focused) ? QStringLiteral("not_tiled") : QStringLiteral("no_focus");
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("swap_master"), reason, QString(), QString(),
                                            screenId);
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

    if (!state || windows.size() < 2) {
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("nothing_to_rotate"),
                                            QString(), QString(), screenId);
        return; // Nothing to rotate with 0 or 1 window
    }

    // Rotate the window order
    bool rotated = state->rotateWindows(clockwise);

    // For overlap layouts, rotating exists to bring a different window to the
    // user's working position (monocle cycles the visible window, deck cycles
    // which window holds the master slot). Geometry alone cannot express that
    // — with identical or overlapping zones the retile leaves the old window
    // on top — so request activation of the window that just arrived there.
    // Layouts with a declared master zone (deck, horizontal-deck) focus the
    // window now occupying that slot; for the rest the interaction front is
    // the stack's topmost window (last tiled index for lastOnTop, first for
    // firstOnTop). The pending focus is emitted AFTER windowsTiled (see
    // applyTiling), landing the raise on top of the effect's post-tile
    // restack.
    if (rotated) {
        const PhosphorTiles::TilingAlgorithm* algo = m_engine->effectiveAlgorithm(screenId);
        if (algo && algo->producesOverlappingZones()) {
            const QStringList rotatedOrder = state->tiledWindows();
            if (!rotatedOrder.isEmpty()) {
                const int masterIdx = algo->masterZoneIndex();
                QString front;
                if (masterIdx >= 0 && masterIdx < rotatedOrder.size()) {
                    front = rotatedOrder.at(masterIdx);
                } else if (algo->overlapStacking() == QLatin1String("firstOnTop")) {
                    front = rotatedOrder.first();
                } else {
                    front = rotatedOrder.last();
                }
                m_engine->requestPostRetileFocus(screenId, front);
            }
        }
    }

    m_engine->retileAfterOperation(screenId, rotated);

    if (rotated) {
        qCInfo(PhosphorTileEngine::lcTileEngine) << "Rotated windows" << (clockwise ? "clockwise" : "counterclockwise");
        QString reason = QStringLiteral("%1:%2")
                             .arg(clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise"))
                             .arg(windows.size());
        Q_EMIT m_engine->navigationFeedback(true, QStringLiteral("rotate"), reason, QString(), QString(), screenId);
    } else {
        // Defensive: TilingState::rotateWindows only refuses below two
        // windows, which the guard above already rejected. Kept as a tripwire
        // in case that precondition ever moves.
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_rotations"), QString(),
                                            QString(), screenId);
    }
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
        // On failure the OSD needs a structured reason, not the raw direction
        // (which the failure branches would misread as a boundary condition).
        Q_EMIT m_engine->navigationFeedback(swapped, action, swapped ? direction : QStringLiteral("swap_failed"),
                                            QString(), QString(), screenId);
        return;
    }

    if (hasGeometry) {
        // The focused window is at the layout edge in this direction — try the
        // adjacent output first, then the adjacent desktop, before giving up.
        //
        // The cross-MODE leg of crossOutputMove defers to the daemon's
        // direct-connected handoff handlers, which can no-op (target engine
        // unavailable, or per-desktop mode resolution disagreeing with this
        // engine's neighbour check). This success OSD is therefore optimistic
        // for that leg — the same documented convention as the snap engine's
        // tryCrossModeOutput. A daemon-side failure emit cannot fix it: the
        // handler runs synchronously inside the Q_EMIT, so its OSD would be
        // immediately replaced by this one.
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
    // The focused-window discriminator runs FIRST: a window that is tracked
    // but absent from tiledWindows() is floating, not unfocused, and reporting
    // "nothing to swap" for it would hide the real reason whenever the screen
    // also happens to hold fewer than two tiled windows.
    const int currentIndex = windows.indexOf(focused);
    if (currentIndex < 0) {
        const QString reason =
            state->containsWindow(focused) ? QStringLiteral("not_tiled") : QStringLiteral("no_focus");
        Q_EMIT m_engine->navigationFeedback(false, action, reason, QString(), QString(), screenId);
        return;
    }
    // A lone tiled window has no partner to trade with and no geometry to
    // cross a boundary with.
    if (windows.size() < 2) {
        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("nothing_to_swap"), QString(), QString(),
                                            screenId);
        return;
    }
    const bool forward = isForwardDirection(direction);
    int targetIndex = forward ? currentIndex + 1 : currentIndex - 1;
    if (targetIndex < 0) {
        targetIndex = windows.size() - 1;
    } else if (targetIndex >= windows.size()) {
        targetIndex = 0;
    }
    const bool swapped = state->swapWindowsById(focused, windows.at(targetIndex));
    m_engine->retileAfterOperation(screenId, swapped);
    // Same structured failure reason as the geometry path above.
    Q_EMIT m_engine->navigationFeedback(swapped, action, swapped ? direction : QStringLiteral("swap_failed"), QString(),
                                        QString(), screenId);
}

void NavigationController::focusInDirection(const QString& direction, const QString& action,
                                            const QString& explicitWindowId)
{
    QString screenId;
    PhosphorTiles::TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state, explicitWindowId);

    if (windows.isEmpty() || !state) {
        // An EMPTY autotile surface still crosses (parity with the scroll
        // engine's empty-screen arm): a directional press with nothing local
        // walks onto the neighbour instead of dead-ending — the boundary is
        // the whole verb when there is nothing local to prefer.
        // crossOutputFocusTarget degrades to the neighbour's first tiled
        // window for the empty focused id. Deliberately OUTPUT-only, like
        // the scroll twin's empty arm: crossing DESKTOPS from an empty
        // surface is not a spatial gesture. Only when nothing was activated
        // does the press answer no_windows.
        //
        // The crossing ORIGIN is the pressed screen (the active-screen
        // hint), NOT `screenId`: on the total-miss path
        // tiledWindowsForFocusedScreen homes its out-param on the PRIMARY
        // screen so the failure OSD has an output, and crossing from the
        // primary's edge would walk a neighbour of a screen the user never
        // pressed on. `screenId` stays the OSD-homing id.
        const QString pressedScreen =
            (!m_engine->m_activeScreen.isEmpty() && m_engine->isAutotileScreen(m_engine->m_activeScreen))
            ? m_engine->m_activeScreen
            : screenId;
        const QString emptyNeighbor = (m_engine->m_crossSurfaceResolver && !pressedScreen.isEmpty())
            ? m_engine->m_crossSurfaceResolver->neighborOutputInDirection(pressedScreen, direction)
            : QString();
        if (!emptyNeighbor.isEmpty() && emptyNeighbor != pressedScreen) {
            const QString crossOutputTarget = crossOutputFocusTarget(pressedScreen, QString(), direction);
            if (!crossOutputTarget.isEmpty()) {
                Q_EMIT m_engine->activateWindowRequested(crossOutputTarget);
                Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("screen:") + direction, QString(),
                                                    QString(), emptyNeighbor);
                return;
            }
            if (!m_engine->isAutotileScreen(emptyNeighbor)) {
                bool handled = false;
                Q_EMIT m_engine->crossModeFocusRequested(emptyNeighbor, direction, &handled);
                if (handled) {
                    Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("screen:") + direction, QString(),
                                                        QString(), emptyNeighbor);
                    return;
                }
            }
        }
        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("no_windows"), QString(), QString(),
                                            screenId);
        return;
    }

    // Resolved once for the geometry-edge crossing arms below: the
    // same-mode probe needs it only for the announcement, the cross-mode
    // arm for the gate too. Here `screenId` IS the pressed screen — the
    // state resolution above found real windows on it.
    const QString neighbor = m_engine->m_crossSurfaceResolver
        ? m_engine->m_crossSurfaceResolver->neighborOutputInDirection(screenId, direction)
        : QString();

    const QString focused = !explicitWindowId.isEmpty() ? explicitWindowId : state->focusedWindow();
    if (focused.isEmpty() || windows.indexOf(focused) < 0) {
        // The focus is not a member of the tiled set: either untracked, or
        // sitting on a floating / dialog / excluded window on this screen.
        // Both spellings behave identically downstream —
        // directionalNeighborWindow bails before reporting geometry, and the
        // order-cycling fallback then clamps indexOf(-1) to 0 and activates an
        // arbitrary neighbour as if the press had travelled. Enter at the
        // master instead, which is where a directional press from "nowhere"
        // belongs, and report it as a master entry rather than as travel in
        // the pressed direction, which never happened.
        Q_EMIT m_engine->activateWindowRequested(windows.first());
        Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("master"), QString(), QString(), screenId);
        return;
    }

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
            // Announced on the DESTINATION output, matching the cross-mode
            // arm below and the scroll engine's crossing convention: the
            // card is about where focus landed, not the output it left.
            Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("screen:") + direction, QString(),
                                                QString(), neighbor.isEmpty() ? screenId : neighbor);
            return;
        }
        // Different-MODE neighbour output: the probe above only knows autotile
        // state, so a scrolling or snapping neighbour answered empty. Defer to
        // the daemon the way the move/swap verbs already do — it asks the
        // target engine for its entry-edge window and activates it. The
        // connection is DirectConnection (enginewiring.cpp), so the out-param
        // carries the handler's verdict on return: an empty neighbour surface
        // is an everyday state for a focus, and the cross-desktop / no_neighbor
        // fallthrough below must still run when nothing was activated.
        if (!neighbor.isEmpty() && neighbor != screenId && !m_engine->isAutotileScreen(neighbor)) {
            bool handled = false;
            Q_EMIT m_engine->crossModeFocusRequested(neighbor, direction, &handled);
            if (handled) {
                // QString() source slot like every sibling emit here: the
                // window the OSD is about lives on the announced screen,
                // and `focused` does not.
                Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("screen:") + direction, QString(),
                                                    QString(), neighbor);
                return;
            }
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
    // Tiled-membership discriminator, matching swapFocusedInDirection's:
    // focusedWindow() is never cleared and can name a floating window, and
    // moveToTiledPosition would then reorder that float inside windowOrder —
    // a visual no-op announced as a success (and the float's later un-float
    // lands on the reordered slot). An untracked focus reports no_focus.
    if (!windows.contains(focused)) {
        const QString reason =
            state->containsWindow(focused) ? QStringLiteral("not_tiled") : QStringLiteral("no_focus");
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("snap"), reason, QString(), QString(), screenId);
        return;
    }

    // position is 1-based (from snap-to-zone-N shortcuts). An out-of-range
    // digit is REJECTED rather than clamped (the scroll and snap twins'
    // convention: silently retargeting a position the layout cannot honour
    // moves a window the user never asked to move, and the success OSD then
    // names the digit rather than where the window went). Checked before the
    // subtraction so an INT_MIN off the exported surface cannot underflow.
    if (position < 1 || position > windows.size()) {
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"),
                                            QString(), QString(), screenId);
        return;
    }
    const int targetIndex = position - 1;
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

    // Always show OSD with the clamped value — even at min/max bounds.
    // A zero delta reads as "increased", matching adjustMasterCount.
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
    applyToAllStates([this, ratio](const QString& screenId, PhosphorTiles::TilingState* state) {
        // Screens carrying an explicit per-screen SplitRatio override keep it,
        // mirroring propagateGlobalSplitRatio — writing them here would only
        // last until the next settings refresh resurfaces the override.
        if (m_engine->hasPerScreenOverride(screenId, PerScreenKeys::SplitRatio)) {
            return false;
        }
        state->setSplitRatio(ratio);
        return true;
    });
}

void NavigationController::setGlobalMasterCount(int count)
{
    count = std::clamp(count, PhosphorTiles::AutotileDefaults::MinMasterCount,
                       PhosphorTiles::AutotileDefaults::MaxMasterCount);
    m_engine->config()->masterCount = count;
    applyToAllStates([this, count](const QString& screenId, PhosphorTiles::TilingState* state) {
        // Screens carrying an explicit per-screen MasterCount override keep
        // it, mirroring propagateGlobalMasterCount.
        if (m_engine->hasPerScreenOverride(screenId, PerScreenKeys::MasterCount)) {
            return false;
        }
        state->setMasterCount(count);
        return true;
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
            // screen+desktop+activity's TilingState (set above, serialized with
            // the session state). Mark it user-tuned so propagateGlobalMasterCount
            // leaves it alone on a refresh, and deliberately do NOT write the
            // global config / settings — a per-desktop master-count tweak is not
            // a new global default.
            m_engine->noteMasterCountUserTuned(screenId);
        }

        if (m_engine->isEnabled()) {
            m_engine->retileAfterOperation(screenId, true);
        }
    }

    // Always show OSD with the clamped value — even at bounds. A zero delta
    // reads as "increased", matching increaseMasterRatio.
    QString reason =
        (delta >= 0 ? QStringLiteral("increased:") : QStringLiteral("decreased:")) + QString::number(resultCount);
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
    // Gated on autotile mode, matching tier 2 of tiledWindowsForFocusedScreen.
    // setActiveScreenHint accepts any screenId, including a snap-mode one, so
    // an ungated hint sent the master-ratio and master-count shortcuts to a
    // screen with no tiling state — they reported no_focus while the autotile
    // screen the user was actually on went untouched.
    if (!m_engine->m_activeScreen.isEmpty() && m_engine->isAutotileScreen(m_engine->m_activeScreen)) {
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
        // Report it. focusNext/focusPrevious used to return in silence here,
        // so a press that did nothing also said nothing and read as a broken
        // shortcut, while every sibling operation surfaced no_windows.
        Q_EMIT m_engine->navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_windows"), QString(),
                                            QString(), screenId);
        return;
    }
    activateResolvedWindowAtIndex(windows, state, indexOffset, useFirst);
}

void NavigationController::activateResolvedWindowAtIndex(const QStringList& windows, PhosphorTiles::TilingState* state,
                                                         int indexOffset, bool useFirst)
{
    if (windows.isEmpty()) {
        return;
    }

    int targetIndex = 0;
    if (!useFirst && state) {
        const QString focused = state->focusedWindow();
        // An unresolved focus is treated as an implied index 0 (master), so
        // "next" is the second window and "previous" the last — the pinned
        // cycling convention (testNavigation_noTrackedFocus_...), distinct
        // from focusInDirection's enter-at-master rule: a CYCLE press asks
        // for travel, a DIRECTIONAL press from "nowhere" asks for an entry.
        const int currentIndex = qMax(0, windows.indexOf(focused));
        targetIndex = (currentIndex + indexOffset + windows.size()) % windows.size();
    }

    Q_EMIT m_engine->activateWindowRequested(windows.at(targetIndex));
}

QStringList NavigationController::tiledWindowsForFocusedScreen(QString& outScreenId,
                                                               PhosphorTiles::TilingState*& outState,
                                                               const QString& explicitWindowId,
                                                               bool requireTiledWindows)
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

    // Use the tracked active screen (set by onWindowFocused, and by the
    // daemon's setActiveScreenHint on every autotile shortcut) to avoid
    // non-deterministic QHash iteration when multiple screens have windows.
    //
    // The hinted screen wins whenever it HAS tiled windows — deliberately not
    // gated on a tracked focusedWindow(). Focus can legitimately sit on a
    // floating, snapped, or never-tracked window there, and requiring a
    // tracked focus sent every screen-scoped operation (rotate, swap with
    // master, focus master, cycle) down to the hash-ordered scan below, where
    // it acted on a DIFFERENT monitor than the shortcut targeted.
    if (!m_engine->m_activeScreen.isEmpty() && m_engine->isAutotileScreen(m_engine->m_activeScreen)) {
        const auto key = m_engine->currentKeyForScreen(m_engine->m_activeScreen);
        auto sit = m_engine->m_states.states().constFind(key);
        if (sit != m_engine->m_states.states().constEnd()) {
            PhosphorTiles::TilingState* state = sit.value();
            const QStringList tiled = state ? state->tiledWindows() : QStringList();
            if (!tiled.isEmpty() || (state && !requireTiledWindows && !state->focusedWindow().isEmpty())) {
                outScreenId = m_engine->m_activeScreen;
                outState = state;
                return tiled;
            }
        }
    }

    // Fallback: scan states for current desktop/activity (e.g. a stale hint).
    // Selects on TILED WINDOWS, matching the hinted branch: focusedWindow() is
    // never cleared and can name a floating window, so selecting on it could
    // return a state with nothing tiled while a sibling screen has a full
    // layout — every consumer would then report "no windows" on a desktop that
    // plainly has some. A state that also holds a remembered focus outranks
    // the no-focus ones.
    //
    // BOTH tiers are ranked rather than taken first-seen: QHash order is not
    // stable run to run, and focusedWindow() is per-state (never cleared), so
    // two monitors can each remember one — an in-loop return on the first
    // focus-bearing state landed rotate/swap-with-master on an arbitrary
    // output. Preference order is the hinted screen, then the primary screen,
    // then the lowest screenId, which is total and deterministic.
    const QString primaryScreenId =
        m_engine->m_screenManager ? m_engine->m_screenManager->primaryScreen().identifier : QString();
    const auto fallbackRank = [&](const QString& screenId) {
        if (!m_engine->m_activeScreen.isEmpty() && screenId == m_engine->m_activeScreen) {
            return 0;
        }
        if (!primaryScreenId.isEmpty() && screenId == primaryScreenId) {
            return 1;
        }
        return 2;
    };
    PhosphorTiles::TilingState* focusedState = nullptr;
    QString focusedScreen;
    int focusedRankValue = 0;
    PhosphorTiles::TilingState* fallbackState = nullptr;
    QString fallbackScreen;
    int fallbackRankValue = 0;
    for (auto it = m_engine->m_states.states().constBegin(); it != m_engine->m_states.states().constEnd(); ++it) {
        if (it.key().desktop != m_engine->currentKeyForScreen(it.key().screenId).desktop
            || it.key().activity != m_engine->m_context.currentActivity()
            || !m_engine->isAutotileScreen(it.key().screenId)) {
            continue;
        }
        PhosphorTiles::TilingState* state = it.value();
        if (!state) {
            continue;
        }
        const bool hasTiled = !state->tiledWindows().isEmpty();
        if (!hasTiled && (requireTiledWindows || state->focusedWindow().isEmpty())) {
            continue;
        }
        const int rank = fallbackRank(it.key().screenId);
        if (!state->focusedWindow().isEmpty()) {
            if (!focusedState || rank < focusedRankValue
                || (rank == focusedRankValue && it.key().screenId < focusedScreen)) {
                focusedState = state;
                focusedScreen = it.key().screenId;
                focusedRankValue = rank;
            }
            continue;
        }
        if (!fallbackState || rank < fallbackRankValue
            || (rank == fallbackRankValue && it.key().screenId < fallbackScreen)) {
            fallbackState = state;
            fallbackScreen = it.key().screenId;
            fallbackRankValue = rank;
        }
    }
    if (focusedState) {
        outScreenId = focusedScreen;
        outState = focusedState;
        return focusedState->tiledWindows();
    }
    if (fallbackState) {
        outScreenId = fallbackScreen;
        outState = fallbackState;
        return fallbackState->tiledWindows();
    }

    // Nothing resolved yet — fall back to the primary screen. The STATE lookup
    // is gated on autotile mode so a snap-mode screen never receives a tiling
    // mutation that retileAfterOperation would then refuse to paint, but the
    // screen id is set either way: on a total miss the callers still emit
    // navigationFeedback, and OverlayService needs an output to place that OSD
    // on. Gating the id as well left every failure OSD homeless, which under
    // the virtual-screen model is the common case (m_autotileScreens holds
    // VIRTUAL ids while primaryScreen.identifier is physical, so the mode gate
    // can never pass on a subdivided setup).
    const PhosphorScreens::PhysicalScreen primaryScreen =
        m_engine->m_screenManager ? m_engine->m_screenManager->primaryScreen() : PhosphorScreens::PhysicalScreen{};
    if (primaryScreen.isValid()) {
        outScreenId = primaryScreen.identifier;
        if (m_engine->isAutotileScreen(primaryScreen.identifier)) {
            const auto key = m_engine->currentKeyForScreen(outScreenId);
            auto sit = m_engine->m_states.states().constFind(key);
            if (sit != m_engine->m_states.states().constEnd() && sit.value()) {
                outState = sit.value();
                return sit.value()->tiledWindows();
            }
        }
        return {};
    }

    outScreenId = m_engine->m_activeScreen;
    return {};
}

void NavigationController::applyToAllStates(
    const std::function<bool(const QString& screenId, PhosphorTiles::TilingState*)>& operation)
{
    if (m_engine->m_states.states().isEmpty()) {
        return; // No states to modify
    }

    // Every state, on every desktop and activity — see the header for why this
    // must not be narrowed to the current context the way the passive
    // propagateGlobalSplitRatio/propagateGlobalMasterCount refresh is.
    //
    // Only a write to a state in the CURRENT context can change anything on
    // screen: retile() reflows the current desktop/activity's autotile screens
    // and nothing else. Track that case specifically, so a pass that wrote only
    // other desktops (or wrote nothing at all, e.g. every screen carries a
    // per-screen override of this key) does not trigger a pointless full retile.
    bool wroteCurrentContext = false;
    for (auto it = m_engine->m_states.states().constBegin(); it != m_engine->m_states.states().constEnd(); ++it) {
        if (!it.value()) {
            continue;
        }
        if (!operation(it.key().screenId, it.value())) {
            continue;
        }
        // Under per-output virtual desktops (#648) the desktop is resolved
        // per-screen, so "current" is a per-screen question.
        if (it.key().desktop == m_engine->currentKeyForScreen(it.key().screenId).desktop
            && it.key().activity == m_engine->m_context.currentActivity()) {
            wroteCurrentContext = true;
        }
    }

    if (wroteCurrentContext && m_engine->isEnabled()) {
        m_engine->retile();
    }
}

} // namespace PhosphorTileEngine
