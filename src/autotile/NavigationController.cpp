// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "NavigationController.h"
#include "AutotileEngine.h"
#include "AutotileConfig.h"
#include "TilingState.h"
#include "core/constants.h"
#include "core/logging.h"
#include "core/screenmanager.h"
#include "core/utils.h"
#include "core/windowtrackingservice.h"

#include <QScreen>
#include <algorithm>

namespace PlasmaZones {

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
    TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state);
    if (windows.isEmpty()) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, QStringLiteral("focus_master"),
                                                     QStringLiteral("no_windows"), QString(), QString(), screenId);
        return;
    }
    emitFocusRequestAtIndex(0, true);
    Q_EMIT m_engine->navigationFeedbackRequested(true, QStringLiteral("focus_master"), QStringLiteral("master"),
                                                 QString(), QString(), screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window swapping & rotation
// ═══════════════════════════════════════════════════════════════════════════════

void NavigationController::swapFocusedWithMaster()
{
    QString screenId;
    TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state);

    if (windows.isEmpty() || !state) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, QStringLiteral("swap_master"), QStringLiteral("no_windows"),
                                                     QString(), QString(), screenId);
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, QStringLiteral("swap_master"), QStringLiteral("no_focus"),
                                                     QString(), QString(), screenId);
        return;
    }

    // Locked windows cannot be swapped with master
    if (isWindowLocked(focused)) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, QStringLiteral("swap_master"),
                                                     QStringLiteral("window_locked"), QString(), QString(), screenId);
        return;
    }

    // Cannot displace a locked master window
    if (!windows.isEmpty() && isWindowLocked(windows.first())) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, QStringLiteral("swap_master"),
                                                     QStringLiteral("master_locked"), QString(), QString(), screenId);
        return;
    }

    const bool promoted = state->moveToTiledPosition(focused, 0);
    m_engine->retileAfterOperation(screenId, promoted);

    if (promoted) {
        Q_EMIT m_engine->navigationFeedbackRequested(true, QStringLiteral("swap_master"), QStringLiteral("master"),
                                                     QString(), QString(), screenId);
    } else {
        Q_EMIT m_engine->navigationFeedbackRequested(false, QStringLiteral("swap_master"),
                                                     QStringLiteral("already_master"), QString(), QString(), screenId);
    }
}

void NavigationController::rotateWindowOrder(bool clockwise)
{
    QString screenId;
    TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state);

    if (windows.size() < 2 || !state) {
        Q_EMIT m_engine->navigationFeedbackRequested(
            false, QStringLiteral("rotate"), QStringLiteral("nothing_to_rotate"), QString(), QString(), screenId);
        return; // Nothing to rotate with 0 or 1 window
    }

    // Count non-locked windows — need at least 2 unlocked to rotate
    int unlockedCount = 0;
    for (const QString& w : windows) {
        if (!isWindowLocked(w)) {
            ++unlockedCount;
        }
    }
    if (unlockedCount < 2) {
        Q_EMIT m_engine->navigationFeedbackRequested(
            false, QStringLiteral("rotate"), QStringLiteral("nothing_to_rotate"), QString(), QString(), screenId);
        return;
    }

    // Rotate only unlocked windows while locked windows keep their positions
    bool rotated = state->rotateWindowsExcluding(clockwise, [this](const QString& wId) {
        return isWindowLocked(wId);
    });
    m_engine->retileAfterOperation(screenId, rotated);

    if (rotated) {
        QString reason = QStringLiteral("%1:%2")
                             .arg(clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise"))
                             .arg(windows.size());
        Q_EMIT m_engine->navigationFeedbackRequested(true, QStringLiteral("rotate"), reason, QString(), QString(),
                                                     screenId);
    } else {
        Q_EMIT m_engine->navigationFeedbackRequested(false, QStringLiteral("rotate"), QStringLiteral("no_rotations"),
                                                     QString(), QString(), screenId);
    }

    qCInfo(lcAutotile) << "Rotated windows" << (clockwise ? "clockwise" : "counterclockwise");
}

void NavigationController::swapFocusedInDirection(const QString& direction, const QString& action)
{
    const bool forward = (direction == QLatin1String("right") || direction == QLatin1String("down"));

    QString screenId;
    TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state);

    if (windows.size() < 2 || !state) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, action, QStringLiteral("nothing_to_swap"), QString(),
                                                     QString(), screenId);
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, action, QStringLiteral("no_focus"), QString(), QString(),
                                                     screenId);
        return;
    }

    // Locked windows cannot be swapped
    if (isWindowLocked(focused)) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, action, QStringLiteral("window_locked"), QString(),
                                                     QString(), screenId);
        return;
    }

    const int currentIndex = windows.indexOf(focused);
    if (currentIndex < 0) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, action, QStringLiteral("no_focus"), QString(), QString(),
                                                     screenId);
        return;
    }

    // Find the next non-locked target, skipping locked windows
    int targetIndex = currentIndex;
    const int count = windows.size();
    for (int i = 0; i < count - 1; ++i) {
        targetIndex = forward ? targetIndex + 1 : targetIndex - 1;
        if (targetIndex < 0) {
            targetIndex = count - 1;
        } else if (targetIndex >= count) {
            targetIndex = 0;
        }
        if (!isWindowLocked(windows.at(targetIndex))) {
            break;
        }
        // If we've looped back to start, all targets are locked
        if (targetIndex == currentIndex) {
            Q_EMIT m_engine->navigationFeedbackRequested(false, action, QStringLiteral("all_locked"), QString(),
                                                         QString(), screenId);
            return;
        }
    }

    const QString targetWindow = windows.at(targetIndex);
    if (isWindowLocked(targetWindow)) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, action, QStringLiteral("all_locked"), QString(), QString(),
                                                     screenId);
        return;
    }

    const bool swapped = state->swapWindowsById(focused, targetWindow);
    m_engine->retileAfterOperation(screenId, swapped);

    Q_EMIT m_engine->navigationFeedbackRequested(swapped, action, direction, QString(), QString(), screenId);
}

void NavigationController::focusInDirection(const QString& direction, const QString& action)
{
    const bool forward = (direction == QLatin1String("right") || direction == QLatin1String("down"));

    QString screenId;
    TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state);

    if (windows.isEmpty() || !state) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, action, QStringLiteral("no_windows"), QString(), QString(),
                                                     screenId);
        return;
    }

    const QString focused = state->focusedWindow();
    const int currentIndex = qMax(0, windows.indexOf(focused));
    const int targetIndex = (currentIndex + (forward ? 1 : -1) + windows.size()) % windows.size();

    Q_EMIT m_engine->focusWindowRequested(windows.at(targetIndex));
    Q_EMIT m_engine->navigationFeedbackRequested(true, action, direction, QString(), QString(), screenId);
}

void NavigationController::moveFocusedToPosition(int position)
{
    QString screenId;
    TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenId, state);

    if (windows.isEmpty() || !state) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, QStringLiteral("snap"), QStringLiteral("no_windows"),
                                                     QString(), QString(), screenId);
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, QStringLiteral("snap"), QStringLiteral("no_focus"),
                                                     QString(), QString(), screenId);
        return;
    }

    // Locked windows cannot be moved to a different position
    if (isWindowLocked(focused)) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, QStringLiteral("snap"), QStringLiteral("window_locked"),
                                                     QString(), QString(), screenId);
        return;
    }

    // position is 1-based (from snap-to-zone-N shortcuts), convert to 0-based
    const int targetIndex = qBound(0, position - 1, windows.size() - 1);

    // Cannot displace a locked window at the target position
    if (isWindowLocked(windows.at(targetIndex))) {
        Q_EMIT m_engine->navigationFeedbackRequested(false, QStringLiteral("snap"), QStringLiteral("target_locked"),
                                                     QString(), QString(), screenId);
        return;
    }

    const bool moved = state->moveToTiledPosition(focused, targetIndex);
    m_engine->retileAfterOperation(screenId, moved);

    if (moved) {
        Q_EMIT m_engine->navigationFeedbackRequested(
            true, QStringLiteral("snap"), QStringLiteral("position_%1").arg(position), QString(), QString(), screenId);
    } else {
        Q_EMIT m_engine->navigationFeedbackRequested(
            false, QStringLiteral("snap"), QStringLiteral("already_at_position"), QString(), QString(), screenId);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Split ratio adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void NavigationController::increaseMasterRatio(qreal delta)
{
    qreal resultRatio = m_engine->config()->splitRatio;
    applyToAllStates([delta, &resultRatio](TilingState* state) {
        // setSplitRatio handles clamping internally
        state->setSplitRatio(state->splitRatio() + delta);
        resultRatio = state->splitRatio(); // capture clamped result
    });

    // Sync config so propagateGlobalSplitRatio() won't overwrite with stale value
    m_engine->config()->splitRatio = resultRatio;
    m_engine->syncShortcutAdjustmentToSettings();

    const QString screenId = resolveActiveScreen();
    QString reason = delta >= 0 ? QStringLiteral("increased") : QStringLiteral("decreased");
    Q_EMIT m_engine->navigationFeedbackRequested(true, QStringLiteral("master_ratio"), reason, QString(), QString(),
                                                 screenId);
}

void NavigationController::decreaseMasterRatio(qreal delta)
{
    increaseMasterRatio(-delta);
}

void NavigationController::setGlobalSplitRatio(qreal ratio)
{
    ratio = std::clamp(ratio, AutotileDefaults::MinSplitRatio, AutotileDefaults::MaxSplitRatio);
    m_engine->config()->splitRatio = ratio;
    applyToAllStates([ratio](TilingState* state) {
        state->setSplitRatio(ratio);
    });
}

void NavigationController::setGlobalMasterCount(int count)
{
    count = std::clamp(count, AutotileDefaults::MinMasterCount, AutotileDefaults::MaxMasterCount);
    m_engine->config()->masterCount = count;
    applyToAllStates([count](TilingState* state) {
        state->setMasterCount(count);
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// Master count adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void NavigationController::increaseMasterCount()
{
    int resultCount = m_engine->config()->masterCount;
    applyToAllStates([&resultCount](TilingState* state) {
        state->setMasterCount(state->masterCount() + 1);
        resultCount = state->masterCount();
    });
    m_engine->config()->masterCount = resultCount;
    m_engine->syncShortcutAdjustmentToSettings();

    const QString screenId = resolveActiveScreen();
    Q_EMIT m_engine->navigationFeedbackRequested(true, QStringLiteral("master_count"), QStringLiteral("increased"),
                                                 QString(), QString(), screenId);
}

void NavigationController::decreaseMasterCount()
{
    int resultCount = m_engine->config()->masterCount;
    applyToAllStates([&resultCount](TilingState* state) {
        if (state->masterCount() > 1) {
            state->setMasterCount(state->masterCount() - 1);
            resultCount = state->masterCount();
        }
    });
    m_engine->config()->masterCount = resultCount;
    m_engine->syncShortcutAdjustmentToSettings();

    const QString screenId = resolveActiveScreen();
    Q_EMIT m_engine->navigationFeedbackRequested(true, QStringLiteral("master_count"), QStringLiteral("decreased"),
                                                 QString(), QString(), screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private helpers
// ═══════════════════════════════════════════════════════════════════════════════

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
    TilingState* state = nullptr;
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

    Q_EMIT m_engine->focusWindowRequested(windows.at(targetIndex));
}

QStringList NavigationController::tiledWindowsForFocusedScreen(QString& outScreenId, TilingState*& outState)
{
    outState = nullptr;

    // Use the tracked active screen (set by onWindowFocused) to avoid
    // non-deterministic QHash iteration when multiple screens have focused windows
    if (!m_engine->m_activeScreen.isEmpty()) {
        const auto key = m_engine->currentKeyForScreen(m_engine->m_activeScreen);
        auto sit = m_engine->m_screenStates.constFind(key);
        if (sit != m_engine->m_screenStates.constEnd()) {
            TilingState* state = sit.value();
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
        TilingState* state = it.value();
        if (state && !state->focusedWindow().isEmpty()) {
            outScreenId = it.key().screenId;
            outState = state;
            return state->tiledWindows();
        }
    }

    // No focused window found - fallback to primary screen if available
    if (m_engine->m_screenManager && m_engine->m_screenManager->primaryScreen()) {
        outScreenId = Utils::screenIdentifier(m_engine->m_screenManager->primaryScreen());
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

bool NavigationController::isWindowLocked(const QString& windowId) const
{
    return m_engine->m_windowTracker && m_engine->m_windowTracker->isWindowLocked(windowId);
}

void NavigationController::applyToAllStates(const std::function<void(TilingState*)>& operation)
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

} // namespace PlasmaZones
