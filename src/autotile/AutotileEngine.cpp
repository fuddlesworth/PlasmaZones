// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Qt headers
#include <QDebug>
#include <QScreen>

// Project headers
#include "AutotileEngine.h"
#include "AlgorithmRegistry.h"
#include "AutotileConfig.h"
#include "TilingAlgorithm.h"
#include "TilingState.h"
#include "config/settings.h"
#include "core/constants.h"
#include "core/layout.h"
#include "core/layoutmanager.h"
#include "core/logging.h"
#include "core/screenmanager.h"
#include "core/windowtrackingservice.h"
#include "core/zone.h"

namespace PlasmaZones {

AutotileEngine::AutotileEngine(LayoutManager *layoutManager,
                               WindowTrackingService *windowTracker,
                               ScreenManager *screenManager,
                               QObject *parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_windowTracker(windowTracker)
    , m_screenManager(screenManager)
    , m_config(std::make_unique<AutotileConfig>())
    , m_algorithmId(AlgorithmRegistry::defaultAlgorithmId())
{
    connectSignals();

    // Configure settings retile debounce timer
    // Coalesces rapid settings changes (e.g., slider adjustments) into single retile
    m_settingsRetileTimer.setSingleShot(true);
    m_settingsRetileTimer.setInterval(100); // 100ms debounce
    connect(&m_settingsRetileTimer, &QTimer::timeout, this, &AutotileEngine::processSettingsRetile);
}

AutotileEngine::~AutotileEngine() = default;

// ═══════════════════════════════════════════════════════════════════════════════
// Signal connections
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::connectSignals()
{
    // Window tracking signals
    // Primary window events (open/close/focus) are received via public methods:
    // windowOpened(), windowClosed(), windowFocused() - connected by Daemon to
    // WindowTrackingAdaptor signals. This connection also handles zone changes:
    if (m_windowTracker) {
        // Use windowZoneChanged as a proxy until dedicated signals are added
        connect(m_windowTracker, &WindowTrackingService::windowZoneChanged,
                this, [this](const QString &windowId, const QString &zoneId) {
                    if (zoneId.isEmpty()) {
                        onWindowRemoved(windowId);
                    } else {
                        // Window was assigned to a zone - treat as added if not already tracked
                        if (!m_windowToScreen.contains(windowId)) {
                            onWindowAdded(windowId);
                        }
                    }
                });
    }

    // Screen geometry changes
    if (m_screenManager) {
        connect(m_screenManager, &ScreenManager::availableGeometryChanged,
                this, [this](QScreen *screen, const QRect &) {
                    if (screen) {
                        onScreenGeometryChanged(screen->name());
                    }
                });
    }

    // Layout changes
    if (m_layoutManager) {
        connect(m_layoutManager, &LayoutManager::activeLayoutChanged,
                this, &AutotileEngine::onLayoutChanged);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Enable/disable
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileEngine::isEnabled() const noexcept
{
    return m_enabled;
}

void AutotileEngine::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    Q_EMIT enabledChanged(enabled);

    if (enabled) {
        // Retile all screens when enabling
        retile();
    }
}

void AutotileEngine::activate()
{
    // Always emit enabledChanged(true) to ensure KWin effect registers windows,
    // even if autotiling is already enabled. This handles:
    // 1. Race condition at startup (effect may not be connected when initial signal fires)
    // 2. User selecting an autotile layout when already in autotile mode
    m_enabled = true;
    Q_EMIT enabledChanged(true);
    retile();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Algorithm selection
// ═══════════════════════════════════════════════════════════════════════════════

QString AutotileEngine::algorithm() const noexcept
{
    return m_algorithmId;
}

void AutotileEngine::setAlgorithm(const QString &algorithmId)
{
    // Validate algorithm exists
    auto *registry = AlgorithmRegistry::instance();
    QString newId = algorithmId;

    if (!registry->hasAlgorithm(newId)) {
        qWarning() << "AutotileEngine: unknown algorithm" << newId
                   << "- falling back to default";
        newId = AlgorithmRegistry::defaultAlgorithmId();
    }

    if (m_algorithmId == newId) {
        return;
    }

    m_algorithmId = newId;
    Q_EMIT algorithmChanged(m_algorithmId);

    // Retile with new algorithm if enabled
    if (m_enabled) {
        retile();
    }
}

TilingAlgorithm *AutotileEngine::currentAlgorithm() const
{
    return AlgorithmRegistry::instance()->algorithm(m_algorithmId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tiling state access
// ═══════════════════════════════════════════════════════════════════════════════

TilingState *AutotileEngine::stateForScreen(const QString &screenName)
{
    // Validate screenName - don't create state for empty name
    if (screenName.isEmpty()) {
        qWarning() << "AutotileEngine::stateForScreen: empty screen name";
        return nullptr;
    }

    auto it = m_screenStates.find(screenName);
    if (it != m_screenStates.end()) {
        return it.value();
    }

    // Create new state for this screen with parent ownership
    auto *state = new TilingState(screenName, this);

    // Initialize with config defaults
    state->setMasterCount(m_config->masterCount);
    state->setSplitRatio(m_config->splitRatio);

    m_screenStates.insert(screenName, state);
    return state;
}

AutotileConfig *AutotileEngine::config() const noexcept
{
    return m_config.get();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Settings synchronization
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::syncFromSettings(Settings *settings)
{
    if (!settings) {
        return;
    }

    m_settings = settings;

    // Store target enabled state and temporarily disable to prevent double-retile
    // during configuration. setAlgorithm() and setEnabled() both trigger retile(),
    // so we configure everything first, then enable once at the end.
    const bool targetEnabled = settings->autotileEnabled();
    const bool wasEnabled = m_enabled;
    if (wasEnabled) {
        m_enabled = false; // Bypass setEnabled() to avoid emitting signal
    }

    // Apply all settings to config (single source of truth for mapping)
    m_config->algorithmId = settings->autotileAlgorithm();
    m_config->splitRatio = settings->autotileSplitRatio();
    m_config->masterCount = settings->autotileMasterCount();
    m_config->innerGap = settings->autotileInnerGap();
    m_config->outerGap = settings->autotileOuterGap();
    m_config->focusNewWindows = settings->autotileFocusNewWindows();
    m_config->smartGaps = settings->autotileSmartGaps();
    m_config->insertPosition = static_cast<AutotileConfig::InsertPosition>(
        settings->autotileInsertPositionInt());

    // Additional settings
    m_config->focusFollowsMouse = settings->autotileFocusFollowsMouse();
    m_config->respectMinimumSize = settings->autotileRespectMinimumSize();
    m_config->showActiveBorder = settings->autotileShowActiveBorder();
    m_config->activeBorderWidth = settings->autotileActiveBorderWidth();
    m_config->monocleHideOthers = settings->autotileMonocleHideOthers();
    m_config->monocleShowTabs = settings->autotileMonocleShowTabs();

    // Active border color: use system highlight color if enabled, else custom color
    if (settings->autotileUseSystemBorderColor()) {
        m_config->activeBorderColor = settings->highlightColor();
    } else {
        m_config->activeBorderColor = settings->autotileActiveBorderColor();
    }

    // Set algorithm on engine (won't retile since m_enabled is false)
    m_algorithmId = settings->autotileAlgorithm();
    // Validate algorithm exists
    auto *registry = AlgorithmRegistry::instance();
    if (!registry->hasAlgorithm(m_algorithmId)) {
        qCWarning(lcAutotile) << "Unknown algorithm" << m_algorithmId << "- using default";
        m_algorithmId = AlgorithmRegistry::defaultAlgorithmId();
    }

    // Now set enabled state (will trigger single retile if enabling)
    setEnabled(targetEnabled);

    qCInfo(lcAutotile) << "Settings synced - enabled:" << targetEnabled
                       << "algorithm:" << m_algorithmId;
}

void AutotileEngine::connectToSettings(Settings *settings)
{
    if (!settings) {
        return;
    }

    // Guard against double-connection
    if (m_settings == settings) {
        qCDebug(lcAutotile) << "Already connected to settings, skipping";
        return;
    }

    // Disconnect from previous settings if any
    if (m_settings) {
        disconnect(m_settings, nullptr, this, nullptr);
        qCDebug(lcAutotile) << "Disconnected from previous settings";
    }

    m_settings = settings;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Macros for settings connections
    // ═══════════════════════════════════════════════════════════════════════════════

    // Pattern 1: Update config field + schedule retile
#define CONNECT_SETTING_RETILE(signal, field, getter) \
    connect(settings, &Settings::signal, this, [this]() { \
        if (m_settings) { \
            m_config->field = m_settings->getter(); \
            scheduleSettingsRetile(); \
        } \
    })

    // Pattern 2: Update config field only (no retile)
#define CONNECT_SETTING_NO_RETILE(signal, field, getter) \
    connect(settings, &Settings::signal, this, [this]() { \
        if (m_settings) { \
            m_config->field = m_settings->getter(); \
        } \
    })

    // ═══════════════════════════════════════════════════════════════════════════════
    // Immediate-effect settings (no debounce)
    // ═══════════════════════════════════════════════════════════════════════════════

    connect(settings, &Settings::autotileEnabledChanged, this, [this]() {
        if (m_settings) {
            setEnabled(m_settings->autotileEnabled());
        }
    });

    connect(settings, &Settings::autotileAlgorithmChanged, this, [this]() {
        if (m_settings) {
            m_config->algorithmId = m_settings->autotileAlgorithm();
            setAlgorithm(m_settings->autotileAlgorithm());
        }
    });

    // ═══════════════════════════════════════════════════════════════════════════════
    // Settings that require retile (debounced)
    // ═══════════════════════════════════════════════════════════════════════════════

    CONNECT_SETTING_RETILE(autotileSplitRatioChanged, splitRatio, autotileSplitRatio);
    CONNECT_SETTING_RETILE(autotileMasterCountChanged, masterCount, autotileMasterCount);
    CONNECT_SETTING_RETILE(autotileInnerGapChanged, innerGap, autotileInnerGap);
    CONNECT_SETTING_RETILE(autotileOuterGapChanged, outerGap, autotileOuterGap);
    CONNECT_SETTING_RETILE(autotileSmartGapsChanged, smartGaps, autotileSmartGaps);
    CONNECT_SETTING_RETILE(autotileRespectMinimumSizeChanged, respectMinimumSize, autotileRespectMinimumSize);

    // ═══════════════════════════════════════════════════════════════════════════════
    // Settings that don't require retile (config update only)
    // ═══════════════════════════════════════════════════════════════════════════════

    CONNECT_SETTING_NO_RETILE(autotileFocusNewWindowsChanged, focusNewWindows, autotileFocusNewWindows);
    CONNECT_SETTING_NO_RETILE(autotileFocusFollowsMouseChanged, focusFollowsMouse, autotileFocusFollowsMouse);
    CONNECT_SETTING_NO_RETILE(autotileShowActiveBorderChanged, showActiveBorder, autotileShowActiveBorder);
    CONNECT_SETTING_NO_RETILE(autotileActiveBorderWidthChanged, activeBorderWidth, autotileActiveBorderWidth);
    CONNECT_SETTING_NO_RETILE(autotileMonocleHideOthersChanged, monocleHideOthers, autotileMonocleHideOthers);
    CONNECT_SETTING_NO_RETILE(autotileMonocleShowTabsChanged, monocleShowTabs, autotileMonocleShowTabs);

    // InsertPosition requires cast
    connect(settings, &Settings::autotileInsertPositionChanged, this, [this]() {
        if (m_settings) {
            m_config->insertPosition = static_cast<AutotileConfig::InsertPosition>(
                m_settings->autotileInsertPositionInt());
        }
    });

    // ═══════════════════════════════════════════════════════════════════════════════
    // Border color settings (conditional logic)
    // ═══════════════════════════════════════════════════════════════════════════════

    connect(settings, &Settings::autotileUseSystemBorderColorChanged, this, [this]() {
        if (m_settings) {
            m_config->activeBorderColor = m_settings->autotileUseSystemBorderColor()
                ? m_settings->highlightColor()
                : m_settings->autotileActiveBorderColor();
        }
    });

    connect(settings, &Settings::autotileActiveBorderColorChanged, this, [this]() {
        if (m_settings && !m_settings->autotileUseSystemBorderColor()) {
            m_config->activeBorderColor = m_settings->autotileActiveBorderColor();
        }
    });

    connect(settings, &Settings::highlightColorChanged, this, [this]() {
        if (m_settings && m_settings->autotileUseSystemBorderColor()) {
            m_config->activeBorderColor = m_settings->highlightColor();
        }
    });

#undef CONNECT_SETTING_RETILE
#undef CONNECT_SETTING_NO_RETILE
}

void AutotileEngine::scheduleSettingsRetile()
{
    m_pendingSettingsRetile = true;
    m_settingsRetileTimer.start();
}

void AutotileEngine::processSettingsRetile()
{
    if (!m_pendingSettingsRetile) {
        return;
    }

    m_pendingSettingsRetile = false;

    // Only retile if autotiling is enabled
    if (m_enabled) {
        retile();
        qCDebug(lcAutotile) << "Settings changed - retiled windows";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Manual tiling operations
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::retile(const QString &screenName)
{
    if (!m_enabled) {
        return;
    }

    if (screenName.isEmpty()) {
        // Retile all screens
        const auto keys = m_screenStates.keys();
        for (const QString &key : keys) {
            recalculateLayout(key);
            applyTiling(key);
        }
    } else {
        recalculateLayout(screenName);
        applyTiling(screenName);
    }
}

void AutotileEngine::swapWindows(const QString &windowId1, const QString &windowId2)
{
    // Early return if same window (no-op)
    if (windowId1 == windowId2) {
        return;
    }

    // Find screens for both windows
    const QString screen1 = m_windowToScreen.value(windowId1);
    const QString screen2 = m_windowToScreen.value(windowId2);

    if (screen1.isEmpty() || screen2.isEmpty()) {
        qWarning() << "AutotileEngine::swapWindows: window not found";
        return;
    }

    if (screen1 != screen2) {
        qWarning() << "AutotileEngine::swapWindows: windows on different screens";
        return;
    }

    TilingState *state = stateForScreen(screen1);
    if (!state) {
        return;
    }

    const bool swapped = state->swapWindowsById(windowId1, windowId2);
    retileAfterOperation(screen1, swapped);
}

void AutotileEngine::promoteToMaster(const QString &windowId)
{
    QString screenName;
    TilingState *state = stateForWindow(windowId, &screenName);
    if (!state) {
        return;
    }

    const bool promoted = state->moveToFront(windowId);
    retileAfterOperation(screenName, promoted);
}

void AutotileEngine::demoteFromMaster(const QString &windowId)
{
    QString screenName;
    TilingState *state = stateForWindow(windowId, &screenName);
    if (!state) {
        return;
    }

    // Move to position after master area (only if currently in master area)
    const int masterCount = state->masterCount();
    const int currentPos = state->windowPosition(windowId);

    bool demoted = false;
    if (currentPos >= 0 && currentPos < masterCount) {
        demoted = state->moveToPosition(windowId, masterCount);
    }

    retileAfterOperation(screenName, demoted);
}

void AutotileEngine::swapFocusedWithMaster()
{
    // Find the screen with a focused window
    QString screenName;
    TilingState *state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenName, state);

    if (windows.isEmpty() || !state) {
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        return;
    }

    // Promote the focused window to master position
    promoteToMaster(focused);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Focus/window cycling
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::focusNext()
{
    emitFocusRequestAtIndex(1);
}

void AutotileEngine::focusPrevious()
{
    emitFocusRequestAtIndex(-1);
}

void AutotileEngine::focusMaster()
{
    emitFocusRequestAtIndex(0, true);
}

void AutotileEngine::emitFocusRequestAtIndex(int indexOffset, bool useFirst)
{
    QString screenName;
    TilingState *state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenName, state);
    if (windows.isEmpty()) {
        return;
    }

    int targetIndex = 0;
    if (!useFirst && state) {
        const QString focused = state->focusedWindow();
        const int currentIndex = qMax(0, windows.indexOf(focused));
        targetIndex = (currentIndex + indexOffset + windows.size()) % windows.size();
    }

    Q_EMIT focusWindowRequested(windows.at(targetIndex));
}

void AutotileEngine::setFocusedWindow(const QString &windowId)
{
    onWindowFocused(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Split ratio adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::increaseMasterRatio(qreal delta)
{
    applyToAllStates([delta](TilingState *state) {
        // setSplitRatio handles clamping internally
        state->setSplitRatio(state->splitRatio() + delta);
    });
}

void AutotileEngine::decreaseMasterRatio(qreal delta)
{
    increaseMasterRatio(-delta);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Master count adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::increaseMasterCount()
{
    applyToAllStates([](TilingState *state) {
        state->setMasterCount(state->masterCount() + 1);
    });
}

void AutotileEngine::decreaseMasterCount()
{
    applyToAllStates([](TilingState *state) {
        if (state->masterCount() > 1) {
            state->setMasterCount(state->masterCount() - 1);
        }
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window rotation and floating
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::rotateWindowOrder(bool clockwise)
{
    QString screenName;
    TilingState *state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenName, state);

    if (windows.size() < 2 || !state) {
        return; // Nothing to rotate with 0 or 1 window
    }

    // Rotate the window order
    bool rotated = state->rotateWindows(clockwise);
    retileAfterOperation(screenName, rotated);

    qCInfo(lcAutotile) << "Rotated windows" << (clockwise ? "clockwise" : "counterclockwise");
}

void AutotileEngine::toggleFocusedWindowFloat()
{
    QString screenName;
    TilingState *state = nullptr;
    tiledWindowsForFocusedScreen(screenName, state);

    if (!state) {
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        return;
    }

    // Toggle floating state
    state->toggleFloating(focused);
    retileAfterOperation(screenName, true);  // Always retile after successful toggle

    bool isNowFloating = state->isFloating(focused);
    qCInfo(lcAutotile) << "Window" << focused << (isNowFloating ? "now floating" : "now tiled");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public window event handlers (called by Daemon via D-Bus signals)
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::windowOpened(const QString &windowId, const QString &screenName)
{
    if (!warnIfEmptyWindowId(windowId, "windowOpened")) {
        return;
    }

    // Store screen mapping so onWindowAdded uses correct screen
    if (!screenName.isEmpty()) {
        m_windowToScreen[windowId] = screenName;
    }
    onWindowAdded(windowId);
}

void AutotileEngine::windowClosed(const QString &windowId)
{
    if (!warnIfEmptyWindowId(windowId, "windowClosed")) {
        return;
    }

    onWindowRemoved(windowId);
}

void AutotileEngine::windowFocused(const QString &windowId, const QString &screenName)
{
    if (!warnIfEmptyWindowId(windowId, "windowFocused")) {
        return;
    }

    // Check if window moved to a different screen
    QString previousScreen = m_windowToScreen.value(windowId);
    bool screenChanged = !screenName.isEmpty() && !previousScreen.isEmpty() && screenName != previousScreen;

    // Update screen mapping - always store when provided
    if (!screenName.isEmpty()) {
        m_windowToScreen[windowId] = screenName;
    }

    // If window moved to a different screen, migrate it between TilingStates
    if (screenChanged && m_enabled) {
        qCInfo(lcAutotile) << "Window" << windowId << "moved from screen" << previousScreen << "to" << screenName;

        // Remove from old screen's TilingState
        TilingState *oldState = m_screenStates.value(previousScreen);
        if (oldState) {
            oldState->removeWindow(windowId);
        }

        // Add to new screen's TilingState (if window should be tiled)
        if (shouldTileWindow(windowId)) {
            TilingState *newState = stateForScreen(screenName);
            if (newState && !newState->tiledWindows().contains(windowId)) {
                newState->addWindow(windowId);
            }
        }

        // Retile both affected screens
        retileAfterOperation(previousScreen, true);
        retileAfterOperation(screenName, true);
    }

    onWindowFocused(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private slot event handlers
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::onWindowAdded(const QString &windowId)
{
    if (!m_enabled || !shouldTileWindow(windowId)) {
        return;
    }

    const QString screenName = screenForWindow(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    const bool inserted = insertWindow(windowId, screenName);
    retileAfterOperation(screenName, inserted);
}

void AutotileEngine::onWindowRemoved(const QString &windowId)
{
    const QString screenName = m_windowToScreen.value(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    removeWindow(windowId);
    retileAfterOperation(screenName, true);
}

void AutotileEngine::onWindowFocused(const QString &windowId)
{
    TilingState *state = stateForWindow(windowId);
    if (!state) {
        qWarning() << "AutotileEngine::onWindowFocused: unknown window" << windowId;
        return;
    }

    state->setFocusedWindow(windowId);
}

void AutotileEngine::onScreenGeometryChanged(const QString &screenName)
{
    if (!m_enabled || !m_screenStates.contains(screenName)) {
        return;
    }

    retileAfterOperation(screenName, true);
}

void AutotileEngine::onLayoutChanged(Layout *layout)
{
    Q_UNUSED(layout)

    // Layout changed, retile all screens
    if (m_enabled) {
        retile();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Internal implementation
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileEngine::insertWindow(const QString &windowId, const QString &screenName)
{
    TilingState *state = stateForScreen(screenName);
    if (!state) {
        qWarning() << "AutotileEngine::insertWindow: failed to get state for screen" << screenName;
        return false;
    }

    // Check if window already in this screen's tiling state
    // Note: We check the TilingState, not m_windowToScreen, because windowOpened()
    // adds to m_windowToScreen before calling this method (for screenForWindow() lookup)
    if (state->tiledWindows().contains(windowId) || state->isFloating(windowId)) {
        return false;
    }

    // Insert based on config preference
    switch (m_config->insertPosition) {
    case AutotileConfig::InsertPosition::End:
        state->addWindow(windowId);
        break;
    case AutotileConfig::InsertPosition::AfterFocused:
        state->insertAfterFocused(windowId);
        break;
    case AutotileConfig::InsertPosition::AsMaster:
        state->addWindow(windowId);
        state->moveToFront(windowId);
        break;
    }

    m_windowToScreen.insert(windowId, screenName);
    return true;
}

void AutotileEngine::removeWindow(const QString &windowId)
{
    const QString screenName = m_windowToScreen.take(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    TilingState *state = m_screenStates.value(screenName);
    if (state) {
        state->removeWindow(windowId);
    }
}

void AutotileEngine::recalculateLayout(const QString &screenName)
{
    if (screenName.isEmpty()) {
        qWarning() << "AutotileEngine::recalculateLayout: empty screen name";
        return;
    }

    TilingState *state = stateForScreen(screenName);
    if (!state) {
        return;
    }

    TilingAlgorithm *algo = currentAlgorithm();
    if (!algo) {
        qWarning() << "AutotileEngine::recalculateLayout: no algorithm set";
        return;
    }

    const int windowCount = state->tiledWindowCount();
    if (windowCount == 0) {
        state->setCalculatedZones({}); // Clear zones when no windows
        return;
    }

    const QRect screen = screenGeometry(screenName);
    if (!screen.isValid()) {
        qWarning() << "AutotileEngine::recalculateLayout: invalid screen geometry";
        return;
    }

    // Calculate zone geometries using the algorithm
    QVector<QRect> zones = algo->calculateZones(windowCount, screen, *state);

    // Validate algorithm returned correct number of zones
    if (zones.size() != windowCount) {
        qWarning() << "AutotileEngine::recalculateLayout: algorithm returned"
                   << zones.size() << "zones for" << windowCount << "windows";
        return;
    }

    // Apply gaps if configured (smartGaps: skip gaps when only one window)
    const bool skipGaps = m_config->smartGaps && windowCount == 1;
    if (!skipGaps && (m_config->innerGap > 0 || m_config->outerGap > 0)) {
        TilingAlgorithm::applyGaps(zones, screen, m_config->innerGap, m_config->outerGap);
    }

    // Store calculated zones in the state for later application
    state->setCalculatedZones(zones);
}

void AutotileEngine::applyTiling(const QString &screenName)
{
    TilingState *state = stateForScreen(screenName);
    if (!state) {
        return;
    }

    const QStringList windows = state->tiledWindows();
    const QVector<QRect> zones = state->calculatedZones();

    if (windows.size() != zones.size()) {
        qWarning() << "AutotileEngine::applyTiling: window/zone count mismatch"
                   << windows.size() << "vs" << zones.size();
        return;
    }

    // Apply each window to its calculated zone
    for (int i = 0; i < windows.size(); ++i) {
        const QString &windowId = windows[i];
        const QRect &geometry = zones[i];

        // Emit signal for D-Bus adaptor to forward to KWin effect
        // Signal chain: windowTiled → AutotileAdaptor::onWindowTiled →
        // D-Bus windowTileRequested → KWin effect applyAutotileGeometry
        Q_EMIT windowTiled(windowId, geometry);
    }
}

bool AutotileEngine::shouldTileWindow(const QString &windowId) const
{
    if (windowId.isEmpty()) {
        return false;
    }

    // Reject malformed window IDs with empty class (transient/popup windows)
    // These look like " : :94483229079904" and cause zone count mismatches
    if (windowId.startsWith(QLatin1Char(' ')) || windowId.startsWith(QLatin1Char(':'))) {
        qCDebug(lcAutotile) << "Rejecting malformed window ID (empty class):" << windowId;
        return false;
    }

    // Check if window is floating in any screen's TilingState
    // (floating windows are excluded from autotiling)
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.value() && it.value()->isFloating(windowId)) {
            qCDebug(lcAutotile) << "Window" << windowId << "is floating, skipping tile";
            return false;
        }
    }

    // Note: Other exclusions (special windows, dialogs, fullscreen, etc.)
    // are already handled by KWin effect's shouldHandleWindow() before
    // sending window events to daemon.

    return true;
}

QString AutotileEngine::screenForWindow(const QString &windowId) const
{
    // Check if already tracked
    if (m_windowToScreen.contains(windowId)) {
        return m_windowToScreen.value(windowId);
    }

    // Fallback to primary screen if screen not known
    // Note: windowOpened() stores screen name before calling onWindowAdded(),
    // so this fallback is only for edge cases (e.g., windows added via other paths)
    if (m_screenManager && m_screenManager->primaryScreen()) {
        return m_screenManager->primaryScreen()->name();
    }

    return QString();
}

QRect AutotileEngine::screenGeometry(const QString &screenName) const
{
    if (!m_screenManager) {
        return QRect();
    }

    QScreen *screen = m_screenManager->screenByName(screenName);
    if (!screen) {
        return QRect();
    }

    return ScreenManager::actualAvailableGeometry(screen);
}

void AutotileEngine::retileAfterOperation(const QString &screenName, bool operationSucceeded)
{
    if (!operationSucceeded) {
        return; // No change, no signal
    }

    if (m_enabled) {
        recalculateLayout(screenName);
        applyTiling(screenName);
        Q_EMIT tilingChanged(screenName);
    }
}

QStringList AutotileEngine::tiledWindowsForFocusedScreen(QString &outScreenName, TilingState *&outState) const
{
    outState = nullptr;

    // Find screen with a focused window by checking all TilingStates
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        TilingState *state = it.value();
        if (state && !state->focusedWindow().isEmpty()) {
            outScreenName = it.key();
            outState = state;
            return state->tiledWindows();
        }
    }

    // No focused window found - fallback to primary screen if available
    if (m_screenManager && m_screenManager->primaryScreen()) {
        outScreenName = m_screenManager->primaryScreen()->name();
        if (m_screenStates.contains(outScreenName)) {
            TilingState *state = m_screenStates.value(outScreenName);
            if (state) {
                outState = state;
                return state->tiledWindows();
            }
        }
    }

    outScreenName.clear();
    return {};
}

void AutotileEngine::applyToAllStates(const std::function<void(TilingState *)> &operation)
{
    if (m_screenStates.isEmpty()) {
        return; // No states to modify
    }

    for (TilingState *state : m_screenStates) {
        if (state) {
            operation(state);
        }
    }

    if (m_enabled) {
        retile();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Methods
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileEngine::warnIfEmptyWindowId(const QString& windowId, const char* operation) const
{
    if (windowId.isEmpty()) {
        qCWarning(lcAutotile) << operation << "called with empty windowId";
        return false;
    }
    return true;
}

TilingState* AutotileEngine::stateForWindow(const QString& windowId, QString* outScreenName)
{
    const QString screenName = m_windowToScreen.value(windowId);
    if (screenName.isEmpty()) {
        if (outScreenName) {
            outScreenName->clear();
        }
        return nullptr;
    }

    if (outScreenName) {
        *outScreenName = screenName;
    }
    return stateForScreen(screenName);
}

} // namespace PlasmaZones
