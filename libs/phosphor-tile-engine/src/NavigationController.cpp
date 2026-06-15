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

#include <QScreen>
#include <algorithm>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorTileEngine {

namespace PerScreenKeys = PhosphorEngine::PerScreenKeys;

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

    const int pick = PhosphorGeometry::directionalNeighbor(QRectF(focusRect), candidates, *dir);
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
    PhosphorTiles::TilingState* neighborState = m_engine->tilingStateForScreen(neighbor);
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
    const QRect focusRect = rectForWindowInState(m_engine->tilingStateForScreen(sourceScreenId), focused);
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
                                           const QString& direction)
{
    if (!m_engine->m_crossSurfaceResolver) {
        return false;
    }
    const QString neighbor = m_engine->m_crossSurfaceResolver->neighborOutputInDirection(sourceScreenId, direction);
    if (neighbor.isEmpty()) {
        return false;
    }
    const PhosphorEngine::TilingStateKey oldKey = m_engine->currentKeyForScreen(sourceScreenId);
    const PhosphorEngine::TilingStateKey newKey = m_engine->currentKeyForScreen(neighbor);
    // Re-point the window's state-key BEFORE migrating, exactly as the reactive
    // windowFocused() path does: migrateWindowBetweenKeys re-adds the window via
    // onWindowAdded() → screenForWindow(), which reads this map. Without the
    // update it would resolve back to the source screen and re-add it there.
    m_engine->m_windowToStateKey[focused] = newKey;
    // migrateWindowBetweenKeys removes the window from the source state (with
    // its onWindowRemoved lifecycle) and adds it on the neighbour output. It
    // schedules DEFERRED retiles for both — but those can be raced by the
    // reactive screen-change event the move triggers (observed on real
    // hardware: the source monitor failed to reflow). Retile both surfaces
    // SYNCHRONOUSLY here, exactly as the in-surface swap does, so the source's
    // reflow and the destination's placement reach the compositor within this
    // handler, before activateWindowRequested.
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
    const int targetDesktop =
        m_engine->m_crossSurfaceResolver->neighborDesktopInDirection(m_engine->m_currentDesktop, direction);
    if (targetDesktop <= 0) {
        return QString();
    }
    const PhosphorEngine::TilingStateKey targetKey{sourceScreenId, targetDesktop, m_engine->m_currentActivity};
    PhosphorTiles::TilingState* targetState = m_engine->stateForKey(targetKey);
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
    const bool forward = (direction == QLatin1String("right") || direction == QLatin1String("down"));
    return forward ? targetWindows.first() : targetWindows.last();
}

bool NavigationController::crossDesktopMove(const QString& sourceScreenId, const QString& focused,
                                            const QString& direction)
{
    if (!m_engine->m_crossSurfaceResolver) {
        return false;
    }
    const int targetDesktop =
        m_engine->m_crossSurfaceResolver->neighborDesktopInDirection(m_engine->m_currentDesktop, direction);
    if (targetDesktop <= 0) {
        return false;
    }
    const PhosphorEngine::TilingStateKey sourceKey = m_engine->currentKeyForScreen(sourceScreenId);
    const PhosphorEngine::TilingStateKey targetKey{sourceScreenId, targetDesktop, m_engine->m_currentActivity};
    PhosphorTiles::TilingState* sourceState = m_engine->stateForKey(sourceKey);
    PhosphorTiles::TilingState* targetState = m_engine->stateForKey(targetKey);
    if (!sourceState || !targetState) {
        return false;
    }

    const bool forward = (direction == QLatin1String("right") || direction == QLatin1String("down"));
    sourceState->removeWindow(focused);
    targetState->addWindow(focused, forward ? 0 : -1);
    m_engine->m_windowToStateKey[focused] = targetKey;
    // Close the hole on the (visible) source desktop SYNCHRONOUSLY so its
    // reflow reaches the compositor now, not via a deferred retile a reactive
    // event could race (same fix as crossOutputMove). The target desktop is not
    // current, so it tiles when it next becomes visible.
    m_engine->retileAfterOperation(sourceScreenId, true);
    // Ask the compositor to move the real KWin window to the target desktop.
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
        if (crossOutputMove(screenId, focused, direction)) {
            Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("screen:") + direction, QString(),
                                                QString(), screenId);
            return;
        }
        if (crossDesktopMove(screenId, focused, direction)) {
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
    const bool forward = (direction == QLatin1String("right") || direction == QLatin1String("down"));
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
    const bool forward = (direction == QLatin1String("right") || direction == QLatin1String("down"));
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
            // Update the per-screen override so the value persists across
            // settings reloads (applyPerScreenConfig uses the stored override).
            m_engine->updatePerScreenOverride(screenId, PerScreenKeys::SplitRatio, resultRatio);
        } else {
            // Update global config so new screens inherit the adjusted ratio.
            m_engine->config()->splitRatio = resultRatio;
            m_engine->syncShortcutAdjustmentToSettings();
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
            m_engine->updatePerScreenOverride(screenId, PerScreenKeys::MasterCount, resultCount);
        } else {
            m_engine->config()->masterCount = resultCount;
            m_engine->syncShortcutAdjustmentToSettings();
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
    auto sit = m_engine->m_screenStates.find(key);
    if (sit == m_engine->m_screenStates.end() || !sit.value()) {
        return nullptr;
    }
    return sit.value();
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
        for (auto it = m_engine->m_screenStates.constBegin(); it != m_engine->m_screenStates.constEnd(); ++it) {
            if (it.key().desktop != m_engine->m_currentDesktop || it.key().activity != m_engine->m_currentActivity) {
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
        auto sit = m_engine->m_screenStates.constFind(key);
        if (sit != m_engine->m_screenStates.constEnd()) {
            PhosphorTiles::TilingState* state = sit.value();
            if (state && !state->focusedWindow().isEmpty()) {
                outScreenId = m_engine->m_activeScreen;
                outState = state;
                return state->tiledWindows();
            }
        }
    }

    // Fallback: scan states for current desktop/activity (e.g., if m_activeScreen is stale)
    for (auto it = m_engine->m_screenStates.constBegin(); it != m_engine->m_screenStates.constEnd(); ++it) {
        if (it.key().desktop != m_engine->m_currentDesktop || it.key().activity != m_engine->m_currentActivity) {
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
        auto sit = m_engine->m_screenStates.constFind(key);
        if (sit != m_engine->m_screenStates.constEnd() && sit.value()) {
            outState = sit.value();
            return sit.value()->tiledWindows();
        }
    }

    outScreenId.clear();
    return {};
}

void NavigationController::applyToAllStates(const std::function<void(PhosphorTiles::TilingState*)>& operation)
{
    if (m_engine->m_screenStates.isEmpty()) {
        return; // No states to modify
    }

    // Only apply to states for the current desktop/activity
    for (auto it = m_engine->m_screenStates.begin(); it != m_engine->m_screenStates.end(); ++it) {
        if (it.key().desktop == m_engine->m_currentDesktop && it.key().activity == m_engine->m_currentActivity
            && it.value()) {
            operation(it.value());
        }
    }

    if (m_engine->isEnabled()) {
        m_engine->retile();
    }
}

} // namespace PhosphorTileEngine
