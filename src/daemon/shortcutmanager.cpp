// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcutmanager.h"
#include "../config/configdefaults.h"
#include "../config/settings.h"
#include "../core/layoutmanager.h"
#include "../core/logging.h"
#include <QAction>
#include <QKeySequence>
#include <KGlobalAccel>
#include <KLocalizedString>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Macros
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Setup a global shortcut action
 * @param actionMember Pointer to QAction* member variable
 * @param i18nName Translatable display name for the action
 * @param objectName QStringLiteral object name for KGlobalAccel
 * @param getterName Method name (must exist on both ConfigDefaults and Settings)
 * @param slot Slot to connect to QAction::triggered
 *
 * Uses ConfigDefaults::getterName() for setDefaultShortcut so System Settings
 * shows the true app default when resetting. Uses m_settings->getterName() for
 * the actual shortcut (user's config or default).
 */
#define SETUP_SHORTCUT(actionMember, i18nName, objectName, getterName, slot) \
    do { \
        if (!actionMember) { \
            actionMember = new QAction(i18n(i18nName), this); \
            actionMember->setObjectName(QStringLiteral(objectName)); \
            const QKeySequence defaultShortcut(ConfigDefaults::getterName()); \
            const QKeySequence shortcut(m_settings->getterName()); \
            KGlobalAccel::self()->setDefaultShortcut(actionMember, {defaultShortcut}); \
            KGlobalAccel::setGlobalShortcut(actionMember, shortcut); \
            connect(actionMember, &QAction::triggered, this, slot); \
        } \
    } while (0)

/**
 * @brief Update a global shortcut from settings
 * @param actionMember Pointer to QAction* member variable
 * @param settingsGetter Settings method to get the shortcut string
 */
#define UPDATE_SHORTCUT(actionMember, settingsGetter) \
    do { \
        if (actionMember) { \
            KGlobalAccel::setGlobalShortcut(actionMember, QKeySequence(m_settings->settingsGetter())); \
        } \
    } while (0)

/**
 * @brief Delete and null a shortcut action
 * @param actionMember Pointer to QAction* member variable
 */
#define DELETE_SHORTCUT(actionMember) \
    do { \
        delete actionMember; \
        actionMember = nullptr; \
    } while (0)

// ═══════════════════════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════════════════════

ShortcutManager::ShortcutManager(Settings* settings, LayoutManager* layoutManager, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_layoutManager(layoutManager)
{
    Q_ASSERT(settings);
    Q_ASSERT(layoutManager);

    // Connect to settings changes to update shortcuts dynamically
    connect(m_settings, &Settings::openEditorShortcutChanged, this, &ShortcutManager::updateEditorShortcut);
    connect(m_settings, &Settings::previousLayoutShortcutChanged, this, &ShortcutManager::updatePreviousLayoutShortcut);
    connect(m_settings, &Settings::nextLayoutShortcutChanged, this, &ShortcutManager::updateNextLayoutShortcut);

    // Quick layout shortcuts (1-9)
    connect(m_settings, &Settings::quickLayout1ShortcutChanged, this, [this]() { updateQuickLayoutShortcut(0); });
    connect(m_settings, &Settings::quickLayout2ShortcutChanged, this, [this]() { updateQuickLayoutShortcut(1); });
    connect(m_settings, &Settings::quickLayout3ShortcutChanged, this, [this]() { updateQuickLayoutShortcut(2); });
    connect(m_settings, &Settings::quickLayout4ShortcutChanged, this, [this]() { updateQuickLayoutShortcut(3); });
    connect(m_settings, &Settings::quickLayout5ShortcutChanged, this, [this]() { updateQuickLayoutShortcut(4); });
    connect(m_settings, &Settings::quickLayout6ShortcutChanged, this, [this]() { updateQuickLayoutShortcut(5); });
    connect(m_settings, &Settings::quickLayout7ShortcutChanged, this, [this]() { updateQuickLayoutShortcut(6); });
    connect(m_settings, &Settings::quickLayout8ShortcutChanged, this, [this]() { updateQuickLayoutShortcut(7); });
    connect(m_settings, &Settings::quickLayout9ShortcutChanged, this, [this]() { updateQuickLayoutShortcut(8); });

    // Phase 1 Keyboard Navigation - connect to settings changes
    connect(m_settings, &Settings::moveWindowLeftShortcutChanged, this, &ShortcutManager::updateMoveWindowLeftShortcut);
    connect(m_settings, &Settings::moveWindowRightShortcutChanged, this, &ShortcutManager::updateMoveWindowRightShortcut);
    connect(m_settings, &Settings::moveWindowUpShortcutChanged, this, &ShortcutManager::updateMoveWindowUpShortcut);
    connect(m_settings, &Settings::moveWindowDownShortcutChanged, this, &ShortcutManager::updateMoveWindowDownShortcut);
    connect(m_settings, &Settings::focusZoneLeftShortcutChanged, this, &ShortcutManager::updateFocusZoneLeftShortcut);
    connect(m_settings, &Settings::focusZoneRightShortcutChanged, this, &ShortcutManager::updateFocusZoneRightShortcut);
    connect(m_settings, &Settings::focusZoneUpShortcutChanged, this, &ShortcutManager::updateFocusZoneUpShortcut);
    connect(m_settings, &Settings::focusZoneDownShortcutChanged, this, &ShortcutManager::updateFocusZoneDownShortcut);
    connect(m_settings, &Settings::pushToEmptyZoneShortcutChanged, this, &ShortcutManager::updatePushToEmptyZoneShortcut);
    connect(m_settings, &Settings::restoreWindowSizeShortcutChanged, this, &ShortcutManager::updateRestoreWindowSizeShortcut);
    connect(m_settings, &Settings::toggleWindowFloatShortcutChanged, this, &ShortcutManager::updateToggleWindowFloatShortcut);

    // Swap window shortcuts
    connect(m_settings, &Settings::swapWindowLeftShortcutChanged, this, &ShortcutManager::updateSwapWindowLeftShortcut);
    connect(m_settings, &Settings::swapWindowRightShortcutChanged, this, &ShortcutManager::updateSwapWindowRightShortcut);
    connect(m_settings, &Settings::swapWindowUpShortcutChanged, this, &ShortcutManager::updateSwapWindowUpShortcut);
    connect(m_settings, &Settings::swapWindowDownShortcutChanged, this, &ShortcutManager::updateSwapWindowDownShortcut);

    // Snap to Zone by Number shortcuts (1-9)
    connect(m_settings, &Settings::snapToZone1ShortcutChanged, this, [this]() { updateSnapToZoneShortcut(0); });
    connect(m_settings, &Settings::snapToZone2ShortcutChanged, this, [this]() { updateSnapToZoneShortcut(1); });
    connect(m_settings, &Settings::snapToZone3ShortcutChanged, this, [this]() { updateSnapToZoneShortcut(2); });
    connect(m_settings, &Settings::snapToZone4ShortcutChanged, this, [this]() { updateSnapToZoneShortcut(3); });
    connect(m_settings, &Settings::snapToZone5ShortcutChanged, this, [this]() { updateSnapToZoneShortcut(4); });
    connect(m_settings, &Settings::snapToZone6ShortcutChanged, this, [this]() { updateSnapToZoneShortcut(5); });
    connect(m_settings, &Settings::snapToZone7ShortcutChanged, this, [this]() { updateSnapToZoneShortcut(6); });
    connect(m_settings, &Settings::snapToZone8ShortcutChanged, this, [this]() { updateSnapToZoneShortcut(7); });
    connect(m_settings, &Settings::snapToZone9ShortcutChanged, this, [this]() { updateSnapToZoneShortcut(8); });

    // Rotate Windows shortcuts
    connect(m_settings, &Settings::rotateWindowsClockwiseShortcutChanged, this, &ShortcutManager::updateRotateWindowsClockwiseShortcut);
    connect(m_settings, &Settings::rotateWindowsCounterclockwiseShortcutChanged, this, &ShortcutManager::updateRotateWindowsCounterclockwiseShortcut);

    // Cycle Windows in Zone shortcuts
    connect(m_settings, &Settings::cycleWindowForwardShortcutChanged, this, &ShortcutManager::updateCycleWindowForwardShortcut);
    connect(m_settings, &Settings::cycleWindowBackwardShortcutChanged, this, &ShortcutManager::updateCycleWindowBackwardShortcut);

    // Resnap to New Layout shortcut
    connect(m_settings, &Settings::resnapToNewLayoutShortcutChanged, this, &ShortcutManager::updateResnapToNewLayoutShortcut);

    // Connect to general settingsChanged signal to handle KCM reload
    // This is necessary because Settings::load() only emits settingsChanged(),
    // not individual shortcut signals. When KCM saves and calls reloadSettings(),
    // we need to refresh all shortcuts from the newly loaded values.
    connect(m_settings, &Settings::settingsChanged, this, &ShortcutManager::updateShortcuts);
}

ShortcutManager::~ShortcutManager()
{
    unregisterShortcuts();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public Methods
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::registerShortcuts()
{
    setupEditorShortcut();
    setupCyclingShortcuts();
    setupQuickLayoutShortcuts();
    setupNavigationShortcuts();
    setupSwapWindowShortcuts();
    setupSnapToZoneShortcuts();
    setupRotateWindowsShortcuts();
    setupCycleWindowsShortcuts();
    setupResnapToNewLayoutShortcut();
}

void ShortcutManager::updateShortcuts()
{
    // Called when settingsChanged() is emitted (e.g., after KCM reload)
    // Refresh all shortcuts from current settings values
    qCDebug(lcShortcuts) << "Updating all shortcuts from settings";

    // Core shortcuts
    updateEditorShortcut();
    updatePreviousLayoutShortcut();
    updateNextLayoutShortcut();

    // Quick layout shortcuts (0-8 internally, 1-9 for users)
    for (int i = 0; i < 9; ++i) {
        updateQuickLayoutShortcut(i);
    }

    // Phase 1 Keyboard Navigation shortcuts
    updateMoveWindowLeftShortcut();
    updateMoveWindowRightShortcut();
    updateMoveWindowUpShortcut();
    updateMoveWindowDownShortcut();
    updateFocusZoneLeftShortcut();
    updateFocusZoneRightShortcut();
    updateFocusZoneUpShortcut();
    updateFocusZoneDownShortcut();
    updatePushToEmptyZoneShortcut();
    updateRestoreWindowSizeShortcut();
    updateToggleWindowFloatShortcut();

    // Swap window shortcuts
    updateSwapWindowLeftShortcut();
    updateSwapWindowRightShortcut();
    updateSwapWindowUpShortcut();
    updateSwapWindowDownShortcut();

    // Snap to Zone shortcuts (0-8 internally, 1-9 for users)
    for (int i = 0; i < 9; ++i) {
        updateSnapToZoneShortcut(i);
    }

    // Rotate Windows shortcuts
    updateRotateWindowsClockwiseShortcut();
    updateRotateWindowsCounterclockwiseShortcut();

    // Resnap to New Layout shortcut
    updateResnapToNewLayoutShortcut();

    // Cycle Windows in Zone shortcuts
    updateCycleWindowForwardShortcut();
    updateCycleWindowBackwardShortcut();

}

void ShortcutManager::unregisterShortcuts()
{
    // Clear all actions - KGlobalAccel will unregister automatically when actions are deleted
    // Use direct delete instead of deleteLater() because:
    // 1. Actions have 'this' as parent, so deleteLater() + parent cleanup = double-free risk
    // 2. We're in controlled context (destructor or explicit unregister) where immediate delete is safe
    // 3. KGlobalAccel needs synchronous cleanup for proper shortcut unregistration

    // Core shortcuts
    DELETE_SHORTCUT(m_editorAction);
    DELETE_SHORTCUT(m_previousLayoutAction);
    DELETE_SHORTCUT(m_nextLayoutAction);

    qDeleteAll(m_quickLayoutActions);
    m_quickLayoutActions.clear();

    // Phase 1 Keyboard Navigation actions
    DELETE_SHORTCUT(m_moveWindowLeftAction);
    DELETE_SHORTCUT(m_moveWindowRightAction);
    DELETE_SHORTCUT(m_moveWindowUpAction);
    DELETE_SHORTCUT(m_moveWindowDownAction);
    DELETE_SHORTCUT(m_focusZoneLeftAction);
    DELETE_SHORTCUT(m_focusZoneRightAction);
    DELETE_SHORTCUT(m_focusZoneUpAction);
    DELETE_SHORTCUT(m_focusZoneDownAction);
    DELETE_SHORTCUT(m_pushToEmptyZoneAction);
    DELETE_SHORTCUT(m_restoreWindowSizeAction);
    DELETE_SHORTCUT(m_toggleWindowFloatAction);

    // Swap window actions
    DELETE_SHORTCUT(m_swapWindowLeftAction);
    DELETE_SHORTCUT(m_swapWindowRightAction);
    DELETE_SHORTCUT(m_swapWindowUpAction);
    DELETE_SHORTCUT(m_swapWindowDownAction);

    // Snap to Zone actions
    qDeleteAll(m_snapToZoneActions);
    m_snapToZoneActions.clear();

    // Rotate Windows actions
    DELETE_SHORTCUT(m_rotateWindowsClockwiseAction);
    DELETE_SHORTCUT(m_rotateWindowsCounterclockwiseAction);

    // Cycle Windows in Zone actions
    DELETE_SHORTCUT(m_cycleWindowForwardAction);
    DELETE_SHORTCUT(m_cycleWindowBackwardAction);

    // Resnap to New Layout action
    DELETE_SHORTCUT(m_resnapToNewLayoutAction);
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
    qCDebug(lcShortcuts) << "Quick layout shortcut triggered for slot" << number;
    Q_EMIT quickLayoutRequested(number);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Setup Methods
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::setupEditorShortcut()
{
    SETUP_SHORTCUT(m_editorAction, "Open Zone Editor", "open_editor",
                   openEditorShortcut, &ShortcutManager::onOpenEditor);
}

void ShortcutManager::setupCyclingShortcuts()
{
    SETUP_SHORTCUT(m_previousLayoutAction, "Previous Layout", "previous_layout",
                   previousLayoutShortcut, &ShortcutManager::onPreviousLayout);
    SETUP_SHORTCUT(m_nextLayoutAction, "Next Layout", "next_layout",
                   nextLayoutShortcut, &ShortcutManager::onNextLayout);
}

void ShortcutManager::setupQuickLayoutShortcuts()
{
    // Clear existing actions - use direct delete for same reasons as unregisterShortcuts()
    qDeleteAll(m_quickLayoutActions);
    m_quickLayoutActions.clear();

    const QString quickLayoutDefaults[] = {
        ConfigDefaults::quickLayout1Shortcut(), ConfigDefaults::quickLayout2Shortcut(),
        ConfigDefaults::quickLayout3Shortcut(), ConfigDefaults::quickLayout4Shortcut(),
        ConfigDefaults::quickLayout5Shortcut(), ConfigDefaults::quickLayout6Shortcut(),
        ConfigDefaults::quickLayout7Shortcut(), ConfigDefaults::quickLayout8Shortcut(),
        ConfigDefaults::quickLayout9Shortcut()
    };

    for (int i = 0; i < 9; ++i) {
        auto* quickAction = new QAction(i18n("Apply Layout %1", i + 1), this);
        quickAction->setObjectName(QStringLiteral("quick_layout_%1").arg(i + 1));
        KGlobalAccel::self()->setDefaultShortcut(quickAction, {QKeySequence(quickLayoutDefaults[i])});
        KGlobalAccel::setGlobalShortcut(quickAction, QKeySequence(m_settings->quickLayoutShortcut(i)));

        const int layoutNumber = i + 1;
        connect(quickAction, &QAction::triggered, this, [this, layoutNumber]() {
            onQuickLayout(layoutNumber);
        });

        m_quickLayoutActions.append(quickAction);
    }
}

void ShortcutManager::setupNavigationShortcuts()
{
    // Move Window shortcuts
    SETUP_SHORTCUT(m_moveWindowLeftAction, "Move Window Left", "move_window_left",
                   moveWindowLeftShortcut, &ShortcutManager::onMoveWindowLeft);
    SETUP_SHORTCUT(m_moveWindowRightAction, "Move Window Right", "move_window_right",
                   moveWindowRightShortcut, &ShortcutManager::onMoveWindowRight);
    SETUP_SHORTCUT(m_moveWindowUpAction, "Move Window Up", "move_window_up",
                   moveWindowUpShortcut, &ShortcutManager::onMoveWindowUp);
    SETUP_SHORTCUT(m_moveWindowDownAction, "Move Window Down", "move_window_down",
                   moveWindowDownShortcut, &ShortcutManager::onMoveWindowDown);

    // Focus Zone shortcuts
    SETUP_SHORTCUT(m_focusZoneLeftAction, "Focus Zone Left", "focus_zone_left",
                   focusZoneLeftShortcut, &ShortcutManager::onFocusZoneLeft);
    SETUP_SHORTCUT(m_focusZoneRightAction, "Focus Zone Right", "focus_zone_right",
                   focusZoneRightShortcut, &ShortcutManager::onFocusZoneRight);
    SETUP_SHORTCUT(m_focusZoneUpAction, "Focus Zone Up", "focus_zone_up",
                   focusZoneUpShortcut, &ShortcutManager::onFocusZoneUp);
    SETUP_SHORTCUT(m_focusZoneDownAction, "Focus Zone Down", "focus_zone_down",
                   focusZoneDownShortcut, &ShortcutManager::onFocusZoneDown);

    // Additional navigation shortcuts
    SETUP_SHORTCUT(m_pushToEmptyZoneAction, "Push to Empty Zone", "push_to_empty_zone",
                   pushToEmptyZoneShortcut, &ShortcutManager::onPushToEmptyZone);
    SETUP_SHORTCUT(m_restoreWindowSizeAction, "Restore Window Size", "restore_window_size",
                   restoreWindowSizeShortcut, &ShortcutManager::onRestoreWindowSize);
    SETUP_SHORTCUT(m_toggleWindowFloatAction, "Toggle Window Float", "toggle_window_float",
                   toggleWindowFloatShortcut, &ShortcutManager::onToggleWindowFloat);

    qCInfo(lcShortcuts) << "Navigation shortcuts registered";
}

void ShortcutManager::setupSwapWindowShortcuts()
{
    SETUP_SHORTCUT(m_swapWindowLeftAction, "Swap Window Left", "swap_window_left",
                   swapWindowLeftShortcut, &ShortcutManager::onSwapWindowLeft);
    SETUP_SHORTCUT(m_swapWindowRightAction, "Swap Window Right", "swap_window_right",
                   swapWindowRightShortcut, &ShortcutManager::onSwapWindowRight);
    SETUP_SHORTCUT(m_swapWindowUpAction, "Swap Window Up", "swap_window_up",
                   swapWindowUpShortcut, &ShortcutManager::onSwapWindowUp);
    SETUP_SHORTCUT(m_swapWindowDownAction, "Swap Window Down", "swap_window_down",
                   swapWindowDownShortcut, &ShortcutManager::onSwapWindowDown);

    qCInfo(lcShortcuts) << "Swap window shortcuts registered (Meta+Ctrl+Alt+Arrow)";
}

void ShortcutManager::setupSnapToZoneShortcuts()
{
    // Clear existing actions
    qDeleteAll(m_snapToZoneActions);
    m_snapToZoneActions.clear();

    const QString snapToZoneDefaults[] = {
        ConfigDefaults::snapToZone1Shortcut(), ConfigDefaults::snapToZone2Shortcut(),
        ConfigDefaults::snapToZone3Shortcut(), ConfigDefaults::snapToZone4Shortcut(),
        ConfigDefaults::snapToZone5Shortcut(), ConfigDefaults::snapToZone6Shortcut(),
        ConfigDefaults::snapToZone7Shortcut(), ConfigDefaults::snapToZone8Shortcut(),
        ConfigDefaults::snapToZone9Shortcut()
    };

    for (int i = 0; i < 9; ++i) {
        auto* snapAction = new QAction(i18n("Snap to Zone %1", i + 1), this);
        snapAction->setObjectName(QStringLiteral("snap_to_zone_%1").arg(i + 1));
        KGlobalAccel::self()->setDefaultShortcut(snapAction, {QKeySequence(snapToZoneDefaults[i])});
        KGlobalAccel::setGlobalShortcut(snapAction, QKeySequence(m_settings->snapToZoneShortcut(i)));

        const int zoneNumber = i + 1;
        connect(snapAction, &QAction::triggered, this, [this, zoneNumber]() {
            onSnapToZone(zoneNumber);
        });

        m_snapToZoneActions.append(snapAction);
    }

    qCInfo(lcShortcuts) << "Snap-to-zone shortcuts registered (Meta+Ctrl+1-9)";
}

void ShortcutManager::setupRotateWindowsShortcuts()
{
    SETUP_SHORTCUT(m_rotateWindowsClockwiseAction, "Rotate Windows Clockwise", "rotate_windows_clockwise",
                   rotateWindowsClockwiseShortcut, &ShortcutManager::onRotateWindowsClockwise);
    SETUP_SHORTCUT(m_rotateWindowsCounterclockwiseAction, "Rotate Windows Counterclockwise", "rotate_windows_counterclockwise",
                   rotateWindowsCounterclockwiseShortcut, &ShortcutManager::onRotateWindowsCounterclockwise);

    qCInfo(lcShortcuts) << "Rotate windows shortcuts registered (Meta+Ctrl+[ / Meta+Ctrl+])";
}

void ShortcutManager::setupCycleWindowsShortcuts()
{
    SETUP_SHORTCUT(m_cycleWindowForwardAction, "Cycle Window Forward in Zone", "cycle_window_forward",
                   cycleWindowForwardShortcut, &ShortcutManager::onCycleWindowForward);
    SETUP_SHORTCUT(m_cycleWindowBackwardAction, "Cycle Window Backward in Zone", "cycle_window_backward",
                   cycleWindowBackwardShortcut, &ShortcutManager::onCycleWindowBackward);

    qCInfo(lcShortcuts) << "Cycle windows shortcuts registered (Meta+Alt+. / Meta+Alt+,)";
}

void ShortcutManager::setupResnapToNewLayoutShortcut()
{
    SETUP_SHORTCUT(m_resnapToNewLayoutAction, "Resnap Windows to New Layout", "resnap_to_new_layout",
                   resnapToNewLayoutShortcut, &ShortcutManager::onResnapToNewLayout);

    qCInfo(lcShortcuts) << "Resnap to new layout shortcut registered (Meta+Ctrl+Z)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation Slot Handlers
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::onMoveWindowLeft()
{
    qCDebug(lcShortcuts) << "Move window left triggered";
    Q_EMIT moveWindowRequested(NavigationDirection::Left);
}

void ShortcutManager::onMoveWindowRight()
{
    qCDebug(lcShortcuts) << "Move window right triggered";
    Q_EMIT moveWindowRequested(NavigationDirection::Right);
}

void ShortcutManager::onMoveWindowUp()
{
    qCDebug(lcShortcuts) << "Move window up triggered";
    Q_EMIT moveWindowRequested(NavigationDirection::Up);
}

void ShortcutManager::onMoveWindowDown()
{
    qCDebug(lcShortcuts) << "Move window down triggered";
    Q_EMIT moveWindowRequested(NavigationDirection::Down);
}

void ShortcutManager::onFocusZoneLeft()
{
    qCDebug(lcShortcuts) << "Focus zone left triggered";
    Q_EMIT focusZoneRequested(NavigationDirection::Left);
}

void ShortcutManager::onFocusZoneRight()
{
    qCDebug(lcShortcuts) << "Focus zone right triggered";
    Q_EMIT focusZoneRequested(NavigationDirection::Right);
}

void ShortcutManager::onFocusZoneUp()
{
    qCDebug(lcShortcuts) << "Focus zone up triggered";
    Q_EMIT focusZoneRequested(NavigationDirection::Up);
}

void ShortcutManager::onFocusZoneDown()
{
    qCDebug(lcShortcuts) << "Focus zone down triggered";
    Q_EMIT focusZoneRequested(NavigationDirection::Down);
}

void ShortcutManager::onPushToEmptyZone()
{
    qCDebug(lcShortcuts) << "Push to empty zone triggered";
    Q_EMIT pushToEmptyZoneRequested();
}

void ShortcutManager::onRestoreWindowSize()
{
    qCDebug(lcShortcuts) << "Restore window size triggered";
    Q_EMIT restoreWindowSizeRequested();
}

void ShortcutManager::onToggleWindowFloat()
{
    qCDebug(lcShortcuts) << "Toggle window float triggered";
    Q_EMIT toggleWindowFloatRequested();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Swap Window Slot Handlers
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::onSwapWindowLeft()
{
    qCDebug(lcShortcuts) << "Swap window left triggered";
    Q_EMIT swapWindowRequested(NavigationDirection::Left);
}

void ShortcutManager::onSwapWindowRight()
{
    qCDebug(lcShortcuts) << "Swap window right triggered";
    Q_EMIT swapWindowRequested(NavigationDirection::Right);
}

void ShortcutManager::onSwapWindowUp()
{
    qCDebug(lcShortcuts) << "Swap window up triggered";
    Q_EMIT swapWindowRequested(NavigationDirection::Up);
}

void ShortcutManager::onSwapWindowDown()
{
    qCDebug(lcShortcuts) << "Swap window down triggered";
    Q_EMIT swapWindowRequested(NavigationDirection::Down);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Snap to Zone Slot Handlers
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::onSnapToZone(int zoneNumber)
{
    qCDebug(lcShortcuts) << "Snap to zone" << zoneNumber << "triggered";
    Q_EMIT snapToZoneRequested(zoneNumber);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Rotate Windows Slot Handlers
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::onRotateWindowsClockwise()
{
    qCDebug(lcShortcuts) << "Rotate windows clockwise triggered";
    Q_EMIT rotateWindowsRequested(true);
}

void ShortcutManager::onRotateWindowsCounterclockwise()
{
    qCDebug(lcShortcuts) << "Rotate windows counterclockwise triggered";
    Q_EMIT rotateWindowsRequested(false);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cycle Windows Slot Handlers
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::onCycleWindowForward()
{
    qCDebug(lcShortcuts) << "Cycle window forward triggered";
    Q_EMIT cycleWindowsInZoneRequested(true);
}

void ShortcutManager::onCycleWindowBackward()
{
    qCDebug(lcShortcuts) << "Cycle window backward triggered";
    Q_EMIT cycleWindowsInZoneRequested(false);
}

void ShortcutManager::onResnapToNewLayout()
{
    qCDebug(lcShortcuts) << "Resnap to new layout triggered";
    Q_EMIT resnapToNewLayoutRequested();
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
    if (index >= 0 && index < m_quickLayoutActions.size()) {
        KGlobalAccel::setGlobalShortcut(m_quickLayoutActions[index],
                                        QKeySequence(m_settings->quickLayoutShortcut(index)));
    }
}

void ShortcutManager::updateMoveWindowLeftShortcut()
{
    UPDATE_SHORTCUT(m_moveWindowLeftAction, moveWindowLeftShortcut);
}

void ShortcutManager::updateMoveWindowRightShortcut()
{
    UPDATE_SHORTCUT(m_moveWindowRightAction, moveWindowRightShortcut);
}

void ShortcutManager::updateMoveWindowUpShortcut()
{
    UPDATE_SHORTCUT(m_moveWindowUpAction, moveWindowUpShortcut);
}

void ShortcutManager::updateMoveWindowDownShortcut()
{
    UPDATE_SHORTCUT(m_moveWindowDownAction, moveWindowDownShortcut);
}

void ShortcutManager::updateFocusZoneLeftShortcut()
{
    UPDATE_SHORTCUT(m_focusZoneLeftAction, focusZoneLeftShortcut);
}

void ShortcutManager::updateFocusZoneRightShortcut()
{
    UPDATE_SHORTCUT(m_focusZoneRightAction, focusZoneRightShortcut);
}

void ShortcutManager::updateFocusZoneUpShortcut()
{
    UPDATE_SHORTCUT(m_focusZoneUpAction, focusZoneUpShortcut);
}

void ShortcutManager::updateFocusZoneDownShortcut()
{
    UPDATE_SHORTCUT(m_focusZoneDownAction, focusZoneDownShortcut);
}

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

void ShortcutManager::updateSwapWindowLeftShortcut()
{
    UPDATE_SHORTCUT(m_swapWindowLeftAction, swapWindowLeftShortcut);
}

void ShortcutManager::updateSwapWindowRightShortcut()
{
    UPDATE_SHORTCUT(m_swapWindowRightAction, swapWindowRightShortcut);
}

void ShortcutManager::updateSwapWindowUpShortcut()
{
    UPDATE_SHORTCUT(m_swapWindowUpAction, swapWindowUpShortcut);
}

void ShortcutManager::updateSwapWindowDownShortcut()
{
    UPDATE_SHORTCUT(m_swapWindowDownAction, swapWindowDownShortcut);
}

void ShortcutManager::updateSnapToZoneShortcut(int index)
{
    if (index >= 0 && index < m_snapToZoneActions.size()) {
        KGlobalAccel::setGlobalShortcut(m_snapToZoneActions[index],
                                        QKeySequence(m_settings->snapToZoneShortcut(index)));
    }
}

void ShortcutManager::updateRotateWindowsClockwiseShortcut()
{
    UPDATE_SHORTCUT(m_rotateWindowsClockwiseAction, rotateWindowsClockwiseShortcut);
}

void ShortcutManager::updateRotateWindowsCounterclockwiseShortcut()
{
    UPDATE_SHORTCUT(m_rotateWindowsCounterclockwiseAction, rotateWindowsCounterclockwiseShortcut);
}

void ShortcutManager::updateCycleWindowForwardShortcut()
{
    UPDATE_SHORTCUT(m_cycleWindowForwardAction, cycleWindowForwardShortcut);
}

void ShortcutManager::updateCycleWindowBackwardShortcut()
{
    UPDATE_SHORTCUT(m_cycleWindowBackwardAction, cycleWindowBackwardShortcut);
}

void ShortcutManager::updateResnapToNewLayoutShortcut()
{
    UPDATE_SHORTCUT(m_resnapToNewLayoutAction, resnapToNewLayoutShortcut);
}

// Undefine macros to keep them local to this file
#undef SETUP_SHORTCUT
#undef UPDATE_SHORTCUT
#undef DELETE_SHORTCUT

} // namespace PlasmaZones
