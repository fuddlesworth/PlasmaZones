// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AutotileEngine.h"
#include "AlgorithmRegistry.h"
#include "AutotileConfig.h"
#include "TilingAlgorithm.h"
#include "TilingState.h"
#include "core/constants.h"
#include "core/layout.h"
#include "core/layoutmanager.h"
#include "core/screenmanager.h"
#include "core/windowtrackingservice.h"
#include "core/zone.h"

#include <QDebug>
#include <QScreen>

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
    auto it = m_screenStates.find(screenName);
    if (it != m_screenStates.end()) {
        return it->second.get();
    }

    // Create new state for this screen
    auto state = std::make_unique<TilingState>(screenName);

    // Initialize with config defaults
    state->setMasterCount(m_config->masterCount);
    state->setSplitRatio(m_config->splitRatio);

    TilingState *ptr = state.get();
    m_screenStates.emplace(screenName, std::move(state));
    return ptr;
}

AutotileConfig *AutotileEngine::config() const noexcept
{
    return m_config.get();
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
        for (const auto &[key, state] : m_screenStates) {
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
    state->swapWindowsById(windowId1, windowId2);

    if (m_enabled) {
        applyTiling(screen1);
    }

    Q_EMIT tilingChanged(screen1);
}

void AutotileEngine::promoteToMaster(const QString &windowId)
{
    const QString screenName = m_windowToScreen.value(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    TilingState *state = stateForScreen(screenName);
    state->moveToFront(windowId);

    if (m_enabled) {
        applyTiling(screenName);
    }

    Q_EMIT tilingChanged(screenName);
}

void AutotileEngine::demoteFromMaster(const QString &windowId)
{
    const QString screenName = m_windowToScreen.value(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    TilingState *state = stateForScreen(screenName);

    // Move to position after master area
    const int masterCount = state->masterCount();
    if (state->windowPosition(windowId) < masterCount) {
        state->moveToPosition(windowId, masterCount);
    }

    if (m_enabled) {
        applyTiling(screenName);
    }

    Q_EMIT tilingChanged(screenName);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Focus/window cycling
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::focusNext()
{
    // Get currently focused window's screen
    // TODO: Get focused window from WindowTrackingService when API is added
    const QString focused = QString(); // Placeholder until WindowTrackingService supports focusedWindow()
    if (focused.isEmpty()) {
        return;
    }

    const QString screenName = m_windowToScreen.value(focused);
    if (screenName.isEmpty()) {
        return;
    }

    TilingState *state = stateForScreen(screenName);
    const QStringList windows = state->tiledWindows();

    if (windows.isEmpty()) {
        return;
    }

    int currentIndex = windows.indexOf(focused);
    int nextIndex = (currentIndex + 1) % windows.size();

    // TODO: Actually focus the window via KWin script or D-Bus
    Q_UNUSED(nextIndex)
}

void AutotileEngine::focusPrevious()
{
    // TODO: Get focused window from WindowTrackingService when API is added
    const QString focused = QString(); // Placeholder until WindowTrackingService supports focusedWindow()
    if (focused.isEmpty()) {
        return;
    }

    const QString screenName = m_windowToScreen.value(focused);
    if (screenName.isEmpty()) {
        return;
    }

    TilingState *state = stateForScreen(screenName);
    const QStringList windows = state->tiledWindows();

    if (windows.isEmpty()) {
        return;
    }

    int currentIndex = windows.indexOf(focused);
    int prevIndex = (currentIndex - 1 + windows.size()) % windows.size();

    // TODO: Actually focus the window via KWin script or D-Bus
    Q_UNUSED(prevIndex)
}

void AutotileEngine::focusMaster()
{
    // Focus the first window in the tiling order (master)
    // TODO: Get focused window from WindowTrackingService when API is added
    const QString focused = QString(); // Placeholder until WindowTrackingService supports focusedWindow()
    const QString screenName = focused.isEmpty() ? QString() : m_windowToScreen.value(focused);

    if (screenName.isEmpty()) {
        return;
    }

    TilingState *state = stateForScreen(screenName);
    const QStringList windows = state->tiledWindows();

    if (!windows.isEmpty()) {
        // TODO: Actually focus the window via KWin script or D-Bus
        Q_UNUSED(windows.first())
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Split ratio adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::increaseMasterRatio(qreal delta)
{
    // Adjust for all screens (or could be per-focused-screen)
    for (auto &[key, statePtr] : m_screenStates) {
        TilingState *state = statePtr.get();
        qreal newRatio = state->splitRatio() + delta;
        newRatio = std::clamp(newRatio, AutotileDefaults::MinSplitRatio,
                              AutotileDefaults::MaxSplitRatio);
        state->setSplitRatio(newRatio);
    }

    if (m_enabled) {
        retile();
    }
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
    for (auto &[key, statePtr] : m_screenStates) {
        TilingState *state = statePtr.get();
        state->setMasterCount(state->masterCount() + 1);
    }

    if (m_enabled) {
        retile();
    }
}

void AutotileEngine::decreaseMasterCount()
{
    for (auto &[key, statePtr] : m_screenStates) {
        TilingState *state = statePtr.get();
        if (state->masterCount() > 1) {
            state->setMasterCount(state->masterCount() - 1);
        }
    }

    if (m_enabled) {
        retile();
    }
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

    insertWindow(windowId, screenName);
    recalculateLayout(screenName);
    applyTiling(screenName);

    Q_EMIT tilingChanged(screenName);
}

void AutotileEngine::onWindowRemoved(const QString &windowId)
{
    const QString screenName = m_windowToScreen.value(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    removeWindow(windowId);

    if (m_enabled) {
        recalculateLayout(screenName);
        applyTiling(screenName);
        Q_EMIT tilingChanged(screenName);
    }
}

void AutotileEngine::onWindowFocused(const QString &windowId)
{
    // Update focused window in tiling state
    const QString screenName = m_windowToScreen.value(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    TilingState *state = stateForScreen(screenName);
    state->setFocusedWindow(windowId);
}

void AutotileEngine::onScreenGeometryChanged(const QString &screenName)
{
    if (!m_enabled) {
        return;
    }

    if (m_screenStates.count(screenName) > 0) {
        recalculateLayout(screenName);
        applyTiling(screenName);
        Q_EMIT tilingChanged(screenName);
    }
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

void AutotileEngine::insertWindow(const QString &windowId, const QString &screenName)
{
    TilingState *state = stateForScreen(screenName);

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
}

void AutotileEngine::removeWindow(const QString &windowId)
{
    const QString screenName = m_windowToScreen.take(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    if (auto it = m_screenStates.find(screenName); it != m_screenStates.end()) {
        it->second->removeWindow(windowId);
    }
}

void AutotileEngine::recalculateLayout(const QString &screenName)
{
    TilingState *state = stateForScreen(screenName);
    TilingAlgorithm *algo = currentAlgorithm();

    if (!algo) {
        qWarning() << "AutotileEngine::recalculateLayout: no algorithm set";
        return;
    }

    const int windowCount = state->tiledWindowCount();
    if (windowCount == 0) {
        return;
    }

    const QRect screen = screenGeometry(screenName);
    if (!screen.isValid()) {
        qWarning() << "AutotileEngine::recalculateLayout: invalid screen geometry";
        return;
    }

    // Calculate zone geometries using the algorithm
    QVector<QRect> zones = algo->calculateZones(windowCount, screen, *state);

    // Apply gaps if configured
    if (m_config->innerGap > 0 || m_config->outerGap > 0) {
        TilingAlgorithm::applyGaps(zones, screen, m_config->innerGap, m_config->outerGap);
    }

    // Store calculated zones in the state for later application
    state->setCalculatedZones(zones);
}

void AutotileEngine::applyTiling(const QString &screenName)
{
    TilingState *state = stateForScreen(screenName);
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

} // namespace PlasmaZones
