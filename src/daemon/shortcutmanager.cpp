// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcutmanager.h"
#include "../config/configdefaults.h"
#include "../config/settings.h"
#include "../core/layoutmanager.h"
#include "../core/logging.h"
#include <QAction>
#include <QKeySequence>
#include <QTimer>
#include <KGlobalAccel>
#include <KLocalizedString>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Macros
// ═══════════════════════════════════════════════════════════════════════════════

// Setup a global shortcut: ConfigDefaults for default, m_settings for current.
// Phase 1 (sync): setDefaultShortcut registers action without key grabbing.
// Phase 2 (deferred): queueGlobalShortcut defers setGlobalShortcut to avoid login freeze.
#define SETUP_SHORTCUT(actionMember, i18nName, objectName, getterName, slot)                                           \
    do {                                                                                                               \
        if (!actionMember) {                                                                                           \
            actionMember = new QAction(i18n(i18nName), this);                                                          \
            actionMember->setObjectName(QStringLiteral(objectName));                                                   \
            const QKeySequence defaultShortcut(ConfigDefaults::getterName());                                          \
            const QKeySequence shortcut(m_settings->getterName());                                                     \
            KGlobalAccel::self()->setDefaultShortcut(actionMember, {defaultShortcut});                                 \
            queueGlobalShortcut(actionMember, shortcut);                                                               \
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
{
    Q_ASSERT(settings);
    Q_ASSERT(layoutManager);

    // Connect to settings changes to update shortcuts dynamically
    connect(m_settings, &Settings::openEditorShortcutChanged, this, &ShortcutManager::updateEditorShortcut);
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

    // Phase 1 Keyboard Navigation - connect to settings changes
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
                                  "is already in progress — ignoring";
        return;
    }
    m_registrationInProgress = true;

    // Phase 1: Register all actions with setDefaultShortcut (fast — no key
    // grabbing contention). All shortcuts are queued for Phase 2 which calls
    // setGlobalShortcut (with SetPresent flag) to trigger actual key grabs.
    setupEditorShortcut();
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
    setupAutotileShortcuts();

    qCInfo(lcShortcuts) << "Phase 1 complete:" << m_deferredQueue.size() << "deferred setGlobalShortcut calls queued";

    // Phase 2: Process deferred setGlobalShortcut calls one at a time,
    // yielding to the event loop between each blocking D-Bus call.
    // Each call blocks for ~600ms max under login contention, but the
    // event loop stays responsive between calls — no desktop freeze.
    if (m_deferredQueue.isEmpty()) {
        m_registrationInProgress = false;
        Q_EMIT shortcutsRegistered();
    } else {
        processNextDeferredShortcut();
    }
}

void ShortcutManager::queueGlobalShortcut(QAction* action, const QKeySequence& shortcut)
{
    // setDefaultShortcut (Phase 1) stores defaults in kglobalacceld's database
    // but does NOT trigger key grabs (daemon returns early for IsDefault flag).
    // setGlobalShortcut sends the SetPresent flag which calls setIsPresent(true)
    // on the daemon side, actually grabbing the key. This is required for ALL
    // shortcuts, not just customized ones.
    m_deferredQueue.enqueue({action, shortcut});
}

void ShortcutManager::processNextDeferredShortcut()
{
    if (m_deferredQueue.isEmpty()) {
        m_registrationInProgress = false;
        qCInfo(lcShortcuts) << "Phase 2 complete: all deferred shortcuts registered";
        Q_EMIT shortcutsRegistered();

        // If settings changed during Phase 2, apply now
        if (m_settingsDirty) {
            m_settingsDirty = false;
            updateShortcuts();
        }
        return;
    }

    const DeferredShortcut entry = m_deferredQueue.dequeue();
    if (entry.action) {
        KGlobalAccel::setGlobalShortcut(entry.action, entry.shortcut);
    }

    // Yield to the event loop before processing the next one.
    // Safe: QTimer::singleShot with a QObject* receiver automatically
    // cancels the invocation if the receiver is destroyed before it fires.
    QTimer::singleShot(0, this, &ShortcutManager::processNextDeferredShortcut);
}

void ShortcutManager::updateShortcuts()
{
    // Called when settingsChanged() is emitted (e.g., after KCM reload)
    // If deferred registration is still in progress, skip — Phase 2 will
    // apply the current settings values when it runs.
    if (m_registrationInProgress) {
        qCDebug(lcShortcuts) << "Skipping updateShortcuts() — deferred registration in progress";
        m_settingsDirty = true;
        return;
    }

    // Refresh all shortcuts from current settings values
    qCInfo(lcShortcuts) << "Updating all shortcuts from settings";

    // Core shortcuts
    updateEditorShortcut();
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
    // Cancel any in-flight deferred registration
    m_deferredQueue.clear();
    m_registrationInProgress = false;
    m_settingsDirty = false;

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
        auto* quickAction = new QAction(i18n("Apply Layout %1", i + 1), this);
        quickAction->setObjectName(QStringLiteral("quick_layout_%1").arg(i + 1));
        KGlobalAccel::self()->setDefaultShortcut(quickAction, {QKeySequence(quickLayoutDefaults[i])});
        queueGlobalShortcut(quickAction, QKeySequence(m_settings->quickLayoutShortcut(i)));

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
    SETUP_SHORTCUT(m_pushToEmptyZoneAction, "Push to Empty Zone", "push_to_empty_zone", pushToEmptyZoneShortcut,
                   &ShortcutManager::onPushToEmptyZone);
    SETUP_SHORTCUT(m_restoreWindowSizeAction, "Restore Window Size", "restore_window_size", restoreWindowSizeShortcut,
                   &ShortcutManager::onRestoreWindowSize);
    SETUP_SHORTCUT(m_toggleWindowFloatAction, "Toggle Window Float", "toggle_window_float", toggleWindowFloatShortcut,
                   &ShortcutManager::onToggleWindowFloat);

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
        auto* snapAction = new QAction(i18n("Snap to Zone %1", i + 1), this);
        snapAction->setObjectName(QStringLiteral("snap_to_zone_%1").arg(i + 1));
        KGlobalAccel::self()->setDefaultShortcut(snapAction, {QKeySequence(snapToZoneDefaults[i])});
        queueGlobalShortcut(snapAction, QKeySequence(m_settings->snapToZoneShortcut(i)));

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

    qCInfo(lcShortcuts) << "Resnap to new layout shortcut registered (" << m_settings->resnapToNewLayoutShortcut()
                        << ")";
}

void ShortcutManager::setupSnapAllWindowsShortcut()
{
    SETUP_SHORTCUT(m_snapAllWindowsAction, "Snap All Windows to Zones", "snap_all_windows", snapAllWindowsShortcut,
                   &ShortcutManager::onSnapAllWindows);

    qCInfo(lcShortcuts) << "Snap all windows shortcut registered (" << m_settings->snapAllWindowsShortcut() << ")";
}

void ShortcutManager::setupLayoutPickerShortcut()
{
    SETUP_SHORTCUT(m_layoutPickerAction, "Open Layout Picker", "layout_picker", layoutPickerShortcut,
                   &ShortcutManager::onLayoutPicker);

    qCInfo(lcShortcuts) << "Layout picker shortcut registered (" << m_settings->layoutPickerShortcut() << ")";
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
