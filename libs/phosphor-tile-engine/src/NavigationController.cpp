// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTileEngine/NavigationController.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorTiles/AutotileConstants.h>
#include "tileenginelogging.h"
#include <PhosphorScreens/Manager.h>

#include <QScreen>
#include <QRect>
#include <algorithm>
#include <limits>
#include <utility>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorTileEngine {

namespace PerScreenKeys = PhosphorEngine::PerScreenKeys;

namespace {

bool isForwardDirection(const QString& direction)
{
    return direction == QLatin1String("right") || direction == QLatin1String("down");
}

bool isHorizontalDirection(const QString& direction)
{
    return direction == QLatin1String("left") || direction == QLatin1String("right");
}

int entryIndexForDirection(const QString& direction, int windowCount)
{
    if (windowCount <= 0) {
        return 0;
    }
    // Moving right/down enters a target screen/desktop at its first tiled slot;
    // moving left/up enters at the opposite edge.
    return isForwardDirection(direction) ? 0 : windowCount - 1;
}

int axisOverlap(const QRect& a, const QRect& b, bool horizontalNavigation)
{
    if (horizontalNavigation) {
        return qMax(0, qMin(a.bottom(), b.bottom()) - qMax(a.top(), b.top()) + 1);
    }
    return qMax(0, qMin(a.right(), b.right()) - qMax(a.left(), b.left()) + 1);
}

int orthogonalCenterDistance(const QRect& a, const QRect& b, bool horizontalNavigation)
{
    return horizontalNavigation ? qAbs(a.center().y() - b.center().y()) : qAbs(a.center().x() - b.center().x());
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

void NavigationController::swapFocusedInDirection(const QString& direction, const QString& action,
                                                  const QString& explicitWindowId)
{
    const bool forward = isForwardDirection(direction);

    QString screenId;
    PhosphorTiles::TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state, explicitWindowId);

    if (windows.isEmpty() || !state) {
        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("nothing_to_swap"), QString(), QString(),
                                            screenId);
        return;
    }

    const QString focused = !explicitWindowId.isEmpty() ? explicitWindowId : state->focusedWindow();
    if (focused.isEmpty()) {
        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("no_focus"), QString(), QString(), screenId);
        return;
    }

    const int currentIndex = windows.indexOf(focused);
    if (currentIndex < 0) {
        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("no_focus"), QString(), QString(), screenId);
        return;
    }

    const bool atBoundary = forward ? (currentIndex == windows.size() - 1) : (currentIndex == 0);
    if (atBoundary) {
        const QString targetScreen = neighborAutotileScreenInDirection(screenId, direction);
        if (!targetScreen.isEmpty()
            && moveFocusedToBoundaryTarget(focused, screenId, state, targetScreen,
                                           m_engine->currentDesktopForScreen(targetScreen), direction)) {
            Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("screen:%1").arg(direction), QString(),
                                                QString(), targetScreen);
            return;
        }

        const int targetDesktop = neighborDesktopInDirection(screenId, direction);
        if (targetDesktop > 0
            && moveFocusedToBoundaryTarget(focused, screenId, state, screenId, targetDesktop, direction)) {
            Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("desktop:%1").arg(targetDesktop),
                                                QString(), QString(), screenId);
            return;
        }
    }

    if (windows.size() < 2) {
        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("nothing_to_swap"), QString(), QString(),
                                            screenId);
        return;
    }

    int targetIndex = forward ? currentIndex + 1 : currentIndex - 1;
    // Preserve the existing local wrap behavior when there is no boundary target.
    if (targetIndex < 0) {
        targetIndex = windows.size() - 1;
    } else if (targetIndex >= windows.size()) {
        targetIndex = 0;
    }

    const QString targetWindow = windows.at(targetIndex);
    const bool swapped = state->swapWindowsById(focused, targetWindow);
    m_engine->retileAfterOperation(screenId, swapped);

    Q_EMIT m_engine->navigationFeedback(swapped, action, direction, QString(), QString(), screenId);
}

void NavigationController::focusInDirection(const QString& direction, const QString& action,
                                            const QString& explicitWindowId, const QString& explicitScreenId)
{
    const bool forward = isForwardDirection(direction);

    QString screenId;
    PhosphorTiles::TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state, explicitWindowId);

    if (windows.isEmpty() || !state) {
        const QString fallbackScreenId = screenId.isEmpty() ? resolveFallbackScreenId(explicitScreenId) : screenId;

        const QString targetScreen = neighborAutotileScreenInDirection(fallbackScreenId, direction);
        if (!targetScreen.isEmpty()) {
            if (focusBoundaryTarget(targetScreen, m_engine->currentDesktopForScreen(targetScreen), direction, action,
                                    targetScreen)) {
                return;
            }
            if (focusEmptyScreenBoundaryTarget(targetScreen, direction, action)) {
                return;
            }
        }

        const int targetDesktop = neighborDesktopInDirection(fallbackScreenId, direction);
        if (targetDesktop > 0 && !fallbackScreenId.isEmpty()) {
            Q_EMIT m_engine->currentDesktopChangeRequestedForScreen(fallbackScreenId, targetDesktop);
            m_engine->setCurrentDesktopForScreen(fallbackScreenId, targetDesktop);
            if (focusBoundaryTarget(fallbackScreenId, targetDesktop, direction, action, fallbackScreenId)) {
                return;
            }

            // With no tiled window on the source desktop, there is no local
            // boundary index to inspect. Treat the empty desktop itself as
            // the boundary: switch desktops and stop rather than reporting
            // no_windows or wrapping to stale focus state from another context.
            Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("desktop:%1").arg(targetDesktop),
                                                QString(), QString(), fallbackScreenId);
            return;
        }

        Q_EMIT m_engine->navigationFeedback(false, action, QStringLiteral("no_windows"), QString(), QString(),
                                            fallbackScreenId);
        return;
    }

    const QString focused = !explicitWindowId.isEmpty() ? explicitWindowId : state->focusedWindow();
    const int currentIndex = qMax(0, windows.indexOf(focused));
    const bool atBoundary = forward ? (currentIndex == windows.size() - 1) : (currentIndex == 0);
    if (atBoundary) {
        const QString targetScreen = neighborAutotileScreenInDirection(screenId, direction);
        if (!targetScreen.isEmpty()) {
            if (focusBoundaryTarget(targetScreen, m_engine->currentDesktopForScreen(targetScreen), direction, action,
                                    targetScreen)) {
                return;
            }
            if (focusEmptyScreenBoundaryTarget(targetScreen, direction, action)) {
                return;
            }
        }

        const int targetDesktop = neighborDesktopInDirection(screenId, direction);
        if (targetDesktop > 0) {
            Q_EMIT m_engine->currentDesktopChangeRequestedForScreen(screenId, targetDesktop);
            m_engine->setCurrentDesktopForScreen(screenId, targetDesktop);
            if (focusBoundaryTarget(screenId, targetDesktop, direction, action, screenId)) {
                return;
            }

            // Desktop traversal should still cross the
            // boundary when the target desktop currently has no tiled
            // windows. Switch desktops and stop instead of wrapping focus
            // back to another window on the source desktop.
            Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("desktop:%1").arg(targetDesktop),
                                                QString(), QString(), screenId);
            return;
        }
    }

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

QString NavigationController::resolveFallbackScreenId(const QString& explicitScreenId) const
{
    if (!explicitScreenId.isEmpty()) {
        return explicitScreenId;
    }

    const QString activeScreen = resolveActiveScreen();
    if (!activeScreen.isEmpty()) {
        return activeScreen;
    }

    const Phosphor::Screens::PhysicalScreen primaryScreen =
        m_engine->m_screenManager ? m_engine->m_screenManager->primaryScreen() : Phosphor::Screens::PhysicalScreen{};
    return primaryScreen.isValid() ? primaryScreen.identifier : QString();
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
            if (it.key().desktop != m_engine->currentDesktopForScreen(it.key().screenId)
                || it.key().activity != m_engine->m_currentActivity) {
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
        if (it.key().desktop != m_engine->currentDesktopForScreen(it.key().screenId)
            || it.key().activity != m_engine->m_currentActivity) {
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
    const Phosphor::Screens::PhysicalScreen primaryScreen =
        m_engine->m_screenManager ? m_engine->m_screenManager->primaryScreen() : Phosphor::Screens::PhysicalScreen{};
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

QString NavigationController::neighborAutotileScreenInDirection(const QString& sourceScreenId,
                                                                const QString& direction) const
{
    if (!m_engine || !m_engine->m_screenManager || sourceScreenId.isEmpty()) {
        return QString();
    }

    const QRect sourceGeometry = m_engine->screenGeometry(sourceScreenId);
    if (!sourceGeometry.isValid()) {
        return QString();
    }

    const bool horizontal = isHorizontalDirection(direction);
    QString bestScreen;
    int bestDistance = std::numeric_limits<int>::max();
    int bestCenterDistance = std::numeric_limits<int>::max();

    for (const QString& candidate : std::as_const(m_engine->m_autotileScreens)) {
        if (candidate == sourceScreenId) {
            continue;
        }

        const QRect candidateGeometry = m_engine->screenGeometry(candidate);
        if (!candidateGeometry.isValid() || axisOverlap(sourceGeometry, candidateGeometry, horizontal) <= 0) {
            continue;
        }

        int distance = -1;
        if (direction == QLatin1String("right")) {
            if (candidateGeometry.left() <= sourceGeometry.right()) {
                continue;
            }
            distance = candidateGeometry.left() - sourceGeometry.right();
        } else if (direction == QLatin1String("left")) {
            if (candidateGeometry.right() >= sourceGeometry.left()) {
                continue;
            }
            distance = sourceGeometry.left() - candidateGeometry.right();
        } else if (direction == QLatin1String("down")) {
            if (candidateGeometry.top() <= sourceGeometry.bottom()) {
                continue;
            }
            distance = candidateGeometry.top() - sourceGeometry.bottom();
        } else if (direction == QLatin1String("up")) {
            if (candidateGeometry.bottom() >= sourceGeometry.top()) {
                continue;
            }
            distance = sourceGeometry.top() - candidateGeometry.bottom();
        }

        if (distance < 0) {
            continue;
        }

        const int centerDistance = orthogonalCenterDistance(sourceGeometry, candidateGeometry, horizontal);
        if (distance < bestDistance || (distance == bestDistance && centerDistance < bestCenterDistance)) {
            bestDistance = distance;
            bestCenterDistance = centerDistance;
            bestScreen = candidate;
        }
    }

    return bestScreen;
}

int NavigationController::neighborDesktopInDirection(const QString& sourceScreenId, const QString& direction) const
{
    if (!m_engine) {
        return 0;
    }

    const int count = qMax(1, m_engine->m_desktopCount);
    const int rows = qBound(1, m_engine->m_desktopRows, count);
    const int columns = qMax(1, (count + rows - 1) / rows);

    const int sourceDesktop = m_engine->currentDesktopForScreen(sourceScreenId);
    const int sourceIndex = sourceDesktop - 1; // desktop numbers are 1-based
    if (sourceIndex < 0 || sourceIndex >= count) {
        return 0;
    }

    const int sourceRow = sourceIndex / columns;
    const int sourceColumn = sourceIndex % columns;
    int targetIndex = -1;

    if (direction == QLatin1String("left")) {
        if (sourceColumn == 0) {
            return 0;
        }
        targetIndex = sourceIndex - 1;
    } else if (direction == QLatin1String("right")) {
        if (sourceColumn == columns - 1) {
            return 0;
        }
        targetIndex = sourceIndex + 1;
    } else if (direction == QLatin1String("up")) {
        if (sourceRow == 0) {
            return 0;
        }
        targetIndex = sourceIndex - columns;
    } else if (direction == QLatin1String("down")) {
        if (sourceRow == rows - 1) {
            return 0;
        }
        targetIndex = sourceIndex + columns;
    }

    if (targetIndex < 0 || targetIndex >= count) {
        return 0;
    }

    // Avoid horizontal movement wrapping across rows on partial grids.
    if ((direction == QLatin1String("left") || direction == QLatin1String("right"))
        && targetIndex / columns != sourceRow) {
        return 0;
    }

    return targetIndex + 1;
}

bool NavigationController::moveFocusedToBoundaryTarget(const QString& focused, const QString& sourceScreenId,
                                                       PhosphorTiles::TilingState* sourceState,
                                                       const QString& targetScreenId, int targetDesktop,
                                                       const QString& direction)
{
    if (!m_engine || focused.isEmpty() || !sourceState || targetScreenId.isEmpty() || targetDesktop < 1) {
        return false;
    }

    PhosphorEngine::TilingStateKey targetKey{targetScreenId, targetDesktop, m_engine->m_currentActivity};
    PhosphorTiles::TilingState* targetState = nullptr;
    if (targetDesktop == m_engine->currentDesktopForScreen(targetScreenId)) {
        targetState = m_engine->tilingStateForScreen(targetScreenId);
    } else {
        targetState = m_engine->stateForKey(targetKey);
    }
    if (!targetState) {
        return false;
    }

    const int sourceDesktop = m_engine->currentDesktopForScreen(sourceScreenId);
    const bool crossDesktop = targetDesktop != sourceDesktop;
    const bool crossScreen = targetScreenId != sourceScreenId;
    const int insertPosition = isForwardDirection(direction) ? 0 : -1;

    if (!sourceState->removeWindow(focused)) {
        return false;
    }
    if (!targetState->addWindow(focused, insertPosition)) {
        sourceState->addWindow(focused);
        return false;
    }

    m_engine->m_windowToStateKey[focused] = targetKey;
    targetState->setFocusedWindow(focused);
    m_engine->m_activeScreen = targetScreenId;

    m_engine->retileAfterOperation(sourceScreenId, true);
    if (crossDesktop) {
        Q_EMIT m_engine->windowDesktopMoveRequested(focused, targetDesktop);
        if (!crossScreen) {
            Q_EMIT m_engine->currentDesktopChangeRequestedForScreen(sourceScreenId, targetDesktop);
            m_engine->setCurrentDesktopForScreen(sourceScreenId, targetDesktop);
            // The daemon's desktop-change path will retile the new desktop after
            // the compositor moves the window's desktop membership.
        } else {
            // Per-output desktop semantics: crossing onto another output targets
            // that output's already-visible desktop. There is no desktop switch
            // to wait for, so retile the destination surface immediately.
            m_engine->retileAfterOperation(targetScreenId, true);
        }
    } else {
        m_engine->retileAfterOperation(targetScreenId, true);
    }

    Q_EMIT m_engine->activateWindowRequested(focused);
    return true;
}

bool NavigationController::focusBoundaryTarget(const QString& targetScreenId, int targetDesktop,
                                               const QString& direction, const QString& action,
                                               const QString& feedbackScreenId)
{
    if (!m_engine || targetScreenId.isEmpty() || targetDesktop < 1) {
        return false;
    }

    const PhosphorEngine::TilingStateKey targetKey{targetScreenId, targetDesktop, m_engine->m_currentActivity};
    auto it = m_engine->m_screenStates.constFind(targetKey);
    if (it == m_engine->m_screenStates.constEnd() || !it.value()) {
        return false;
    }

    const QStringList targetWindows = it.value()->tiledWindows();
    if (targetWindows.isEmpty()) {
        return false;
    }

    const int targetIndex = entryIndexForDirection(direction, targetWindows.size());
    Q_EMIT m_engine->activateWindowRequested(targetWindows.at(targetIndex));
    Q_EMIT m_engine->navigationFeedback(true, action,
                                        targetDesktop == m_engine->currentDesktopForScreen(targetScreenId)
                                            ? QStringLiteral("screen:%1").arg(direction)
                                            : QStringLiteral("desktop:%1").arg(targetDesktop),
                                        QString(), QString(), feedbackScreenId);
    return true;
}

bool NavigationController::focusEmptyScreenBoundaryTarget(const QString& targetScreenId, const QString& direction,
                                                          const QString& action)
{
    if (!m_engine || targetScreenId.isEmpty()) {
        return false;
    }

    // Outputs have priority over virtual desktops at a boundary. If the
    // neighboring output has no tiled windows on its visible desktop, remember
    // it as the active navigation surface and stop instead of falling through
    // to a virtual-desktop transition on the source output. There is no KWin
    // window to activate in this case.
    m_engine->m_activeScreen = targetScreenId;
    Q_EMIT m_engine->navigationFeedback(true, action, QStringLiteral("screen:%1").arg(direction), QString(), QString(),
                                        targetScreenId);
    return true;
}

void NavigationController::applyToAllStates(const std::function<void(PhosphorTiles::TilingState*)>& operation)
{
    if (m_engine->m_screenStates.isEmpty()) {
        return; // No states to modify
    }

    // Only apply to states for the current desktop/activity
    for (auto it = m_engine->m_screenStates.begin(); it != m_engine->m_screenStates.end(); ++it) {
        if (it.key().desktop == m_engine->currentDesktopForScreen(it.key().screenId)
            && it.key().activity == m_engine->m_currentActivity && it.value()) {
            operation(it.value());
        }
    }

    if (m_engine->isEnabled()) {
        m_engine->retile();
    }
}

} // namespace PhosphorTileEngine
