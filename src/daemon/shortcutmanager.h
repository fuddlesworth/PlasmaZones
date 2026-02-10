// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QAction>

namespace PlasmaZones {

class Settings;
class LayoutManager;

/**
 * @brief Navigation direction for keyboard navigation
 */
enum class NavigationDirection {
    Left,
    Right,
    Up,
    Down
};

/**
 * @brief Manages global keyboard shortcuts
 *
 * Handles registration and management of all global shortcuts
 * for PlasmaZones.
 * separating shortcut management from other daemon concerns.
 */
class ShortcutManager : public QObject
{
    Q_OBJECT

public:
    explicit ShortcutManager(Settings* settings, LayoutManager* layoutManager, QObject* parent = nullptr);
    ~ShortcutManager() override;

    /**
     * @brief Initialize and register shortcuts
     */
    void registerShortcuts();

    /**
     * @brief Update shortcuts when settings change
     */
    void updateShortcuts();

    /**
     * @brief Clear all registered shortcuts
     */
    void unregisterShortcuts();

Q_SIGNALS:
    /**
     * @brief Emitted when editor shortcut is triggered
     */
    void openEditorRequested();

    /**
     * @brief Emitted when previous layout shortcut is triggered
     */
    void previousLayoutRequested();

    /**
     * @brief Emitted when next layout shortcut is triggered
     */
    void nextLayoutRequested();

    /**
     * @brief Emitted when quick layout shortcut is triggered
     * @param number Layout number (1-9)
     */
    void quickLayoutRequested(int number);

    // Keyboard navigation signals
    /**
     * @brief Emitted when move window to adjacent zone is requested
     * @param direction Navigation direction (Left, Right, Up, Down)
     */
    void moveWindowRequested(NavigationDirection direction);

    /**
     * @brief Emitted when focus navigation to adjacent zone is requested
     * @param direction Navigation direction (Left, Right, Up, Down)
     */
    void focusZoneRequested(NavigationDirection direction);

    /**
     * @brief Emitted when push window to empty zone is requested
     */
    void pushToEmptyZoneRequested();

    /**
     * @brief Emitted when restore window size is requested
     */
    void restoreWindowSizeRequested();

    /**
     * @brief Emitted when toggle window float is requested
     */
    void toggleWindowFloatRequested();

    /**
     * @brief Emitted when swap window with adjacent zone is requested
     * @param direction Navigation direction (Left, Right, Up, Down)
     */
    void swapWindowRequested(NavigationDirection direction);

    /**
     * @brief Emitted when snap to zone by number is requested
     * @param zoneNumber Zone number (1-9)
     */
    void snapToZoneRequested(int zoneNumber);

    /**
     * @brief Emitted when rotate windows is requested
     * @param clockwise true for clockwise, false for counterclockwise
     */
    void rotateWindowsRequested(bool clockwise);

    /**
     * @brief Emitted when cycle windows in zone is requested
     * @param forward true for forward/next, false for backward/previous
     */
    void cycleWindowsInZoneRequested(bool forward);

    /**
     * @brief Emitted when resnap to new layout is requested
     *
     * Resnaps all windows from the previous layout to the current layout
     * (by zone number with cycling when fewer zones).
     */
    void resnapToNewLayoutRequested();

    /**
     * @brief Emitted when snap all windows shortcut is triggered
     *
     * Snaps all visible unsnapped windows on the current screen to zones.
     */
    void snapAllWindowsRequested();

    /**
     * @brief Emitted when promote to master shortcut is triggered (#106)
     */
    void promoteToMasterRequested();

    /**
     * @brief Emitted when increase master ratio shortcut is triggered (#107)
     */
    void increaseMasterRatioRequested();

    /**
     * @brief Emitted when decrease master ratio shortcut is triggered (#107)
     */
    void decreaseMasterRatioRequested();

private Q_SLOTS:
    void onOpenEditor();
    void onPreviousLayout();
    void onNextLayout();
    void onQuickLayout(int number);
    void updateEditorShortcut();
    void updatePreviousLayoutShortcut();
    void updateNextLayoutShortcut();
    void updateQuickLayoutShortcut(int index);

    // Phase 1 Keyboard Navigation slots
    void onMoveWindowLeft();
    void onMoveWindowRight();
    void onMoveWindowUp();
    void onMoveWindowDown();
    void onFocusZoneLeft();
    void onFocusZoneRight();
    void onFocusZoneUp();
    void onFocusZoneDown();
    void onPushToEmptyZone();
    void onRestoreWindowSize();
    void onToggleWindowFloat();

    // Swap window slots
    void onSwapWindowLeft();
    void onSwapWindowRight();
    void onSwapWindowUp();
    void onSwapWindowDown();

    // Update navigation shortcuts from settings
    void updateMoveWindowLeftShortcut();
    void updateMoveWindowRightShortcut();
    void updateMoveWindowUpShortcut();
    void updateMoveWindowDownShortcut();
    void updateFocusZoneLeftShortcut();
    void updateFocusZoneRightShortcut();
    void updateFocusZoneUpShortcut();
    void updateFocusZoneDownShortcut();
    void updatePushToEmptyZoneShortcut();
    void updateRestoreWindowSizeShortcut();
    void updateToggleWindowFloatShortcut();

    // Update swap window shortcuts from settings
    void updateSwapWindowLeftShortcut();
    void updateSwapWindowRightShortcut();
    void updateSwapWindowUpShortcut();
    void updateSwapWindowDownShortcut();

    // Snap to Zone by Number
    void onSnapToZone(int zoneNumber);
    void updateSnapToZoneShortcut(int index);

    // Rotate Windows
    void onRotateWindowsClockwise();
    void onRotateWindowsCounterclockwise();
    void updateRotateWindowsClockwiseShortcut();
    void updateRotateWindowsCounterclockwiseShortcut();

    // Cycle Windows in Zone
    void onCycleWindowForward();
    void onCycleWindowBackward();
    void updateCycleWindowForwardShortcut();
    void updateCycleWindowBackwardShortcut();

    // Resnap to New Layout
    void onResnapToNewLayout();
    void updateResnapToNewLayoutShortcut();

    // Snap All Windows
    void onSnapAllWindows();
    void updateSnapAllWindowsShortcut();

    // Auto-Tiling (#106, #107)
    void onPromoteToMaster();
    void onIncreaseMasterRatio();
    void onDecreaseMasterRatio();
    void updatePromoteMasterShortcut();
    void updateIncreaseMasterRatioShortcut();
    void updateDecreaseMasterRatioShortcut();

private:
    void setupEditorShortcut();
    void setupCyclingShortcuts();
    void setupQuickLayoutShortcuts();
    void setupNavigationShortcuts();
    void setupSwapWindowShortcuts();
    void setupSnapToZoneShortcuts();
    void setupRotateWindowsShortcuts();
    void setupCycleWindowsShortcuts();
    void setupResnapToNewLayoutShortcut();
    void setupSnapAllWindowsShortcut();
    void setupAutoTileShortcuts();

    Settings* m_settings = nullptr;
    LayoutManager* m_layoutManager = nullptr;

    QAction* m_editorAction = nullptr;
    QAction* m_previousLayoutAction = nullptr;
    QAction* m_nextLayoutAction = nullptr;
    QVector<QAction*> m_quickLayoutActions;

    // Keyboard navigation actions
    QAction* m_moveWindowLeftAction = nullptr;
    QAction* m_moveWindowRightAction = nullptr;
    QAction* m_moveWindowUpAction = nullptr;
    QAction* m_moveWindowDownAction = nullptr;
    QAction* m_focusZoneLeftAction = nullptr;
    QAction* m_focusZoneRightAction = nullptr;
    QAction* m_focusZoneUpAction = nullptr;
    QAction* m_focusZoneDownAction = nullptr;
    QAction* m_pushToEmptyZoneAction = nullptr;
    QAction* m_restoreWindowSizeAction = nullptr;
    QAction* m_toggleWindowFloatAction = nullptr;

    // Swap window actions
    QAction* m_swapWindowLeftAction = nullptr;
    QAction* m_swapWindowRightAction = nullptr;
    QAction* m_swapWindowUpAction = nullptr;
    QAction* m_swapWindowDownAction = nullptr;

    // Snap to Zone by Number actions
    QVector<QAction*> m_snapToZoneActions;

    // Rotate Windows actions
    QAction* m_rotateWindowsClockwiseAction = nullptr;
    QAction* m_rotateWindowsCounterclockwiseAction = nullptr;

    // Cycle Windows in Zone actions
    QAction* m_cycleWindowForwardAction = nullptr;
    QAction* m_cycleWindowBackwardAction = nullptr;

    // Resnap to New Layout action
    QAction* m_resnapToNewLayoutAction = nullptr;

    // Snap All Windows action
    QAction* m_snapAllWindowsAction = nullptr;

    // Auto-Tiling actions (#106, #107)
    QAction* m_promoteMasterAction = nullptr;
    QAction* m_increaseMasterRatioAction = nullptr;
    QAction* m_decreaseMasterRatioAction = nullptr;
};

} // namespace PlasmaZones
