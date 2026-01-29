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
    // TODO: WindowTrackingService needs windowSnapped/windowUnsnapped signals
    // For now, AutotileEngine must be notified externally via onWindowAdded/onWindowRemoved
    // or by monitoring WindowTrackingService::windowZoneChanged
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

    // Set algorithm on engine (handles registry lookup)
    setAlgorithm(settings->autotileAlgorithm());

    // Set enabled state last (may trigger tiling)
    setEnabled(settings->autotileEnabled());

    qCInfo(lcAutotile) << "Settings synced - enabled:" << settings->autotileEnabled()
                       << "algorithm:" << settings->autotileAlgorithm();
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

    // Enabled state - immediate effect, no debounce needed
    connect(settings, &Settings::autotileEnabledChanged, this, [this]() {
        if (m_settings) {
            setEnabled(m_settings->autotileEnabled());
        }
    });

    // Algorithm change - immediate effect
    connect(settings, &Settings::autotileAlgorithmChanged, this, [this]() {
        if (m_settings) {
            m_config->algorithmId = m_settings->autotileAlgorithm();
            setAlgorithm(m_settings->autotileAlgorithm());
        }
    });

    // Settings that require retile - use debounced timer
    connect(settings, &Settings::autotileSplitRatioChanged, this, [this]() {
        if (m_settings) {
            m_config->splitRatio = m_settings->autotileSplitRatio();
            scheduleSettingsRetile();
        }
    });

    connect(settings, &Settings::autotileMasterCountChanged, this, [this]() {
        if (m_settings) {
            m_config->masterCount = m_settings->autotileMasterCount();
            scheduleSettingsRetile();
        }
    });

    connect(settings, &Settings::autotileInnerGapChanged, this, [this]() {
        if (m_settings) {
            m_config->innerGap = m_settings->autotileInnerGap();
            scheduleSettingsRetile();
        }
    });

    connect(settings, &Settings::autotileOuterGapChanged, this, [this]() {
        if (m_settings) {
            m_config->outerGap = m_settings->autotileOuterGap();
            scheduleSettingsRetile();
        }
    });

    connect(settings, &Settings::autotileSmartGapsChanged, this, [this]() {
        if (m_settings) {
            m_config->smartGaps = m_settings->autotileSmartGaps();
            scheduleSettingsRetile();
        }
    });

    // Settings that don't require retile - just update config
    connect(settings, &Settings::autotileFocusNewWindowsChanged, this, [this]() {
        if (m_settings) {
            m_config->focusNewWindows = m_settings->autotileFocusNewWindows();
        }
    });

    connect(settings, &Settings::autotileInsertPositionChanged, this, [this]() {
        if (m_settings) {
            m_config->insertPosition = static_cast<AutotileConfig::InsertPosition>(
                m_settings->autotileInsertPositionInt());
        }
    });
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
    const QString screenName = m_windowToScreen.value(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    TilingState *state = stateForScreen(screenName);
    if (!state) {
        return;
    }

    const bool promoted = state->moveToFront(windowId);
    retileAfterOperation(screenName, promoted);
}

void AutotileEngine::demoteFromMaster(const QString &windowId)
{
    const QString screenName = m_windowToScreen.value(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    TilingState *state = stateForScreen(screenName);
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
// Event handlers
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
    // Update focused window in tiling state
    const QString screenName = m_windowToScreen.value(windowId);
    if (screenName.isEmpty()) {
        qWarning() << "AutotileEngine::onWindowFocused: unknown window" << windowId;
        return;
    }

    TilingState *state = stateForScreen(screenName);
    if (state) {
        state->setFocusedWindow(windowId);
    }
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

    // Check if window already tracked
    if (m_windowToScreen.contains(windowId)) {
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

        // TODO: Actually move the window via KWin script or D-Bus
        // For now, just emit the signal
        Q_EMIT windowTiled(windowId, geometry);
    }
}

bool AutotileEngine::shouldTileWindow(const QString &windowId) const
{
    Q_UNUSED(windowId)

    // TODO: Check exclusion rules from config
    // - Window class exclusions
    // - Floating state
    // - Dialog/transient windows
    // - Fullscreen windows

    return true;
}

QString AutotileEngine::screenForWindow(const QString &windowId) const
{
    // Check if already tracked
    if (m_windowToScreen.contains(windowId)) {
        return m_windowToScreen.value(windowId);
    }

    // TODO: Get screen from window geometry via KWin
    // For now, return primary screen
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

} // namespace PlasmaZones
