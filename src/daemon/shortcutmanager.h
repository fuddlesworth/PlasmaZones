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
 * @brief Manages global keyboard shortcuts (SRP)
 *
 * Handles registration and management of all global shortcuts
 * for PlasmaZones. Follows Single Responsibility Principle by
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

    // Phase 1 Keyboard Navigation signals
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

private:
    void setupEditorShortcut();
    void setupCyclingShortcuts();
    void setupQuickLayoutShortcuts();
    void setupNavigationShortcuts();

    Settings* m_settings = nullptr;
    LayoutManager* m_layoutManager = nullptr;

    QAction* m_editorAction = nullptr;
    QAction* m_previousLayoutAction = nullptr;
    QAction* m_nextLayoutAction = nullptr;
    QVector<QAction*> m_quickLayoutActions;

    // Phase 1 Keyboard Navigation actions
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
};

} // namespace PlasmaZones
