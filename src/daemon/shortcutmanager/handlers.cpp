// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../shortcutmanager.h"
#include "../config/settings.h"
#include "../../core/logging.h"
#include <KGlobalAccel>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Macros
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Update a global shortcut from settings
 * @param actionMember Pointer to QAction* member variable
 * @param settingsGetter Settings method to get the shortcut string
 */
#define UPDATE_SHORTCUT(actionMember, settingsGetter)                                                                  \
    do {                                                                                                               \
        if (m_registrationInProgress) {                                                                                \
            m_settingsDirty = true;                                                                                    \
            return;                                                                                                    \
        }                                                                                                              \
        if (actionMember) {                                                                                            \
            KGlobalAccel::setGlobalShortcut(actionMember, QKeySequence(m_settings->settingsGetter()));                 \
        }                                                                                                              \
    } while (0)

/**
 * @brief Directional pass-through: log + emit signal with direction enum
 * @param prefix Handler name prefix (e.g., MoveWindow)
 * @param signal Signal to emit (e.g., moveWindowRequested)
 * @param dirStr Direction string suffix (e.g., Left)
 * @param dir NavigationDirection enum value (e.g., Left)
 */
#define DIRECTION_HANDLER(prefix, signal, dirStr, dir)                                                                 \
    void ShortcutManager::on##prefix##dirStr()                                                                         \
    {                                                                                                                  \
        qCInfo(lcShortcuts) << #prefix " " #dirStr " triggered";                                                       \
        Q_EMIT signal(NavigationDirection::dir);                                                                       \
    }

/**
 * @brief Directional update: calls UPDATE_SHORTCUT for a directional action
 * @param prefix Handler name prefix (e.g., MoveWindow)
 * @param dirStr Direction string suffix (e.g., Left)
 * @param member Action member variable (e.g., m_moveWindowLeftAction)
 * @param getter Settings getter method (e.g., moveWindowLeftShortcut)
 */
#define DIRECTION_UPDATE(prefix, dirStr, member, getter)                                                               \
    void ShortcutManager::update##prefix##dirStr##Shortcut()                                                           \
    {                                                                                                                  \
        UPDATE_SHORTCUT(member, getter);                                                                               \
    }

// ═══════════════════════════════════════════════════════════════════════════════
// Core Shortcut Handlers
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::onOpenEditor()
{
    Q_EMIT openEditorRequested();
}

void ShortcutManager::onPreviousLayout()
{
    Q_EMIT previousLayoutRequested();
}

void ShortcutManager::onNextLayout()
{
    Q_EMIT nextLayoutRequested();
}

void ShortcutManager::onQuickLayout(int number)
{
    qCInfo(lcShortcuts) << "Quick layout shortcut triggered for slot" << number;
    Q_EMIT quickLayoutRequested(number);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation Slot Handlers (DRY directional macros)
// ═══════════════════════════════════════════════════════════════════════════════

// Move Window directional handlers
DIRECTION_HANDLER(MoveWindow, moveWindowRequested, Left, Left)
DIRECTION_HANDLER(MoveWindow, moveWindowRequested, Right, Right)
DIRECTION_HANDLER(MoveWindow, moveWindowRequested, Up, Up)
DIRECTION_HANDLER(MoveWindow, moveWindowRequested, Down, Down)

// Focus Zone directional handlers
DIRECTION_HANDLER(FocusZone, focusZoneRequested, Left, Left)
DIRECTION_HANDLER(FocusZone, focusZoneRequested, Right, Right)
DIRECTION_HANDLER(FocusZone, focusZoneRequested, Up, Up)
DIRECTION_HANDLER(FocusZone, focusZoneRequested, Down, Down)

// Non-directional navigation handlers
void ShortcutManager::onPushToEmptyZone()
{
    qCInfo(lcShortcuts) << "Push to empty zone triggered";
    Q_EMIT pushToEmptyZoneRequested();
}

void ShortcutManager::onRestoreWindowSize()
{
    qCInfo(lcShortcuts) << "Restore window size triggered";
    Q_EMIT restoreWindowSizeRequested();
}

void ShortcutManager::onToggleWindowFloat()
{
    qCInfo(lcShortcuts) << "Toggle window float triggered";
    Q_EMIT toggleWindowFloatRequested();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Swap Window Slot Handlers (DRY directional macros)
// ═══════════════════════════════════════════════════════════════════════════════

DIRECTION_HANDLER(SwapWindow, swapWindowRequested, Left, Left)
DIRECTION_HANDLER(SwapWindow, swapWindowRequested, Right, Right)
DIRECTION_HANDLER(SwapWindow, swapWindowRequested, Up, Up)
DIRECTION_HANDLER(SwapWindow, swapWindowRequested, Down, Down)

// ═══════════════════════════════════════════════════════════════════════════════
// Snap to Zone Slot Handlers
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::onSnapToZone(int zoneNumber)
{
    qCInfo(lcShortcuts) << "Snap to zone" << zoneNumber << "triggered";
    Q_EMIT snapToZoneRequested(zoneNumber);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Rotate Windows Slot Handlers
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::onRotateWindowsClockwise()
{
    qCInfo(lcShortcuts) << "Rotate windows clockwise triggered";
    Q_EMIT rotateWindowsRequested(true);
}

void ShortcutManager::onRotateWindowsCounterclockwise()
{
    qCInfo(lcShortcuts) << "Rotate windows counterclockwise triggered";
    Q_EMIT rotateWindowsRequested(false);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cycle Windows Slot Handlers
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::onCycleWindowForward()
{
    qCInfo(lcShortcuts) << "Cycle window forward triggered";
    Q_EMIT cycleWindowsInZoneRequested(true);
}

void ShortcutManager::onCycleWindowBackward()
{
    qCInfo(lcShortcuts) << "Cycle window backward triggered";
    Q_EMIT cycleWindowsInZoneRequested(false);
}

void ShortcutManager::onResnapToNewLayout()
{
    qCInfo(lcShortcuts) << "Resnap to new layout triggered";
    Q_EMIT resnapToNewLayoutRequested();
}

void ShortcutManager::onSnapAllWindows()
{
    qCInfo(lcShortcuts) << "Snap all windows triggered";
    Q_EMIT snapAllWindowsRequested();
}

void ShortcutManager::onLayoutPicker()
{
    qCInfo(lcShortcuts) << "Layout picker triggered";
    Q_EMIT layoutPickerRequested();
}

void ShortcutManager::onToggleLayoutLock()
{
    qCInfo(lcShortcuts) << "Toggle layout lock triggered";
    Q_EMIT toggleLayoutLockRequested();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Update Shortcut Methods
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::updateEditorShortcut()
{
    UPDATE_SHORTCUT(m_editorAction, openEditorShortcut);
}

void ShortcutManager::updatePreviousLayoutShortcut()
{
    UPDATE_SHORTCUT(m_previousLayoutAction, previousLayoutShortcut);
}

void ShortcutManager::updateNextLayoutShortcut()
{
    UPDATE_SHORTCUT(m_nextLayoutAction, nextLayoutShortcut);
}

void ShortcutManager::updateQuickLayoutShortcut(int index)
{
    if (m_registrationInProgress) {
        m_settingsDirty = true;
        return;
    }
    if (index >= 0 && index < m_quickLayoutActions.size()) {
        KGlobalAccel::setGlobalShortcut(m_quickLayoutActions[index],
                                        QKeySequence(m_settings->quickLayoutShortcut(index)));
    }
}

// Move Window directional updates
DIRECTION_UPDATE(MoveWindow, Left, m_moveWindowLeftAction, moveWindowLeftShortcut)
DIRECTION_UPDATE(MoveWindow, Right, m_moveWindowRightAction, moveWindowRightShortcut)
DIRECTION_UPDATE(MoveWindow, Up, m_moveWindowUpAction, moveWindowUpShortcut)
DIRECTION_UPDATE(MoveWindow, Down, m_moveWindowDownAction, moveWindowDownShortcut)

// Focus Zone directional updates
DIRECTION_UPDATE(FocusZone, Left, m_focusZoneLeftAction, focusZoneLeftShortcut)
DIRECTION_UPDATE(FocusZone, Right, m_focusZoneRightAction, focusZoneRightShortcut)
DIRECTION_UPDATE(FocusZone, Up, m_focusZoneUpAction, focusZoneUpShortcut)
DIRECTION_UPDATE(FocusZone, Down, m_focusZoneDownAction, focusZoneDownShortcut)

// Non-directional navigation updates
void ShortcutManager::updatePushToEmptyZoneShortcut()
{
    UPDATE_SHORTCUT(m_pushToEmptyZoneAction, pushToEmptyZoneShortcut);
}

void ShortcutManager::updateRestoreWindowSizeShortcut()
{
    UPDATE_SHORTCUT(m_restoreWindowSizeAction, restoreWindowSizeShortcut);
}

void ShortcutManager::updateToggleWindowFloatShortcut()
{
    UPDATE_SHORTCUT(m_toggleWindowFloatAction, toggleWindowFloatShortcut);
}

// Swap Window directional updates
DIRECTION_UPDATE(SwapWindow, Left, m_swapWindowLeftAction, swapWindowLeftShortcut)
DIRECTION_UPDATE(SwapWindow, Right, m_swapWindowRightAction, swapWindowRightShortcut)
DIRECTION_UPDATE(SwapWindow, Up, m_swapWindowUpAction, swapWindowUpShortcut)
DIRECTION_UPDATE(SwapWindow, Down, m_swapWindowDownAction, swapWindowDownShortcut)

// Snap to Zone update
void ShortcutManager::updateSnapToZoneShortcut(int index)
{
    if (m_registrationInProgress) {
        m_settingsDirty = true;
        return;
    }
    if (index >= 0 && index < m_snapToZoneActions.size()) {
        KGlobalAccel::setGlobalShortcut(m_snapToZoneActions[index],
                                        QKeySequence(m_settings->snapToZoneShortcut(index)));
    }
}

// Rotate Windows updates
void ShortcutManager::updateRotateWindowsClockwiseShortcut()
{
    UPDATE_SHORTCUT(m_rotateWindowsClockwiseAction, rotateWindowsClockwiseShortcut);
}

void ShortcutManager::updateRotateWindowsCounterclockwiseShortcut()
{
    UPDATE_SHORTCUT(m_rotateWindowsCounterclockwiseAction, rotateWindowsCounterclockwiseShortcut);
}

// Cycle Windows updates
void ShortcutManager::updateCycleWindowForwardShortcut()
{
    UPDATE_SHORTCUT(m_cycleWindowForwardAction, cycleWindowForwardShortcut);
}

void ShortcutManager::updateCycleWindowBackwardShortcut()
{
    UPDATE_SHORTCUT(m_cycleWindowBackwardAction, cycleWindowBackwardShortcut);
}

// Resnap / Snap All / Layout Picker updates
void ShortcutManager::updateResnapToNewLayoutShortcut()
{
    UPDATE_SHORTCUT(m_resnapToNewLayoutAction, resnapToNewLayoutShortcut);
}

void ShortcutManager::updateSnapAllWindowsShortcut()
{
    UPDATE_SHORTCUT(m_snapAllWindowsAction, snapAllWindowsShortcut);
}

void ShortcutManager::updateLayoutPickerShortcut()
{
    UPDATE_SHORTCUT(m_layoutPickerAction, layoutPickerShortcut);
}

void ShortcutManager::updateToggleLayoutLockShortcut()
{
    UPDATE_SHORTCUT(m_toggleLayoutLockAction, toggleLayoutLockShortcut);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Autotile Slot Handlers
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::onToggleAutotile()
{
    Q_EMIT toggleAutotileRequested();
}
void ShortcutManager::onFocusMaster()
{
    Q_EMIT focusMasterRequested();
}
void ShortcutManager::onSwapWithMaster()
{
    Q_EMIT swapWithMasterRequested();
}
void ShortcutManager::onIncreaseMasterRatio()
{
    Q_EMIT increaseMasterRatioRequested();
}
void ShortcutManager::onDecreaseMasterRatio()
{
    Q_EMIT decreaseMasterRatioRequested();
}
void ShortcutManager::onIncreaseMasterCount()
{
    Q_EMIT increaseMasterCountRequested();
}
void ShortcutManager::onDecreaseMasterCount()
{
    Q_EMIT decreaseMasterCountRequested();
}
void ShortcutManager::onRetile()
{
    Q_EMIT retileRequested();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Autotile Update Shortcut Methods
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::updateToggleAutotileShortcut()
{
    UPDATE_SHORTCUT(m_toggleAutotileAction, autotileToggleShortcut);
}
void ShortcutManager::updateFocusMasterShortcut()
{
    UPDATE_SHORTCUT(m_focusMasterAction, autotileFocusMasterShortcut);
}
void ShortcutManager::updateSwapMasterShortcut()
{
    UPDATE_SHORTCUT(m_swapMasterAction, autotileSwapMasterShortcut);
}
void ShortcutManager::updateIncMasterRatioShortcut()
{
    UPDATE_SHORTCUT(m_incMasterRatioAction, autotileIncMasterRatioShortcut);
}
void ShortcutManager::updateDecMasterRatioShortcut()
{
    UPDATE_SHORTCUT(m_decMasterRatioAction, autotileDecMasterRatioShortcut);
}
void ShortcutManager::updateIncMasterCountShortcut()
{
    UPDATE_SHORTCUT(m_incMasterCountAction, autotileIncMasterCountShortcut);
}
void ShortcutManager::updateDecMasterCountShortcut()
{
    UPDATE_SHORTCUT(m_decMasterCountAction, autotileDecMasterCountShortcut);
}
void ShortcutManager::updateRetileShortcut()
{
    UPDATE_SHORTCUT(m_retileAction, autotileRetileShortcut);
}

// Undefine macros to keep them local to this TU
#undef UPDATE_SHORTCUT
#undef DIRECTION_HANDLER
#undef DIRECTION_UPDATE

} // namespace PlasmaZones
