// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcutmanager.h"
#include "shortcutbackend.h"
#include "../config/configdefaults.h"
#include "../config/settings.h"
#include "../core/layoutmanager.h"
#include "../core/logging.h"
#include <QAction>
#include <QKeySequence>
#include <QTimer>
#include "pz_i18n.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Macros
// ═══════════════════════════════════════════════════════════════════════════════

// Setup a global shortcut: ConfigDefaults for default, m_settings for current.
// setDefaultShortcut registers the action without key grabbing (fast).
// setGlobalShortcut defers the key grab to the backend (async internally).
#define SETUP_SHORTCUT(actionMember, i18nName, objectName, getterName, slot)                                           \
    do {                                                                                                               \
        if (!actionMember) {                                                                                           \
            actionMember = new QAction(PzI18n::tr(i18nName), this);                                                    \
            actionMember->setObjectName(QStringLiteral(objectName));                                                   \
            const QKeySequence defaultShortcut(ConfigDefaults::getterName());                                          \
            const QKeySequence shortcut(m_settings->getterName());                                                     \
            m_shortcutBackend->setDefaultShortcut(actionMember, defaultShortcut);                                      \
            m_shortcutBackend->setGlobalShortcut(actionMember, shortcut);                                              \
            connect(actionMember, &QAction::triggered, this, slot);                                                    \
        }                                                                                                              \
    } while (0)

// Delete and null a shortcut action
#define DELETE_SHORTCUT(actionMember)                                                                                  \
    do {                                                                                                               \
        delete actionMember;                                                                                           \
        actionMember = nullptr;                                                                                        \
    } while (0)

// ═══════════════════════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════════════════════

ShortcutManager::ShortcutManager(Settings* settings, LayoutManager* layoutManager, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_layoutManager(layoutManager)
    , m_shortcutBackend(createShortcutBackend(this))
{
    Q_ASSERT(settings);
    Q_ASSERT(layoutManager);

    // Connect to settings changes to update shortcuts dynamically
    connect(m_settings, &Settings::openEditorShortcutChanged, this, &ShortcutManager::updateEditorShortcut);
    connect(m_settings, &Settings::openSettingsShortcutChanged, this, &ShortcutManager::updateSettingsShortcut);
    connect(m_settings, &Settings::previousLayoutShortcutChanged, this, &ShortcutManager::updatePreviousLayoutShortcut);
    connect(m_settings, &Settings::nextLayoutShortcutChanged, this, &ShortcutManager::updateNextLayoutShortcut);

    // Quick layout shortcuts (1-9)
    connect(m_settings, &Settings::quickLayout1ShortcutChanged, this, [this]() {
        updateQuickLayoutShortcut(0);
    });
    connect(m_settings, &Settings::quickLayout2ShortcutChanged, this, [this]() {
        updateQuickLayoutShortcut(1);
    });
    connect(m_settings, &Settings::quickLayout3ShortcutChanged, this, [this]() {
        updateQuickLayoutShortcut(2);
    });
    connect(m_settings, &Settings::quickLayout4ShortcutChanged, this, [this]() {
        updateQuickLayoutShortcut(3);
    });
    connect(m_settings, &Settings::quickLayout5ShortcutChanged, this, [this]() {
        updateQuickLayoutShortcut(4);
    });
    connect(m_settings, &Settings::quickLayout6ShortcutChanged, this, [this]() {
        updateQuickLayoutShortcut(5);
    });
    connect(m_settings, &Settings::quickLayout7ShortcutChanged, this, [this]() {
        updateQuickLayoutShortcut(6);
    });
    connect(m_settings, &Settings::quickLayout8ShortcutChanged, this, [this]() {
        updateQuickLayoutShortcut(7);
    });
    connect(m_settings, &Settings::quickLayout9ShortcutChanged, this, [this]() {
        updateQuickLayoutShortcut(8);
    });

    // Keyboard navigation - connect to settings changes
    connect(m_settings, &Settings::moveWindowLeftShortcutChanged, this, &ShortcutManager::updateMoveWindowLeftShortcut);
    connect(m_settings, &Settings::moveWindowRightShortcutChanged, this,
            &ShortcutManager::updateMoveWindowRightShortcut);
    connect(m_settings, &Settings::moveWindowUpShortcutChanged, this, &ShortcutManager::updateMoveWindowUpShortcut);
    connect(m_settings, &Settings::moveWindowDownShortcutChanged, this, &ShortcutManager::updateMoveWindowDownShortcut);
    connect(m_settings, &Settings::focusZoneLeftShortcutChanged, this, &ShortcutManager::updateFocusZoneLeftShortcut);
    connect(m_settings, &Settings::focusZoneRightShortcutChanged, this, &ShortcutManager::updateFocusZoneRightShortcut);
    connect(m_settings, &Settings::focusZoneUpShortcutChanged, this, &ShortcutManager::updateFocusZoneUpShortcut);
    connect(m_settings, &Settings::focusZoneDownShortcutChanged, this, &ShortcutManager::updateFocusZoneDownShortcut);
    connect(m_settings, &Settings::pushToEmptyZoneShortcutChanged, this,
            &ShortcutManager::updatePushToEmptyZoneShortcut);
    connect(m_settings, &Settings::restoreWindowSizeShortcutChanged, this,
            &ShortcutManager::updateRestoreWindowSizeShortcut);
    connect(m_settings, &Settings::toggleWindowFloatShortcutChanged, this,
            &ShortcutManager::updateToggleWindowFloatShortcut);

    // Swap window shortcuts
    connect(m_settings, &Settings::swapWindowLeftShortcutChanged, this, &ShortcutManager::updateSwapWindowLeftShortcut);
    connect(m_settings, &Settings::swapWindowRightShortcutChanged, this,
            &ShortcutManager::updateSwapWindowRightShortcut);
    connect(m_settings, &Settings::swapWindowUpShortcutChanged, this, &ShortcutManager::updateSwapWindowUpShortcut);
    connect(m_settings, &Settings::swapWindowDownShortcutChanged, this, &ShortcutManager::updateSwapWindowDownShortcut);

    // Snap to Zone by Number shortcuts (1-9)
    connect(m_settings, &Settings::snapToZone1ShortcutChanged, this, [this]() {
        updateSnapToZoneShortcut(0);
    });
    connect(m_settings, &Settings::snapToZone2ShortcutChanged, this, [this]() {
        updateSnapToZoneShortcut(1);
    });
    connect(m_settings, &Settings::snapToZone3ShortcutChanged, this, [this]() {
        updateSnapToZoneShortcut(2);
    });
    connect(m_settings, &Settings::snapToZone4ShortcutChanged, this, [this]() {
        updateSnapToZoneShortcut(3);
    });
    connect(m_settings, &Settings::snapToZone5ShortcutChanged, this, [this]() {
        updateSnapToZoneShortcut(4);
    });
    connect(m_settings, &Settings::snapToZone6ShortcutChanged, this, [this]() {
        updateSnapToZoneShortcut(5);
    });
    connect(m_settings, &Settings::snapToZone7ShortcutChanged, this, [this]() {
        updateSnapToZoneShortcut(6);
    });
    connect(m_settings, &Settings::snapToZone8ShortcutChanged, this, [this]() {
        updateSnapToZoneShortcut(7);
    });
    connect(m_settings, &Settings::snapToZone9ShortcutChanged, this, [this]() {
        updateSnapToZoneShortcut(8);
    });

    // Rotate Windows shortcuts
    connect(m_settings, &Settings::rotateWindowsClockwiseShortcutChanged, this,
            &ShortcutManager::updateRotateWindowsClockwiseShortcut);
    connect(m_settings, &Settings::rotateWindowsCounterclockwiseShortcutChanged, this,
            &ShortcutManager::updateRotateWindowsCounterclockwiseShortcut);

    // Cycle Windows in Zone shortcuts
    connect(m_settings, &Settings::cycleWindowForwardShortcutChanged, this,
            &ShortcutManager::updateCycleWindowForwardShortcut);
    connect(m_settings, &Settings::cycleWindowBackwardShortcutChanged, this,
            &ShortcutManager::updateCycleWindowBackwardShortcut);

    // Resnap to New Layout shortcut
    connect(m_settings, &Settings::resnapToNewLayoutShortcutChanged, this,
            &ShortcutManager::updateResnapToNewLayoutShortcut);

    // Snap All Windows shortcut
    connect(m_settings, &Settings::snapAllWindowsShortcutChanged, this, &ShortcutManager::updateSnapAllWindowsShortcut);

    // Layout Picker shortcut
    connect(m_settings, &Settings::layoutPickerShortcutChanged, this, &ShortcutManager::updateLayoutPickerShortcut);

    // Toggle Layout Lock shortcut
    connect(m_settings, &Settings::toggleLayoutLockShortcutChanged, this,
            &ShortcutManager::updateToggleLayoutLockShortcut);

    // Virtual Screen Swap shortcuts
    connect(m_settings, &Settings::swapVirtualScreenLeftShortcutChanged, this,
            &ShortcutManager::updateSwapVirtualScreenLeftShortcut);
    connect(m_settings, &Settings::swapVirtualScreenRightShortcutChanged, this,
            &ShortcutManager::updateSwapVirtualScreenRightShortcut);
    connect(m_settings, &Settings::swapVirtualScreenUpShortcutChanged, this,
            &ShortcutManager::updateSwapVirtualScreenUpShortcut);
    connect(m_settings, &Settings::swapVirtualScreenDownShortcutChanged, this,
            &ShortcutManager::updateSwapVirtualScreenDownShortcut);

    // Rotate Virtual Screens shortcuts
    connect(m_settings, &Settings::rotateVirtualScreensClockwiseShortcutChanged, this,
            &ShortcutManager::updateRotateVirtualScreensClockwiseShortcut);
    connect(m_settings, &Settings::rotateVirtualScreensCounterclockwiseShortcutChanged, this,
            &ShortcutManager::updateRotateVirtualScreensCounterclockwiseShortcut);

    // Autotile shortcut settings connections
    connect(m_settings, &Settings::autotileToggleShortcutChanged, this, &ShortcutManager::updateToggleAutotileShortcut);
    connect(m_settings, &Settings::autotileFocusMasterShortcutChanged, this,
            &ShortcutManager::updateFocusMasterShortcut);
    connect(m_settings, &Settings::autotileSwapMasterShortcutChanged, this, &ShortcutManager::updateSwapMasterShortcut);
    connect(m_settings, &Settings::autotileIncMasterRatioShortcutChanged, this,
            &ShortcutManager::updateIncMasterRatioShortcut);
    connect(m_settings, &Settings::autotileDecMasterRatioShortcutChanged, this,
            &ShortcutManager::updateDecMasterRatioShortcut);
    connect(m_settings, &Settings::autotileIncMasterCountShortcutChanged, this,
            &ShortcutManager::updateIncMasterCountShortcut);
    connect(m_settings, &Settings::autotileDecMasterCountShortcutChanged, this,
            &ShortcutManager::updateDecMasterCountShortcut);
    connect(m_settings, &Settings::autotileRetileShortcutChanged, this, &ShortcutManager::updateRetileShortcut);

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
    if (m_registrationInProgress) {
        qCWarning(lcShortcuts) << "registerShortcuts() called while registration "
                                  "is already in progress - ignoring";
        return;
    }
    m_registrationInProgress = true;

    // Register all actions with setDefaultShortcut (fast — stores defaults in
    // kglobalacceld without grabbing keys). Each setup call also queues the
    // shortcut for async key-grab activation below.
    setupEditorShortcut();
    setupSettingsShortcut();
    setupCyclingShortcuts();
    setupQuickLayoutShortcuts();
    setupNavigationShortcuts();
    setupSwapWindowShortcuts();
    setupSnapToZoneShortcuts();
    setupRotateWindowsShortcuts();
    setupCycleWindowsShortcuts();
    setupResnapToNewLayoutShortcut();
    setupSnapAllWindowsShortcut();
    setupLayoutPickerShortcut();
    setupToggleLayoutLockShortcut();
    setupSwapVirtualScreenShortcuts();
    setupRotateVirtualScreensShortcuts();
    setupAutotileShortcuts();

    qCInfo(lcShortcuts) << "Shortcuts registered, flushing backend";

    // Connect backend's shortcutsReady signal to our completion handler (one-shot).
    // The backend handles async D-Bus batching internally.
    connect(
        m_shortcutBackend.get(), &IShortcutBackend::shortcutsReady, this,
        [this]() {
            m_registrationInProgress = false;
            qCInfo(lcShortcuts) << "All key grabs activated";
            Q_EMIT shortcutsRegistered();

            if (m_settingsDirty) {
                m_settingsDirty = false;
                updateShortcuts();
            }
        },
        static_cast<Qt::ConnectionType>(Qt::SingleShotConnection));

    m_shortcutBackend->flush();
}

void ShortcutManager::updateShortcuts()
{
    // Called when settingsChanged() is emitted (e.g., after KCM reload).
    // If registration is still in progress, defer — the completion callback
    // will apply the current settings values when all grabs finish.
    if (m_registrationInProgress) {
        qCDebug(lcShortcuts) << "updateShortcuts: key grab activation in progress, skipping";
        m_settingsDirty = true;
        return;
    }

    // Refresh all shortcuts from current settings values
    qCInfo(lcShortcuts) << "Updating all shortcuts from settings";

    // Core shortcuts
    updateEditorShortcut();
    updateSettingsShortcut();
    updatePreviousLayoutShortcut();
    updateNextLayoutShortcut();

    // Quick layout shortcuts (0-8 internally, 1-9 for users)
    for (int i = 0; i < 9; ++i) {
        updateQuickLayoutShortcut(i);
    }

    // Keyboard navigation shortcuts
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

    // Snap All Windows shortcut
    updateSnapAllWindowsShortcut();

    // Layout Picker shortcut
    updateLayoutPickerShortcut();

    // Toggle Layout Lock shortcut
    updateToggleLayoutLockShortcut();

    // Virtual Screen Swap shortcuts
    updateSwapVirtualScreenLeftShortcut();
    updateSwapVirtualScreenRightShortcut();
    updateSwapVirtualScreenUpShortcut();
    updateSwapVirtualScreenDownShortcut();

    // Rotate Virtual Screens shortcuts
    updateRotateVirtualScreensClockwiseShortcut();
    updateRotateVirtualScreensCounterclockwiseShortcut();

    // Autotile shortcuts
    updateToggleAutotileShortcut();
    updateFocusMasterShortcut();
    updateSwapMasterShortcut();
    updateIncMasterRatioShortcut();
    updateDecMasterRatioShortcut();
    updateIncMasterCountShortcut();
    updateDecMasterCountShortcut();
    updateRetileShortcut();
}

void ShortcutManager::unregisterShortcuts()
{
    // Cancel any in-flight registration
    m_registrationInProgress = false;
    m_settingsDirty = false;

    // Clear all actions - the backend will unregister automatically when actions are deleted
    // Use direct delete instead of deleteLater() because:
    // 1. Actions have 'this' as parent, so deleteLater() + parent cleanup = double-free risk
    // 2. We're in controlled context (destructor or explicit unregister) where immediate delete is safe

    // Core shortcuts
    DELETE_SHORTCUT(m_editorAction);
    DELETE_SHORTCUT(m_settingsAction);
    DELETE_SHORTCUT(m_previousLayoutAction);
    DELETE_SHORTCUT(m_nextLayoutAction);

    qDeleteAll(m_quickLayoutActions);
    m_quickLayoutActions.clear();

    // Keyboard navigation actions
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

    // Snap All Windows action
    DELETE_SHORTCUT(m_snapAllWindowsAction);

    // Layout Picker action
    DELETE_SHORTCUT(m_layoutPickerAction);

    // Toggle Layout Lock action
    DELETE_SHORTCUT(m_toggleLayoutLockAction);

    // Autotile actions
    DELETE_SHORTCUT(m_toggleAutotileAction);
    DELETE_SHORTCUT(m_focusMasterAction);
    DELETE_SHORTCUT(m_swapMasterAction);
    DELETE_SHORTCUT(m_incMasterRatioAction);
    DELETE_SHORTCUT(m_decMasterRatioAction);
    DELETE_SHORTCUT(m_incMasterCountAction);
    DELETE_SHORTCUT(m_decMasterCountAction);
    DELETE_SHORTCUT(m_retileAction);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Setup Methods
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::setupEditorShortcut()
{
    SETUP_SHORTCUT(m_editorAction, "Open Zone Editor", "open_editor", openEditorShortcut,
                   &ShortcutManager::onOpenEditor);
}

void ShortcutManager::setupSettingsShortcut()
{
    SETUP_SHORTCUT(m_settingsAction, "Open Settings", "open_settings", openSettingsShortcut,
                   &ShortcutManager::onOpenSettings);
}

void ShortcutManager::setupCyclingShortcuts()
{
    SETUP_SHORTCUT(m_previousLayoutAction, "Previous Layout", "previous_layout", previousLayoutShortcut,
                   &ShortcutManager::onPreviousLayout);
    SETUP_SHORTCUT(m_nextLayoutAction, "Next Layout", "next_layout", nextLayoutShortcut,
                   &ShortcutManager::onNextLayout);
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
        ConfigDefaults::quickLayout9Shortcut()};

    for (int i = 0; i < 9; ++i) {
        auto* quickAction = new QAction(PzI18n::tr("Apply Layout %1").arg(i + 1), this);
        quickAction->setObjectName(QStringLiteral("quick_layout_%1").arg(i + 1));
        m_shortcutBackend->setDefaultShortcut(quickAction, QKeySequence(quickLayoutDefaults[i]));
        m_shortcutBackend->setGlobalShortcut(quickAction, QKeySequence(m_settings->quickLayoutShortcut(i)));

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
    SETUP_SHORTCUT(m_moveWindowLeftAction, "Move Window Left", "move_window_left", moveWindowLeftShortcut,
                   &ShortcutManager::onMoveWindowLeft);
    SETUP_SHORTCUT(m_moveWindowRightAction, "Move Window Right", "move_window_right", moveWindowRightShortcut,
                   &ShortcutManager::onMoveWindowRight);
    SETUP_SHORTCUT(m_moveWindowUpAction, "Move Window Up", "move_window_up", moveWindowUpShortcut,
                   &ShortcutManager::onMoveWindowUp);
    SETUP_SHORTCUT(m_moveWindowDownAction, "Move Window Down", "move_window_down", moveWindowDownShortcut,
                   &ShortcutManager::onMoveWindowDown);

    // Focus Zone shortcuts
    SETUP_SHORTCUT(m_focusZoneLeftAction, "Focus Zone Left", "focus_zone_left", focusZoneLeftShortcut,
                   &ShortcutManager::onFocusZoneLeft);
    SETUP_SHORTCUT(m_focusZoneRightAction, "Focus Zone Right", "focus_zone_right", focusZoneRightShortcut,
                   &ShortcutManager::onFocusZoneRight);
    SETUP_SHORTCUT(m_focusZoneUpAction, "Focus Zone Up", "focus_zone_up", focusZoneUpShortcut,
                   &ShortcutManager::onFocusZoneUp);
    SETUP_SHORTCUT(m_focusZoneDownAction, "Focus Zone Down", "focus_zone_down", focusZoneDownShortcut,
                   &ShortcutManager::onFocusZoneDown);

    // Additional navigation shortcuts
    SETUP_SHORTCUT(m_pushToEmptyZoneAction, "Move Window to Empty Zone", "push_to_empty_zone", pushToEmptyZoneShortcut,
                   &ShortcutManager::onPushToEmptyZone);
    SETUP_SHORTCUT(m_restoreWindowSizeAction, "Restore Window Size", "restore_window_size", restoreWindowSizeShortcut,
                   &ShortcutManager::onRestoreWindowSize);
    SETUP_SHORTCUT(m_toggleWindowFloatAction, "Toggle Window Floating", "toggle_window_float",
                   toggleWindowFloatShortcut, &ShortcutManager::onToggleWindowFloat);

    qCInfo(lcShortcuts) << "Navigation shortcuts registered";
}

void ShortcutManager::setupSwapWindowShortcuts()
{
    SETUP_SHORTCUT(m_swapWindowLeftAction, "Swap Window Left", "swap_window_left", swapWindowLeftShortcut,
                   &ShortcutManager::onSwapWindowLeft);
    SETUP_SHORTCUT(m_swapWindowRightAction, "Swap Window Right", "swap_window_right", swapWindowRightShortcut,
                   &ShortcutManager::onSwapWindowRight);
    SETUP_SHORTCUT(m_swapWindowUpAction, "Swap Window Up", "swap_window_up", swapWindowUpShortcut,
                   &ShortcutManager::onSwapWindowUp);
    SETUP_SHORTCUT(m_swapWindowDownAction, "Swap Window Down", "swap_window_down", swapWindowDownShortcut,
                   &ShortcutManager::onSwapWindowDown);

    qCInfo(lcShortcuts) << "Swap window shortcuts registered (Meta+Ctrl+Alt+Arrow)";
}

void ShortcutManager::setupSnapToZoneShortcuts()
{
    // Clear existing actions
    qDeleteAll(m_snapToZoneActions);
    m_snapToZoneActions.clear();

    const QString snapToZoneDefaults[] = {ConfigDefaults::snapToZone1Shortcut(), ConfigDefaults::snapToZone2Shortcut(),
                                          ConfigDefaults::snapToZone3Shortcut(), ConfigDefaults::snapToZone4Shortcut(),
                                          ConfigDefaults::snapToZone5Shortcut(), ConfigDefaults::snapToZone6Shortcut(),
                                          ConfigDefaults::snapToZone7Shortcut(), ConfigDefaults::snapToZone8Shortcut(),
                                          ConfigDefaults::snapToZone9Shortcut()};

    for (int i = 0; i < 9; ++i) {
        auto* snapAction = new QAction(PzI18n::tr("Snap to Zone %1").arg(i + 1), this);
        snapAction->setObjectName(QStringLiteral("snap_to_zone_%1").arg(i + 1));
        m_shortcutBackend->setDefaultShortcut(snapAction, QKeySequence(snapToZoneDefaults[i]));
        m_shortcutBackend->setGlobalShortcut(snapAction, QKeySequence(m_settings->snapToZoneShortcut(i)));

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
    SETUP_SHORTCUT(m_rotateWindowsCounterclockwiseAction, "Rotate Windows Counterclockwise",
                   "rotate_windows_counterclockwise", rotateWindowsCounterclockwiseShortcut,
                   &ShortcutManager::onRotateWindowsCounterclockwise);

    qCInfo(lcShortcuts) << "Rotate windows shortcuts registered (Meta+Ctrl+[ / Meta+Ctrl+])";
}

void ShortcutManager::setupSwapVirtualScreenShortcuts()
{
    SETUP_SHORTCUT(m_swapVirtualScreenLeftAction, "Swap Virtual Screen Left", "swap_virtual_screen_left",
                   swapVirtualScreenLeftShortcut, &ShortcutManager::onSwapVirtualScreenLeft);
    SETUP_SHORTCUT(m_swapVirtualScreenRightAction, "Swap Virtual Screen Right", "swap_virtual_screen_right",
                   swapVirtualScreenRightShortcut, &ShortcutManager::onSwapVirtualScreenRight);
    SETUP_SHORTCUT(m_swapVirtualScreenUpAction, "Swap Virtual Screen Up", "swap_virtual_screen_up",
                   swapVirtualScreenUpShortcut, &ShortcutManager::onSwapVirtualScreenUp);
    SETUP_SHORTCUT(m_swapVirtualScreenDownAction, "Swap Virtual Screen Down", "swap_virtual_screen_down",
                   swapVirtualScreenDownShortcut, &ShortcutManager::onSwapVirtualScreenDown);

    qCInfo(lcShortcuts) << "Swap virtual screen shortcuts registered (Meta+Ctrl+Alt+Shift+Arrow)";
}

void ShortcutManager::setupRotateVirtualScreensShortcuts()
{
    SETUP_SHORTCUT(m_rotateVirtualScreensClockwiseAction, "Rotate Virtual Screens Clockwise",
                   "rotate_virtual_screens_clockwise", rotateVirtualScreensClockwiseShortcut,
                   &ShortcutManager::onRotateVirtualScreensClockwise);
    SETUP_SHORTCUT(m_rotateVirtualScreensCounterclockwiseAction, "Rotate Virtual Screens Counterclockwise",
                   "rotate_virtual_screens_counterclockwise", rotateVirtualScreensCounterclockwiseShortcut,
                   &ShortcutManager::onRotateVirtualScreensCounterclockwise);

    qCInfo(lcShortcuts) << "Rotate virtual screens shortcuts registered (Meta+Ctrl+Alt+] / Meta+Ctrl+Alt+[)";
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
    SETUP_SHORTCUT(m_resnapToNewLayoutAction, "Reapply Layout to Windows", "resnap_to_new_layout",
                   resnapToNewLayoutShortcut, &ShortcutManager::onResnapToNewLayout);

    qCInfo(lcShortcuts) << "Resnap to new layout shortcut registered:" << m_settings->resnapToNewLayoutShortcut();
}

void ShortcutManager::setupSnapAllWindowsShortcut()
{
    SETUP_SHORTCUT(m_snapAllWindowsAction, "Snap All Windows to Zones", "snap_all_windows", snapAllWindowsShortcut,
                   &ShortcutManager::onSnapAllWindows);

    qCInfo(lcShortcuts) << "Snap all windows shortcut registered:" << m_settings->snapAllWindowsShortcut();
}

void ShortcutManager::setupLayoutPickerShortcut()
{
    SETUP_SHORTCUT(m_layoutPickerAction, "Open Layout Picker", "layout_picker", layoutPickerShortcut,
                   &ShortcutManager::onLayoutPicker);

    qCInfo(lcShortcuts) << "Layout picker shortcut registered:" << m_settings->layoutPickerShortcut();
}

void ShortcutManager::setupToggleLayoutLockShortcut()
{
    SETUP_SHORTCUT(m_toggleLayoutLockAction, "Toggle Layout Lock", "toggle_layout_lock", toggleLayoutLockShortcut,
                   &ShortcutManager::onToggleLayoutLock);

    qCInfo(lcShortcuts) << "Toggle layout lock shortcut registered:" << m_settings->toggleLayoutLockShortcut();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Autotile Shortcut Setup
// ═══════════════════════════════════════════════════════════════════════════════

void ShortcutManager::setupAutotileShortcuts()
{
    SETUP_SHORTCUT(m_toggleAutotileAction, "Toggle Autotile", "toggle_autotile", autotileToggleShortcut,
                   &ShortcutManager::onToggleAutotile);
    SETUP_SHORTCUT(m_focusMasterAction, "Focus Master Window", "focus_master", autotileFocusMasterShortcut,
                   &ShortcutManager::onFocusMaster);
    SETUP_SHORTCUT(m_swapMasterAction, "Swap with Master", "swap_master", autotileSwapMasterShortcut,
                   &ShortcutManager::onSwapWithMaster);
    SETUP_SHORTCUT(m_incMasterRatioAction, "Increase Master Ratio", "increase_master_ratio",
                   autotileIncMasterRatioShortcut, &ShortcutManager::onIncreaseMasterRatio);
    SETUP_SHORTCUT(m_decMasterRatioAction, "Decrease Master Ratio", "decrease_master_ratio",
                   autotileDecMasterRatioShortcut, &ShortcutManager::onDecreaseMasterRatio);
    SETUP_SHORTCUT(m_incMasterCountAction, "Increase Master Count", "increase_master_count",
                   autotileIncMasterCountShortcut, &ShortcutManager::onIncreaseMasterCount);
    SETUP_SHORTCUT(m_decMasterCountAction, "Decrease Master Count", "decrease_master_count",
                   autotileDecMasterCountShortcut, &ShortcutManager::onDecreaseMasterCount);
    SETUP_SHORTCUT(m_retileAction, "Retile Windows", "retile", autotileRetileShortcut, &ShortcutManager::onRetile);

    qCInfo(lcShortcuts) << "Autotile shortcuts registered";
}

// Undefine macros to keep them local to this file
#undef SETUP_SHORTCUT
#undef DELETE_SHORTCUT

} // namespace PlasmaZones
