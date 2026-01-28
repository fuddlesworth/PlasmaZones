// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcutmanager.h"
#include "../config/settings.h"
#include "../core/layoutmanager.h"
#include "../core/logging.h"
#include <QAction>
#include <QKeySequence>
#include <KGlobalAccel>
#include <KLocalizedString>

namespace PlasmaZones {

ShortcutManager::ShortcutManager(Settings* settings, LayoutManager* layoutManager, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_layoutManager(layoutManager)
{
    Q_ASSERT(settings);
    Q_ASSERT(layoutManager);

    // Connect to settings changes to update shortcuts dynamically
    connect(m_settings, &Settings::openEditorShortcutChanged, this, &ShortcutManager::updateEditorShortcut);
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
    connect(m_settings, &Settings::previousLayoutShortcutChanged, this, &ShortcutManager::updatePreviousLayoutShortcut);
    connect(m_settings, &Settings::nextLayoutShortcutChanged, this, &ShortcutManager::updateNextLayoutShortcut);

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

    // Snap to Zone by Number shortcuts
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

void ShortcutManager::registerShortcuts()
{
    setupEditorShortcut();
    setupCyclingShortcuts();
    setupQuickLayoutShortcuts();
    setupNavigationShortcuts();
    setupSnapToZoneShortcuts();
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

    // Snap to Zone shortcuts (0-8 internally, 1-9 for users)
    for (int i = 0; i < 9; ++i) {
        updateSnapToZoneShortcut(i);
    }
}

void ShortcutManager::unregisterShortcuts()
{
    // Clear all actions - KGlobalAccel will unregister automatically when actions are deleted
    // Use direct delete instead of deleteLater() because:
    // 1. Actions have 'this' as parent, so deleteLater() + parent cleanup = double-free risk
    // 2. We're in controlled context (destructor or explicit unregister) where immediate delete is safe
    // 3. KGlobalAccel needs synchronous cleanup for proper shortcut unregistration
    delete m_editorAction;
    m_editorAction = nullptr;

    delete m_previousLayoutAction;
    m_previousLayoutAction = nullptr;

    delete m_nextLayoutAction;
    m_nextLayoutAction = nullptr;

    qDeleteAll(m_quickLayoutActions);
    m_quickLayoutActions.clear();

    // Phase 1 Keyboard Navigation actions
    delete m_moveWindowLeftAction;
    m_moveWindowLeftAction = nullptr;

    delete m_moveWindowRightAction;
    m_moveWindowRightAction = nullptr;

    delete m_moveWindowUpAction;
    m_moveWindowUpAction = nullptr;

    delete m_moveWindowDownAction;
    m_moveWindowDownAction = nullptr;

    delete m_focusZoneLeftAction;
    m_focusZoneLeftAction = nullptr;

    delete m_focusZoneRightAction;
    m_focusZoneRightAction = nullptr;

    delete m_focusZoneUpAction;
    m_focusZoneUpAction = nullptr;

    delete m_focusZoneDownAction;
    m_focusZoneDownAction = nullptr;

    delete m_pushToEmptyZoneAction;
    m_pushToEmptyZoneAction = nullptr;

    delete m_restoreWindowSizeAction;
    m_restoreWindowSizeAction = nullptr;

    delete m_toggleWindowFloatAction;
    m_toggleWindowFloatAction = nullptr;

    // Snap to Zone actions
    qDeleteAll(m_snapToZoneActions);
    m_snapToZoneActions.clear();
}

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

void ShortcutManager::setupEditorShortcut()
{
    if (m_editorAction) {
        return;
    }

    m_editorAction = new QAction(i18n("Open Zone Editor"), this);
    m_editorAction->setObjectName(QStringLiteral("open_editor"));
    // Read shortcut from settings instead of hardcoding
    KGlobalAccel::setGlobalShortcut(m_editorAction, QKeySequence(m_settings->openEditorShortcut()));
    connect(m_editorAction, &QAction::triggered, this, &ShortcutManager::onOpenEditor);
}

void ShortcutManager::setupCyclingShortcuts()
{
    if (m_previousLayoutAction) {
        return;
    }

    m_previousLayoutAction = new QAction(i18n("Previous Layout"), this);
    m_previousLayoutAction->setObjectName(QStringLiteral("previous_layout"));
    KGlobalAccel::setGlobalShortcut(m_previousLayoutAction, QKeySequence(m_settings->previousLayoutShortcut()));
    connect(m_previousLayoutAction, &QAction::triggered, this, &ShortcutManager::onPreviousLayout);

    m_nextLayoutAction = new QAction(i18n("Next Layout"), this);
    m_nextLayoutAction->setObjectName(QStringLiteral("next_layout"));
    KGlobalAccel::setGlobalShortcut(m_nextLayoutAction, QKeySequence(m_settings->nextLayoutShortcut()));
    connect(m_nextLayoutAction, &QAction::triggered, this, &ShortcutManager::onNextLayout);
}

void ShortcutManager::setupQuickLayoutShortcuts()
{
    // Clear existing actions - use direct delete for same reasons as unregisterShortcuts()
    qDeleteAll(m_quickLayoutActions);
    m_quickLayoutActions.clear();

    // Register quick layout shortcuts (read from settings)
    for (int i = 0; i < 9; ++i) {
        auto* quickAction = new QAction(i18n("Apply Layout %1", i + 1), this);
        quickAction->setObjectName(QStringLiteral("quick_layout_%1").arg(i + 1));
        // Read shortcut from settings instead of hardcoding
        KGlobalAccel::setGlobalShortcut(quickAction, QKeySequence(m_settings->quickLayoutShortcut(i)));

        // Use lambda to capture the number (1-based for user display)
        const int layoutNumber = i + 1;
        connect(quickAction, &QAction::triggered, this, [this, layoutNumber]() {
            onQuickLayout(layoutNumber);
        });

        m_quickLayoutActions.append(quickAction);
    }
}

void ShortcutManager::updateEditorShortcut()
{
    if (m_editorAction) {
        KGlobalAccel::setGlobalShortcut(m_editorAction, QKeySequence(m_settings->openEditorShortcut()));
    }
}

void ShortcutManager::updatePreviousLayoutShortcut()
{
    if (m_previousLayoutAction) {
        KGlobalAccel::setGlobalShortcut(m_previousLayoutAction, QKeySequence(m_settings->previousLayoutShortcut()));
    }
}

void ShortcutManager::updateNextLayoutShortcut()
{
    if (m_nextLayoutAction) {
        KGlobalAccel::setGlobalShortcut(m_nextLayoutAction, QKeySequence(m_settings->nextLayoutShortcut()));
    }
}

void ShortcutManager::updateQuickLayoutShortcut(int index)
{
    if (index >= 0 && index < m_quickLayoutActions.size()) {
        KGlobalAccel::setGlobalShortcut(m_quickLayoutActions[index],
                                        QKeySequence(m_settings->quickLayoutShortcut(index)));
    }
}

// Phase 1 Keyboard Navigation - Setup
void ShortcutManager::setupNavigationShortcuts()
{
    // Move Window shortcuts
    if (!m_moveWindowLeftAction) {
        m_moveWindowLeftAction = new QAction(i18n("Move Window Left"), this);
        m_moveWindowLeftAction->setObjectName(QStringLiteral("move_window_left"));
        KGlobalAccel::setGlobalShortcut(m_moveWindowLeftAction, QKeySequence(m_settings->moveWindowLeftShortcut()));
        connect(m_moveWindowLeftAction, &QAction::triggered, this, &ShortcutManager::onMoveWindowLeft);
    }

    if (!m_moveWindowRightAction) {
        m_moveWindowRightAction = new QAction(i18n("Move Window Right"), this);
        m_moveWindowRightAction->setObjectName(QStringLiteral("move_window_right"));
        KGlobalAccel::setGlobalShortcut(m_moveWindowRightAction, QKeySequence(m_settings->moveWindowRightShortcut()));
        connect(m_moveWindowRightAction, &QAction::triggered, this, &ShortcutManager::onMoveWindowRight);
    }

    if (!m_moveWindowUpAction) {
        m_moveWindowUpAction = new QAction(i18n("Move Window Up"), this);
        m_moveWindowUpAction->setObjectName(QStringLiteral("move_window_up"));
        KGlobalAccel::setGlobalShortcut(m_moveWindowUpAction, QKeySequence(m_settings->moveWindowUpShortcut()));
        connect(m_moveWindowUpAction, &QAction::triggered, this, &ShortcutManager::onMoveWindowUp);
    }

    if (!m_moveWindowDownAction) {
        m_moveWindowDownAction = new QAction(i18n("Move Window Down"), this);
        m_moveWindowDownAction->setObjectName(QStringLiteral("move_window_down"));
        KGlobalAccel::setGlobalShortcut(m_moveWindowDownAction, QKeySequence(m_settings->moveWindowDownShortcut()));
        connect(m_moveWindowDownAction, &QAction::triggered, this, &ShortcutManager::onMoveWindowDown);
    }

    // Focus Zone shortcuts
    if (!m_focusZoneLeftAction) {
        m_focusZoneLeftAction = new QAction(i18n("Focus Zone Left"), this);
        m_focusZoneLeftAction->setObjectName(QStringLiteral("focus_zone_left"));
        KGlobalAccel::setGlobalShortcut(m_focusZoneLeftAction, QKeySequence(m_settings->focusZoneLeftShortcut()));
        connect(m_focusZoneLeftAction, &QAction::triggered, this, &ShortcutManager::onFocusZoneLeft);
    }

    if (!m_focusZoneRightAction) {
        m_focusZoneRightAction = new QAction(i18n("Focus Zone Right"), this);
        m_focusZoneRightAction->setObjectName(QStringLiteral("focus_zone_right"));
        KGlobalAccel::setGlobalShortcut(m_focusZoneRightAction, QKeySequence(m_settings->focusZoneRightShortcut()));
        connect(m_focusZoneRightAction, &QAction::triggered, this, &ShortcutManager::onFocusZoneRight);
    }

    if (!m_focusZoneUpAction) {
        m_focusZoneUpAction = new QAction(i18n("Focus Zone Up"), this);
        m_focusZoneUpAction->setObjectName(QStringLiteral("focus_zone_up"));
        KGlobalAccel::setGlobalShortcut(m_focusZoneUpAction, QKeySequence(m_settings->focusZoneUpShortcut()));
        connect(m_focusZoneUpAction, &QAction::triggered, this, &ShortcutManager::onFocusZoneUp);
    }

    if (!m_focusZoneDownAction) {
        m_focusZoneDownAction = new QAction(i18n("Focus Zone Down"), this);
        m_focusZoneDownAction->setObjectName(QStringLiteral("focus_zone_down"));
        KGlobalAccel::setGlobalShortcut(m_focusZoneDownAction, QKeySequence(m_settings->focusZoneDownShortcut()));
        connect(m_focusZoneDownAction, &QAction::triggered, this, &ShortcutManager::onFocusZoneDown);
    }

    // Additional navigation shortcuts
    if (!m_pushToEmptyZoneAction) {
        m_pushToEmptyZoneAction = new QAction(i18n("Push to Empty Zone"), this);
        m_pushToEmptyZoneAction->setObjectName(QStringLiteral("push_to_empty_zone"));
        KGlobalAccel::setGlobalShortcut(m_pushToEmptyZoneAction, QKeySequence(m_settings->pushToEmptyZoneShortcut()));
        connect(m_pushToEmptyZoneAction, &QAction::triggered, this, &ShortcutManager::onPushToEmptyZone);
    }

    if (!m_restoreWindowSizeAction) {
        m_restoreWindowSizeAction = new QAction(i18n("Restore Window Size"), this);
        m_restoreWindowSizeAction->setObjectName(QStringLiteral("restore_window_size"));
        KGlobalAccel::setGlobalShortcut(m_restoreWindowSizeAction,
                                        QKeySequence(m_settings->restoreWindowSizeShortcut()));
        connect(m_restoreWindowSizeAction, &QAction::triggered, this, &ShortcutManager::onRestoreWindowSize);
    }

    if (!m_toggleWindowFloatAction) {
        m_toggleWindowFloatAction = new QAction(i18n("Toggle Window Float"), this);
        m_toggleWindowFloatAction->setObjectName(QStringLiteral("toggle_window_float"));
        KGlobalAccel::setGlobalShortcut(m_toggleWindowFloatAction,
                                        QKeySequence(m_settings->toggleWindowFloatShortcut()));
        connect(m_toggleWindowFloatAction, &QAction::triggered, this, &ShortcutManager::onToggleWindowFloat);
    }

    qCInfo(lcShortcuts) << "Navigation shortcuts registered";
}

// Phase 1 Keyboard Navigation - Slot handlers
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

// Phase 1 Keyboard Navigation - Shortcut update handlers
void ShortcutManager::updateMoveWindowLeftShortcut()
{
    if (m_moveWindowLeftAction) {
        KGlobalAccel::setGlobalShortcut(m_moveWindowLeftAction, QKeySequence(m_settings->moveWindowLeftShortcut()));
    }
}

void ShortcutManager::updateMoveWindowRightShortcut()
{
    if (m_moveWindowRightAction) {
        KGlobalAccel::setGlobalShortcut(m_moveWindowRightAction, QKeySequence(m_settings->moveWindowRightShortcut()));
    }
}

void ShortcutManager::updateMoveWindowUpShortcut()
{
    if (m_moveWindowUpAction) {
        KGlobalAccel::setGlobalShortcut(m_moveWindowUpAction, QKeySequence(m_settings->moveWindowUpShortcut()));
    }
}

void ShortcutManager::updateMoveWindowDownShortcut()
{
    if (m_moveWindowDownAction) {
        KGlobalAccel::setGlobalShortcut(m_moveWindowDownAction, QKeySequence(m_settings->moveWindowDownShortcut()));
    }
}

void ShortcutManager::updateFocusZoneLeftShortcut()
{
    if (m_focusZoneLeftAction) {
        KGlobalAccel::setGlobalShortcut(m_focusZoneLeftAction, QKeySequence(m_settings->focusZoneLeftShortcut()));
    }
}

void ShortcutManager::updateFocusZoneRightShortcut()
{
    if (m_focusZoneRightAction) {
        KGlobalAccel::setGlobalShortcut(m_focusZoneRightAction, QKeySequence(m_settings->focusZoneRightShortcut()));
    }
}

void ShortcutManager::updateFocusZoneUpShortcut()
{
    if (m_focusZoneUpAction) {
        KGlobalAccel::setGlobalShortcut(m_focusZoneUpAction, QKeySequence(m_settings->focusZoneUpShortcut()));
    }
}

void ShortcutManager::updateFocusZoneDownShortcut()
{
    if (m_focusZoneDownAction) {
        KGlobalAccel::setGlobalShortcut(m_focusZoneDownAction, QKeySequence(m_settings->focusZoneDownShortcut()));
    }
}

void ShortcutManager::updatePushToEmptyZoneShortcut()
{
    if (m_pushToEmptyZoneAction) {
        KGlobalAccel::setGlobalShortcut(m_pushToEmptyZoneAction, QKeySequence(m_settings->pushToEmptyZoneShortcut()));
    }
}

void ShortcutManager::updateRestoreWindowSizeShortcut()
{
    if (m_restoreWindowSizeAction) {
        KGlobalAccel::setGlobalShortcut(m_restoreWindowSizeAction,
                                        QKeySequence(m_settings->restoreWindowSizeShortcut()));
    }
}

void ShortcutManager::updateToggleWindowFloatShortcut()
{
    if (m_toggleWindowFloatAction) {
        KGlobalAccel::setGlobalShortcut(m_toggleWindowFloatAction,
                                        QKeySequence(m_settings->toggleWindowFloatShortcut()));
    }
}

// Snap to Zone by Number
void ShortcutManager::setupSnapToZoneShortcuts()
{
    // Clear existing actions
    qDeleteAll(m_snapToZoneActions);
    m_snapToZoneActions.clear();

    // Register snap-to-zone shortcuts (Meta+Ctrl+1-9)
    for (int i = 0; i < 9; ++i) {
        auto* snapAction = new QAction(i18n("Snap to Zone %1", i + 1), this);
        snapAction->setObjectName(QStringLiteral("snap_to_zone_%1").arg(i + 1));
        KGlobalAccel::setGlobalShortcut(snapAction, QKeySequence(m_settings->snapToZoneShortcut(i)));

        // Use lambda to capture the zone number (1-based for user display)
        const int zoneNumber = i + 1;
        connect(snapAction, &QAction::triggered, this, [this, zoneNumber]() {
            onSnapToZone(zoneNumber);
        });

        m_snapToZoneActions.append(snapAction);
    }

    qCInfo(lcShortcuts) << "Snap-to-zone shortcuts registered (Meta+Ctrl+1-9)";
}

void ShortcutManager::onSnapToZone(int zoneNumber)
{
    qCDebug(lcShortcuts) << "Snap to zone" << zoneNumber << "triggered";
    Q_EMIT snapToZoneRequested(zoneNumber);
}

void ShortcutManager::updateSnapToZoneShortcut(int index)
{
    if (index >= 0 && index < m_snapToZoneActions.size()) {
        KGlobalAccel::setGlobalShortcut(m_snapToZoneActions[index],
                                        QKeySequence(m_settings->snapToZoneShortcut(index)));
    }
}

} // namespace PlasmaZones
