// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Qt headers
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>

// Project headers
#include "AutotileEngine.h"
#include "AlgorithmRegistry.h"
#include "core/geometryutils.h"
#include "AutotileConfig.h"
#include "NavigationController.h"
#include "PerScreenConfigResolver.h"
#include "SettingsBridge.h"
#include "TilingAlgorithm.h"
#include "TilingState.h"
#include "core/constants.h"
#include "core/layout.h"
#include "core/layoutmanager.h"
#include "core/logging.h"
#include "core/screenmanager.h"
#include "core/windowtrackingservice.h"
#include "core/zone.h"

namespace PlasmaZones {

namespace {
// Safety timeout for pending initial window orders that never arrive via D-Bus.
// If windows fail to open (e.g., app crash during startup), this prevents
// m_pendingInitialOrders from leaking state indefinitely.
constexpr int PendingOrderTimeoutMs = 10000;
} // namespace

AutotileEngine::AutotileEngine(LayoutManager* layoutManager, WindowTrackingService* windowTracker,
                               ScreenManager* screenManager, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_windowTracker(windowTracker)
    , m_screenManager(screenManager)
    , m_config(std::make_unique<AutotileConfig>())
    , m_configResolver(std::make_unique<PerScreenConfigResolver>(this))
    , m_navigation(std::make_unique<NavigationController>(this))
    , m_settingsBridge(std::make_unique<SettingsBridge>(this))
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
    // Primary window events (open/close/focus) are received via public methods:
    // windowOpened(), windowClosed(), windowFocused() - connected by Daemon to
    // WindowTrackingAdaptor signals. This connection also handles zone changes:
    if (m_windowTracker) {
        // Use windowZoneChanged as a proxy until dedicated signals are added
        connect(m_windowTracker, &WindowTrackingService::windowZoneChanged, this,
                [this](const QString& windowId, const QString& zoneId) {
                    if (m_retiling)
                        return; // Ignore zone changes during retile
                    if (zoneId.isEmpty()) {
                        // Don't remove floating windows — clearing their zone assignment
                        // (e.g., by an external D-Bus caller or legacy code path) would
                        // cause onWindowRemoved to drop the window from autotile. Since
                        // floating windows are still managed by autotile, skip removal.
                        // Note: windowClosed() calls onWindowRemoved() directly and
                        // bypasses this guard, so closed floating windows are cleaned up.
                        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
                            if (it.value() && it.value()->isFloating(windowId)) {
                                return;
                            }
                        }
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
        connect(m_screenManager, &ScreenManager::availableGeometryChanged, this, [this](QScreen* screen, const QRect&) {
            if (screen) {
                onScreenGeometryChanged(screen->name());
            }
        });
    }

    // Layout changes — intentionally NOT connected.
    // Autotile screens are managed by per-screen assignments, not the global
    // active layout. Retile is triggered by setAutotileScreens() and
    // onScreenGeometryChanged() instead.
    // if (m_layoutManager) {
    //     connect(m_layoutManager, &LayoutManager::activeLayoutChanged,
    //             this, &AutotileEngine::onLayoutChanged);
    // }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Enable/disable
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileEngine::isEnabled() const noexcept
{
    return !m_autotileScreens.isEmpty();
}

bool AutotileEngine::isAutotileScreen(const QString& screenName) const
{
    return m_autotileScreens.contains(screenName);
}

void AutotileEngine::setAutotileScreens(const QSet<QString>& screens)
{
    if (m_autotileScreens == screens) {
        return;
    }

    const bool wasEnabled = !m_autotileScreens.isEmpty();
    const QSet<QString> added = screens - m_autotileScreens;

    m_autotileScreens = screens;

    // R1 fix: Retile newly-added screens without requiring pre-existing state.
    // stateForScreen() creates the TilingState lazily, so windows that arrive
    // shortly after (via KWin effect re-notification) have a state ready.
    for (const QString& screenName : added) {
        stateForScreen(screenName);
        // Skip retile if windows are expected to arrive shortly (pending initial
        // order from seedAutotileOrderForScreen). The KWin effect sends windowOpened
        // D-Bus calls after receiving autotileScreensChanged, and each insertWindow
        // schedules its own retile. Retiling an empty screen here produces a wasted
        // empty windowsTiled signal + stagger generation increment, which can interfere
        // with the first real retile's animation timing.
        // For screen hotplug (no pending order), windows are already in the TilingState
        // and the retile is needed to reflow them on the new screen.
        if (!m_pendingInitialOrders.contains(screenName)) {
            scheduleRetileForScreen(screenName);
        }
    }

    // Collect windows from removed screens before pruning, then prune
    QStringList releasedWindows;
    QMutableHashIterator<QString, TilingState*> it(m_screenStates);
    while (it.hasNext()) {
        it.next();
        if (!m_autotileScreens.contains(it.key())) {
            // Save user-floated windows so they stay floating when autotile is re-enabled.
            // Exclude overflow windows — they were auto-floated by maxWindows cap and
            // should tile normally when autotile is re-enabled.
            QSet<QString> screenOverflow = m_overflow.takeForScreen(it.key());
            const QStringList floated = it.value()->floatingWindows();
            for (const QString& fid : floated) {
                if (!screenOverflow.contains(fid)) {
                    m_savedFloatingWindows.insert(fid);
                }
            }
            releasedWindows.append(it.value()->tiledWindows());
            releasedWindows.append(it.value()->floatingWindows());
            m_configResolver->removeOverridesForScreen(it.key());
            m_pendingInitialOrders.remove(it.key());
            it.value()->deleteLater();
            it.remove();
        }
    }
    // Clean up m_windowToScreen entries for released windows BEFORE emitting
    // the signal. Signal handlers (signals.cpp windowsReleasedFromTiling) check
    // zone assignments and floating state — stale m_windowToScreen mappings
    // would cause them to see phantom candidates.
    for (const QString& windowId : std::as_const(releasedWindows)) {
        m_windowToScreen.remove(windowId);
    }

    if (!releasedWindows.isEmpty()) {
        Q_EMIT windowsReleasedFromTiling(releasedWindows);
    }

    // Clean up any remaining overflow entries for removed screens.
    // The floating-windows loop above handles overflow windows that are currently
    // floating in the TilingState, but entries could remain if an overflow window
    // was unfloated by a concurrent retile before reaching this point.
    m_overflow.clearForRemovedScreens(m_autotileScreens);

    // Clear any pending deferred retiles for removed screens
    for (auto pit = m_pendingRetileScreens.begin(); pit != m_pendingRetileScreens.end();) {
        if (!m_autotileScreens.contains(*pit)) {
            pit = m_pendingRetileScreens.erase(pit);
        } else {
            ++pit;
        }
    }

    const bool nowEnabled = !m_autotileScreens.isEmpty();
    if (wasEnabled != nowEnabled) {
        Q_EMIT enabledChanged(nowEnabled);
    }

    Q_EMIT autotileScreensChanged(QStringList(m_autotileScreens.begin(), m_autotileScreens.end()));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Algorithm selection
// ═══════════════════════════════════════════════════════════════════════════════

QString AutotileEngine::algorithm() const noexcept
{
    return m_algorithmId;
}

void AutotileEngine::setAlgorithm(const QString& algorithmId)
{
    // Validate algorithm exists
    auto* registry = AlgorithmRegistry::instance();
    QString newId = algorithmId;

    if (!registry->hasAlgorithm(newId)) {
        qCWarning(lcAutotile) << "AutotileEngine: unknown algorithm" << newId << "- falling back to default";
        newId = AlgorithmRegistry::defaultAlgorithmId();
    }

    if (m_algorithmId == newId) {
        return;
    }

    // Save current split ratio and master count back to per-algorithm fields
    // when switching AWAY from centered-master, so values are remembered.
    if (m_algorithmId == QLatin1String("centered-master")) {
        m_config->centeredMasterSplitRatio = m_config->splitRatio;
        m_config->centeredMasterMasterCount = m_config->masterCount;
    }

    // Always reset split ratio to the new algorithm's default when switching.
    // Different algorithms interpret the same ratio value differently:
    //   MasterStack 0.6 = 60% master width
    //   BSP 0.5 = balanced 50/50 first split
    //   Columns: ignores ratio entirely
    // Preserving a ratio across algorithm switches produces wrong geometries
    // (e.g., Firefox too wide when switching from MasterStack 0.6 to BSP).
    TilingAlgorithm* oldAlgo = registry->algorithm(m_algorithmId);
    TilingAlgorithm* newAlgo = registry->algorithm(newId);
    const int oldMaxWindows = m_config->maxWindows;
    if (oldAlgo && newAlgo) {
        // When switching TO centered-master, use the dedicated per-algorithm values.
        // For other algorithms, reset to their default split ratio.
        if (newId == QLatin1String("centered-master")) {
            m_config->splitRatio = m_config->centeredMasterSplitRatio;
            m_config->masterCount = m_config->centeredMasterMasterCount;
        } else {
            const qreal newDefault = newAlgo->defaultSplitRatio();
            if (!qFuzzyCompare(1.0 + m_config->splitRatio, 1.0 + newDefault)) {
                m_config->splitRatio = newDefault;
            }
        }
        propagateGlobalSplitRatio();
        propagateGlobalMasterCount();

        // Same pattern for maxWindows: if the user hasn't customized it away
        // from the old algorithm's default, reset to the new algorithm's default.
        // Without this, switching from MasterStack (4) to BSP (5) keeps maxWindows=4.
        resetMaxWindowsForAlgorithmSwitch(oldAlgo, newAlgo);
    } else if (newAlgo) {
        // oldAlgo is nullptr (first-ever call or corrupted m_algorithmId).
        // Initialize config from the new algorithm's defaults.
        if (newId == QLatin1String("centered-master")) {
            m_config->splitRatio = m_config->centeredMasterSplitRatio;
            m_config->masterCount = m_config->centeredMasterMasterCount;
        } else {
            m_config->splitRatio = newAlgo->defaultSplitRatio();
        }
        m_config->maxWindows = newAlgo->defaultMaxWindows();
        propagateGlobalSplitRatio();
        propagateGlobalMasterCount();
    }

    // Persist ALL changed fields back to settings to avoid desync between
    // the engine's runtime state and the Settings object. Signal-blocked write
    // prevents recursive corruption (daemon settingsChanged → syncFromSettings →
    // setAlgorithm with stale KCM algo).
    m_settingsBridge->syncAlgorithmToSettings(newId, m_config->splitRatio, m_config->maxWindows, oldMaxWindows);

    m_algorithmId = newId;
    m_config->algorithmId = newId;
    Q_EMIT algorithmChanged(m_algorithmId);

    // Backfill windows when the new algorithm's maxWindows is higher.
    // Guard with maxWindows-increased check to avoid wasted iteration when the
    // new algorithm has a lower or equal limit.
    if (isEnabled()) {
        if (m_config->maxWindows > oldMaxWindows) {
            backfillWindows();
        }
        // Defer retile instead of running immediately. When setAlgorithm is called
        // from applyEntry() or connectToSettings(), the per-screen overrides haven't
        // been updated yet (updateAutotileScreens runs after). An immediate retile
        // would use effectiveAlgorithm() with the stale per-screen override (OLD algo),
        // producing wrong geometries and emitting a bad windowsTiled signal to KWin.
        // Deferring to the next event loop pass ensures per-screen overrides are current.
        for (const QString& screen : m_autotileScreens) {
            scheduleRetileForScreen(screen);
        }
    }
}

TilingAlgorithm* AutotileEngine::currentAlgorithm() const
{
    return AlgorithmRegistry::instance()->algorithm(m_algorithmId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tiling state access
// ═══════════════════════════════════════════════════════════════════════════════

TilingState* AutotileEngine::stateForScreen(const QString& screenName)
{
    // Validate screenName - don't create state for empty name
    if (screenName.isEmpty()) {
        qCWarning(lcAutotile) << "AutotileEngine::stateForScreen: empty screen name";
        return nullptr;
    }

    auto it = m_screenStates.find(screenName);
    if (it != m_screenStates.end()) {
        return it.value();
    }

    // Create new state for this screen with parent ownership
    auto* state = new TilingState(screenName, this);

    // Initialize with config defaults
    state->setMasterCount(m_config->masterCount);
    state->setSplitRatio(m_config->splitRatio);

    m_screenStates.insert(screenName, state);
    return state;
}

AutotileConfig* AutotileEngine::config() const noexcept
{
    return m_config.get();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Zone-ordered window transitions
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::setInitialWindowOrder(const QString& screenName, const QStringList& windowIds)
{
    if (windowIds.isEmpty()) {
        return;
    }
    // Only take effect when the screen's TilingState is empty (no prior windows —
    // including floating — from session restore). Uses windowCount() instead of
    // tiledWindows() to also detect floating-only states.
    TilingState* state = m_screenStates.value(screenName);
    if (state && state->windowCount() > 0) {
        qCDebug(lcAutotile) << "setInitialWindowOrder: screen" << screenName << "already has" << state->windowCount()
                            << "windows, ignoring pre-seeded order";
        return;
    }
    // Warn (but allow) if overwriting a pending order that hasn't been fully consumed
    if (m_pendingInitialOrders.contains(screenName)) {
        qCWarning(lcAutotile) << "setInitialWindowOrder: overwriting existing pending order for" << screenName;
    }
    m_pendingInitialOrders[screenName] = windowIds;
    qCInfo(lcAutotile) << "Pre-seeded window order for screen" << screenName << ":" << windowIds;

    // Safety timeout: clean up if windows never arrive (e.g., app crash during startup)
    QTimer::singleShot(PendingOrderTimeoutMs, this, [this, screenName]() {
        if (m_pendingInitialOrders.remove(screenName)) {
            qCWarning(lcAutotile) << "Pending initial order for screen" << screenName << "timed out after"
                                  << PendingOrderTimeoutMs << "ms — cleaning up stale entry";
        }
    });
}

void AutotileEngine::clearSavedFloatingForWindows(const QStringList& windowIds)
{
    for (const QString& id : windowIds) {
        if (m_savedFloatingWindows.remove(id)) {
            qCDebug(lcAutotile) << "Cleared stale saved-floating state for zone-snapped window" << id;
        }
    }
}

void AutotileEngine::clearAllSavedFloating()
{
    if (!m_savedFloatingWindows.isEmpty()) {
        qCInfo(lcAutotile) << "Clearing all saved floating state (" << m_savedFloatingWindows.size() << "windows)";
        m_savedFloatingWindows.clear();
    }
}

QStringList AutotileEngine::tiledWindowOrder(const QString& screenName) const
{
    TilingState* state = m_screenStates.value(screenName);
    if (!state) {
        return {};
    }
    return state->tiledWindows();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Settings synchronization
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::syncFromSettings(Settings* settings)
{
    m_settingsBridge->syncFromSettings(settings);
}

void AutotileEngine::connectToSettings(Settings* settings)
{
    m_settingsBridge->connectToSettings(settings);
}

void AutotileEngine::applyPerScreenConfig(const QString& screenName, const QVariantMap& overrides)
{
    m_configResolver->applyPerScreenConfig(screenName, overrides);
}

void AutotileEngine::clearPerScreenConfig(const QString& screenName)
{
    m_configResolver->clearPerScreenConfig(screenName);
}

QVariantMap AutotileEngine::perScreenOverrides(const QString& screenName) const
{
    return m_configResolver->perScreenOverrides(screenName);
}

bool AutotileEngine::hasPerScreenOverride(const QString& screenName, const QString& key) const
{
    return m_configResolver->hasPerScreenOverride(screenName, key);
}

int AutotileEngine::effectiveInnerGap(const QString& screenName) const
{
    return m_configResolver->effectiveInnerGap(screenName);
}

int AutotileEngine::effectiveOuterGap(const QString& screenName) const
{
    return m_configResolver->effectiveOuterGap(screenName);
}

EdgeGaps AutotileEngine::effectiveOuterGaps(const QString& screenName) const
{
    return m_configResolver->effectiveOuterGaps(screenName);
}

bool AutotileEngine::effectiveSmartGaps(const QString& screenName) const
{
    return m_configResolver->effectiveSmartGaps(screenName);
}

bool AutotileEngine::effectiveRespectMinimumSize(const QString& screenName) const
{
    return m_configResolver->effectiveRespectMinimumSize(screenName);
}

int AutotileEngine::effectiveMaxWindows(const QString& screenName) const
{
    return m_configResolver->effectiveMaxWindows(screenName);
}

QString AutotileEngine::effectiveAlgorithmId(const QString& screenName) const
{
    return m_configResolver->effectiveAlgorithmId(screenName);
}

TilingAlgorithm* AutotileEngine::effectiveAlgorithm(const QString& screenName) const
{
    return m_configResolver->effectiveAlgorithm(screenName);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Session Persistence
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::saveState()
{
    m_settingsBridge->saveState();
}

void AutotileEngine::loadState()
{
    m_settingsBridge->loadState();
}

void AutotileEngine::scheduleRetileForScreen(const QString& screenName)
{
    m_pendingRetileScreens.insert(screenName);

    if (!m_retilePending) {
        m_retilePending = true;
        // Qt::QueuedConnection (same-thread deferral, not cross-thread — see Qt docs
        // on QMetaObject::invokeMethod) fires after all currently-pending events
        // (including D-Bus messages from the same socket read) are processed.
        // This naturally coalesces bursts: on first activation, the KWin effect
        // sends N windowOpened D-Bus calls in rapid succession — they all arrive
        // in one socket read and are dispatched before this queued call fires.
        // Single-window opens retile on the very next event loop iteration (~0ms).
        QMetaObject::invokeMethod(this, &AutotileEngine::processPendingRetiles, Qt::QueuedConnection);
    }
}

void AutotileEngine::processPendingRetiles()
{
    m_retilePending = false;

    if (m_pendingRetileScreens.isEmpty()) {
        return;
    }

    const QSet<QString> screens = m_pendingRetileScreens;
    m_pendingRetileScreens.clear();

    for (const QString& screenName : screens) {
        if (isAutotileScreen(screenName) && m_screenStates.contains(screenName)) {
            retileAfterOperation(screenName, true);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Manual tiling operations
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::retile(const QString& screenName)
{
    // R3/R4: m_retiling serves as a re-entrancy guard for both retile() and
    // retileAfterOperation(). Both methods set it with QScopeGuard and check it
    // on entry. They are mutually exclusive: retileAfterOperation() returns early
    // if m_retiling is already true (set by retile()), so the dual QScopeGuard
    // pattern cannot leave the flag inconsistent.
    if (m_retiling) {
        return;
    }
    QScopeGuard guard([this] {
        m_retiling = false;
    });
    m_retiling = true;

    if (screenName.isEmpty()) {
        // Retile autotile screens only
        for (const QString& key : m_autotileScreens) {
            if (m_screenStates.contains(key)) {
                retileScreen(key);
            }
        }
    } else {
        if (!isAutotileScreen(screenName)) {
            return;
        }
        retileScreen(screenName);
    }
}

void AutotileEngine::swapWindows(const QString& windowId1, const QString& windowId2)
{
    // Early return if same window (no-op)
    if (windowId1 == windowId2) {
        return;
    }

    // Find screens for both windows
    const QString screen1 = m_windowToScreen.value(windowId1);
    const QString screen2 = m_windowToScreen.value(windowId2);

    if (screen1.isEmpty() || screen2.isEmpty()) {
        qCWarning(lcAutotile) << "AutotileEngine::swapWindows: window not found";
        return;
    }

    if (screen1 != screen2) {
        qCWarning(lcAutotile) << "AutotileEngine::swapWindows: windows on different screens";
        return;
    }

    TilingState* state = stateForScreen(screen1);
    if (!state) {
        return;
    }

    const bool swapped = state->swapWindowsById(windowId1, windowId2);
    retileAfterOperation(screen1, swapped);
}

void AutotileEngine::promoteToMaster(const QString& windowId)
{
    QString screenName;
    TilingState* state = stateForWindow(windowId, &screenName);
    if (!state) {
        return;
    }

    const bool promoted = state->moveToTiledPosition(windowId, 0);
    retileAfterOperation(screenName, promoted);
}

void AutotileEngine::demoteFromMaster(const QString& windowId)
{
    QString screenName;
    TilingState* state = stateForWindow(windowId, &screenName);
    if (!state) {
        return;
    }

    // Move to position after master area (only if currently in master area)
    const int masterCount = state->masterCount();
    const int currentPos = state->tiledWindowIndex(windowId);

    bool demoted = false;
    if (currentPos >= 0 && currentPos < masterCount) {
        demoted = state->moveToTiledPosition(windowId, masterCount);
    }

    retileAfterOperation(screenName, demoted);
}

void AutotileEngine::swapFocusedWithMaster()
{
    m_navigation->swapFocusedWithMaster();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Focus/window cycling
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::focusNext()
{
    m_navigation->focusNext();
}

void AutotileEngine::focusPrevious()
{
    m_navigation->focusPrevious();
}

void AutotileEngine::focusMaster()
{
    m_navigation->focusMaster();
}

void AutotileEngine::setFocusedWindow(const QString& windowId)
{
    onWindowFocused(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Split ratio adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::increaseMasterRatio(qreal delta)
{
    m_navigation->increaseMasterRatio(delta);
}

void AutotileEngine::decreaseMasterRatio(qreal delta)
{
    m_navigation->decreaseMasterRatio(delta);
}

void AutotileEngine::setGlobalSplitRatio(qreal ratio)
{
    m_navigation->setGlobalSplitRatio(ratio);
}

void AutotileEngine::setGlobalMasterCount(int count)
{
    m_navigation->setGlobalMasterCount(count);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Master count adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::increaseMasterCount()
{
    m_navigation->increaseMasterCount();
}

void AutotileEngine::decreaseMasterCount()
{
    m_navigation->decreaseMasterCount();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window rotation and floating
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::rotateWindowOrder(bool clockwise)
{
    m_navigation->rotateWindowOrder(clockwise);
}

void AutotileEngine::swapFocusedInDirection(const QString& direction, const QString& action)
{
    m_navigation->swapFocusedInDirection(direction, action);
}

void AutotileEngine::focusInDirection(const QString& direction, const QString& action)
{
    m_navigation->focusInDirection(direction, action);
}

void AutotileEngine::moveFocusedToPosition(int position)
{
    m_navigation->moveFocusedToPosition(position);
}

void AutotileEngine::toggleFocusedWindowFloat()
{
    // Resolve the focused screen — same logic as NavigationController::tiledWindowsForFocusedScreen
    // but we only need the screen name and state (not the tiled windows list).
    QString screenName;
    TilingState* state = nullptr;

    if (!m_activeScreen.isEmpty() && m_screenStates.contains(m_activeScreen)) {
        TilingState* s = m_screenStates.value(m_activeScreen);
        if (s && !s->focusedWindow().isEmpty()) {
            screenName = m_activeScreen;
            state = s;
        }
    }
    if (!state) {
        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
            if (it.value() && !it.value()->focusedWindow().isEmpty()) {
                screenName = it.key();
                state = it.value();
                break;
            }
        }
    }

    if (!state) {
        qCWarning(lcAutotile) << "toggleFocusedWindowFloat: no state found for focused screen"
                              << "(m_activeScreen=" << m_activeScreen << ")";
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"), QStringLiteral("no_focused_screen"),
                                           QString(), QString(), m_activeScreen);
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        qCWarning(lcAutotile) << "toggleFocusedWindowFloat: no focused window on screen" << screenName;
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"), QStringLiteral("no_focused_window"),
                                           QString(), QString(), screenName);
        return;
    }

    performToggleFloat(state, focused, screenName);
}

void AutotileEngine::toggleWindowFloat(const QString& windowId, const QString& screenName)
{
    if (!warnIfEmptyWindowId(windowId, "toggleWindowFloat")) {
        return;
    }

    if (screenName.isEmpty()) {
        qCWarning(lcAutotile) << "toggleWindowFloat: empty screenName for window" << windowId;
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"), QStringLiteral("no_screen"), QString(),
                                           QString(), QString());
        return;
    }

    // Try the given screen first
    QString resolvedScreen = screenName;
    TilingState* state = nullptr;

    if (isAutotileScreen(screenName)) {
        state = stateForScreen(screenName);
        if (state && !state->containsWindow(windowId)) {
            state = nullptr; // Window not on this screen
        }
    }

    // Cross-screen fallback: the window may have been moved (e.g., pre-autotile
    // geometry restore put it on a different screen). Search all autotile states.
    if (!state) {
        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
            if (it.value() && it.value()->containsWindow(windowId)) {
                state = it.value();
                resolvedScreen = it.key();
                qCInfo(lcAutotile) << "toggleWindowFloat: window" << windowId << "found on screen" << resolvedScreen
                                   << "(caller reported" << screenName << ")";
                break;
            }
        }
    }

    if (!state) {
        qCWarning(lcAutotile) << "toggleWindowFloat: window" << windowId << "not found in any autotile state";
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"), QStringLiteral("window_not_tracked"),
                                           QString(), QString(), screenName);
        return;
    }

    performToggleFloat(state, windowId, resolvedScreen);
}

void AutotileEngine::performToggleFloat(TilingState* state, const QString& windowId, const QString& screenName)
{
    state->toggleFloating(windowId);
    m_overflow.clearOverflow(windowId); // User explicitly toggled, no longer overflow
    retileAfterOperation(screenName, true);

    const bool isNowFloating = state->isFloating(windowId);
    qCInfo(lcAutotile) << "Window" << windowId << (isNowFloating ? "now floating" : "now tiled") << "on screen"
                       << screenName;
    Q_EMIT windowFloatingChanged(windowId, isNowFloating, screenName);
}

void AutotileEngine::setWindowFloat(const QString& windowId, bool shouldFloat)
{
    if (!warnIfEmptyWindowId(windowId, shouldFloat ? "floatWindow" : "unfloatWindow")) {
        return;
    }

    // floatWindow checks autotile screen membership; unfloatWindow does not
    // (window might be on a screen that was removed from autotile after it was floated)
    if (shouldFloat && !isAutotileScreen(m_windowToScreen.value(windowId))) {
        return;
    }

    TilingState* state = stateForWindow(windowId);
    if (!state) {
        qCDebug(lcAutotile) << (shouldFloat ? "floatWindow" : "unfloatWindow") << ": window not tracked:" << windowId;
        return;
    }

    if (state->isFloating(windowId) == shouldFloat) {
        qCDebug(lcAutotile) << (shouldFloat ? "floatWindow: already floating" : "unfloatWindow: not floating") << ":"
                            << windowId;
        return;
    }

    state->setFloating(windowId, shouldFloat);
    m_overflow.clearOverflow(windowId);
    const QString screenName = m_windowToScreen.value(windowId);
    retileAfterOperation(screenName, true);

    qCInfo(lcAutotile) << "Window" << (shouldFloat ? "floated from" : "unfloated to") << "autotile:" << windowId;
    Q_EMIT windowFloatingChanged(windowId, shouldFloat, screenName);
}

void AutotileEngine::floatWindow(const QString& windowId)
{
    setWindowFloat(windowId, true);
}

void AutotileEngine::unfloatWindow(const QString& windowId)
{
    setWindowFloat(windowId, false);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public window event handlers (called by Daemon via D-Bus signals)
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::windowOpened(const QString& windowId, const QString& screenName, int minWidth, int minHeight)
{
    if (!warnIfEmptyWindowId(windowId, "windowOpened")) {
        return;
    }

    // Store window minimum size from KWin (used by enforceWindowMinSizes)
    if (minWidth > 0 || minHeight > 0) {
        m_windowMinSizes[windowId] = QSize(qMax(0, minWidth), qMax(0, minHeight));
        qCDebug(lcAutotile) << "Stored min size for" << windowId << ":" << minWidth << "x" << minHeight;
    }

    // Store screen mapping so onWindowAdded uses correct screen
    if (!screenName.isEmpty()) {
        m_windowToScreen[windowId] = screenName;
    }
    onWindowAdded(windowId);
}

void AutotileEngine::windowMinSizeUpdated(const QString& windowId, int minWidth, int minHeight)
{
    if (!warnIfEmptyWindowId(windowId, "windowMinSizeUpdated")) {
        return;
    }

    const QSize newMin(qMax(0, minWidth), qMax(0, minHeight));
    const QSize oldMin = m_windowMinSizes.value(windowId, QSize(0, 0));

    if (newMin == oldMin) {
        return; // No change
    }

    if (newMin.width() > 0 || newMin.height() > 0) {
        m_windowMinSizes[windowId] = newMin;
    } else {
        m_windowMinSizes.remove(windowId);
    }

    qCDebug(lcAutotile) << "Updated min size for" << windowId << ":" << oldMin << "->" << newMin;

    // Retile the screen this window is on
    const QString screenName = m_windowToScreen.value(windowId);
    if (!screenName.isEmpty() && m_screenStates.contains(screenName)) {
        scheduleRetileForScreen(screenName);
    }
}

void AutotileEngine::windowClosed(const QString& windowId)
{
    if (!warnIfEmptyWindowId(windowId, "windowClosed")) {
        return;
    }

    // Clean up saved floating state even if window isn't currently tracked
    // (it may have been floating when autotile was disabled on its screen)
    m_savedFloatingWindows.remove(windowId);

    onWindowRemoved(windowId);
}

void AutotileEngine::windowFocused(const QString& windowId, const QString& screenName)
{
    if (!warnIfEmptyWindowId(windowId, "windowFocused")) {
        return;
    }

    // Detect cross-screen moves. When a window's focus moves to a different
    // screen, migrate its TilingState membership so m_windowToScreen and the
    // TilingState remain consistent. This handles both overflow-floated windows
    // and windows that were previously migrated (preventing the Screen1->2->1
    // rapid-migration desync where the second hop was silently skipped).
    //
    // Only update m_windowToScreen for windows already tracked via windowOpened().
    // The KWin effect sends focus events for ALL handleable windows (including
    // transients and non-tileable windows that pass shouldHandleWindow but fail
    // isTileableWindow). Creating entries for these phantom windows causes
    // backfillWindows() to insert them on algorithm switches, inflating the
    // tiled window count.
    const QString oldScreen = m_windowToScreen.value(windowId);
    if (!screenName.isEmpty() && m_windowToScreen.contains(windowId)) {
        m_windowToScreen[windowId] = screenName;
    }

    if (!oldScreen.isEmpty() && !screenName.isEmpty() && oldScreen != screenName) {
        TilingState* oldState = m_screenStates.value(oldScreen);
        if (oldState && oldState->containsWindow(windowId)) {
            oldState->removeWindow(windowId);
            m_overflow.migrateWindow(windowId);
            qCInfo(lcAutotile) << "Window" << windowId << "moved from" << oldScreen << "to" << screenName
                               << "- migrating";
            // Re-add to the new screen's normal flow (will be overflow-checked on next retile)
            onWindowAdded(windowId);
        }
    }

    onWindowFocused(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private slot event handlers
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::onWindowAdded(const QString& windowId)
{
    const QString screenName = screenForWindow(windowId);
    if (!isAutotileScreen(screenName) || !shouldTileWindow(windowId)) {
        return;
    }

    TilingState* state = stateForScreen(screenName);
    const int maxWin = effectiveMaxWindows(screenName);
    if (state && state->tiledWindowCount() >= maxWin) {
        qCDebug(lcAutotile) << "Max window limit reached for screen" << screenName << "(max=" << maxWin << ")";
        // Purge this window from pending initial orders so the order doesn't
        // leak waiting for a window that will never be inserted.
        for (auto pit = m_pendingInitialOrders.begin(); pit != m_pendingInitialOrders.end(); ++pit) {
            pit.value().removeAll(windowId);
        }
        return;
    }

    const bool inserted = insertWindow(windowId, screenName);

    // Notify listeners if the window was restored as floating (e.g., after mode toggle)
    if (inserted && state && state->isFloating(windowId)) {
        Q_EMIT windowFloatingChanged(windowId, true, screenName);
    }

    if (inserted && m_config && m_config->focusNewWindows) {
        // Defer focus until after applyTiling emits windowsTiled. The KWin effect's
        // onComplete raises windows in tiling order; emitting focus before retile
        // causes the raise loop to bury the new window behind existing ones.
        m_pendingFocusWindowId = windowId;
    }

    if (inserted) {
        scheduleRetileForScreen(screenName);
    }
}

void AutotileEngine::onWindowRemoved(const QString& windowId)
{
    const QString screenName = m_windowToScreen.value(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    removeWindow(windowId);
    // Retile immediately (not deferred like onWindowAdded). Removals need instant
    // layout recalculation to avoid visible holes. Unlike additions, removals don't
    // arrive in bursts, so coalescing provides no benefit.
    retileAfterOperation(screenName, true);
}

void AutotileEngine::onWindowFocused(const QString& windowId)
{
    TilingState* state = stateForWindow(windowId);
    if (!state) {
        // Not an error — non-autotiled windows (dialogs, floating, etc.) report
        // focus changes too, so this is the normal case for most window activations
        qCDebug(lcAutotile) << "onWindowFocused: window not tracked" << windowId;
        return;
    }

    // Track which screen has the active focus (used by tiledWindowsForFocusedScreen
    // to avoid non-deterministic QHash iteration when multiple screens have focused windows)
    m_activeScreen = m_windowToScreen.value(windowId);

    state->setFocusedWindow(windowId);
}

void AutotileEngine::onScreenGeometryChanged(const QString& screenName)
{
    if (!isAutotileScreen(screenName) || !m_screenStates.contains(screenName)) {
        return;
    }

    retileAfterOperation(screenName, true);
}

void AutotileEngine::onLayoutChanged(Layout* layout)
{
    Q_UNUSED(layout)
    // Autotile screens are managed by per-screen assignments, not the global
    // active layout. Retile is triggered by setAutotileScreens() and
    // onScreenGeometryChanged() instead.
}

// ═══════════════════════════════════════════════════════════════════════════════
// Internal implementation
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileEngine::insertWindow(const QString& windowId, const QString& screenName)
{
    TilingState* state = stateForScreen(screenName);
    if (!state) {
        qCWarning(lcAutotile) << "AutotileEngine::insertWindow: failed to get state for screen" << screenName;
        return false;
    }

    // Check if window already tracked in this screen's tiling state
    // Note: We check the TilingState (not m_windowToScreen) because windowOpened()
    // stores the screen mapping in m_windowToScreen *before* calling onWindowAdded(),
    // so m_windowToScreen.contains() would always be true via that path.
    if (state->containsWindow(windowId)) {
        return false;
    }

    // Check if this window has a pre-seeded position from zone-ordered transition.
    // Take a value copy of the pending list — the erase below invalidates iterators/refs.
    bool inserted = false;
    auto pendingIt = m_pendingInitialOrders.find(screenName);
    if (pendingIt != m_pendingInitialOrders.end()) {
        const QStringList pendingOrder = pendingIt.value(); // copy, not reference (BUG-1 fix)
        int desiredPos = pendingOrder.indexOf(windowId);
        if (desiredPos >= 0) {
            // Count ALL pre-seeded windows (including floating) with lower desired position
            // already in state. addWindow() inserts into m_windowOrder which includes both
            // tiled and floating windows, so the offset must account for all of them.
            int insertAt = 0;
            for (int i = 0; i < desiredPos; ++i) {
                const QString& earlier = pendingOrder.at(i);
                if (state->containsWindow(earlier)) {
                    ++insertAt;
                }
            }
            state->addWindow(windowId, insertAt);
            inserted = true;
            qCDebug(lcAutotile) << "Inserted pre-seeded window" << windowId << "at position" << insertAt
                                << "(desired:" << desiredPos << ")";
        }
        // Clean up pending order when all pre-seeded windows have been inserted (or closed)
        if (inserted) {
            cleanupPendingOrderIfResolved(screenName);
        }
    }

    if (!inserted) {
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
    }

    // Restore floating state from engine's saved set (populated when autotile was
    // previously deactivated) OR from WTS (the canonical floating source shared
    // between autotile and snapping modes). This ensures user-floated windows
    // stay floating across autotile ↔ snapping mode toggles.
    if (m_savedFloatingWindows.remove(windowId)) {
        state->setFloating(windowId, true);
        qCInfo(lcAutotile) << "Restored saved floating state for window" << windowId << "on screen" << screenName;
    } else if (m_windowTracker && m_windowTracker->isWindowFloating(windowId)) {
        state->setFloating(windowId, true);
        qCInfo(lcAutotile) << "Preserved WTS floating state for window" << windowId << "on screen" << screenName;
    }

    m_windowToScreen.insert(windowId, screenName);
    return true;
}

void AutotileEngine::removeWindow(const QString& windowId)
{
    m_windowMinSizes.remove(windowId);
    m_overflow.clearOverflow(windowId);
    const QString screenName = m_windowToScreen.take(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    TilingState* state = m_screenStates.value(screenName);
    if (state) {
        state->removeWindow(windowId);
    }

    // Clean up saved floating state for closed windows
    m_savedFloatingWindows.remove(windowId);

    // Purge closed window from pending initial orders.
    // If a pre-seeded window closes before arriving at the autotile engine,
    // the pending order would leak indefinitely without this cleanup.
    for (auto pit = m_pendingInitialOrders.begin(); pit != m_pendingInitialOrders.end();) {
        pit.value().removeAll(windowId);
        if (pit.value().isEmpty()) {
            pit = m_pendingInitialOrders.erase(pit);
        } else {
            const QString screen = pit.key();
            ++pit; // advance before potential erase by helper
            cleanupPendingOrderIfResolved(screen);
        }
    }
}

void AutotileEngine::recalculateLayout(const QString& screenName)
{
    if (screenName.isEmpty()) {
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: empty screen name";
        return;
    }

    TilingState* state = stateForScreen(screenName);
    if (!state) {
        return;
    }

    TilingAlgorithm* algo = effectiveAlgorithm(screenName);
    if (!algo) {
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: no algorithm set";
        return;
    }

    const int tiledCount = state->tiledWindowCount();
    if (tiledCount == 0) {
        state->setCalculatedZones({}); // Clear zones when no windows
        return;
    }

    // Cap to user's max windows setting — excess windows are not tiled
    const int windowCount = std::min(tiledCount, effectiveMaxWindows(screenName));

    const QRect screen = screenGeometry(screenName);
    if (!screen.isValid()) {
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: invalid screen geometry";
        return;
    }

    const QString algoId = effectiveAlgorithmId(screenName);

    qCDebug(lcAutotile) << "recalculateLayout: screen=" << screenName << "geometry=" << screen
                        << "windows=" << windowCount << "algo=" << algoId;

    // Calculate zone geometries using the algorithm, with gap-aware zones.
    // Algorithms apply gaps directly using their topology knowledge, eliminating
    // the fragile post-processing step that previously guessed adjacency.
    const bool skipGaps = effectiveSmartGaps(screenName) && windowCount == 1;
    const int innerGap = skipGaps ? 0 : effectiveInnerGap(screenName);
    EdgeGaps outerGaps = skipGaps ? EdgeGaps::uniform(0) : effectiveOuterGaps(screenName);

    // Build minSizes vector for the algorithm (when respectMinimumSize is enabled)
    // Only include the first windowCount windows (capped by maxWindows above)
    QVector<QSize> minSizes;
    if (effectiveRespectMinimumSize(screenName)) {
        const QStringList windows = state->tiledWindows();
        // KWin reports min size in logical pixels (same as QScreen/zone geometry);
        // do not divide by devicePixelRatio or we under-report and steal too little.
        minSizes.resize(windowCount, QSize(0, 0));
        for (int i = 0; i < windowCount && i < windows.size(); ++i) {
            minSizes[i] = m_windowMinSizes.value(windows[i], QSize(0, 0));
        }
    }

    // Pass minSizes to algorithm so it can incorporate them directly into zone
    // calculations using its topology knowledge (split tree, column structure, etc.)
    QVector<QRect> zones = algo->calculateZones({windowCount, screen, state, innerGap, outerGaps, minSizes});

    // Validate algorithm returned correct number of zones
    if (zones.size() != windowCount) {
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: algorithm returned" << zones.size() << "zones for"
                              << windowCount << "windows";
        return;
    }

    // Lightweight safety net: the algorithm handles min sizes directly, but
    // enforceWindowMinSizes catches any residual deficits from rounding or
    // edge cases the algorithm couldn't fully solve (e.g., unsatisfiable constraints).
    // Skip for Monocle: zones intentionally overlap (stacked windows), and
    // removeZoneOverlaps would separate them into side-by-side columns.
    if (effectiveRespectMinimumSize(screenName) && !minSizes.isEmpty() && algoId != DBus::AutotileAlgorithm::Monocle) {
        const int threshold = effectiveInnerGap(screenName) + qMax(AutotileDefaults::GapEdgeThresholdPx, 12);
        GeometryUtils::enforceWindowMinSizes(zones, minSizes, threshold, innerGap);
    }

    // Store calculated zones in the state for later application
    state->setCalculatedZones(zones);
}

void AutotileEngine::applyTiling(const QString& screenName)
{
    TilingState* state = stateForScreen(screenName);
    if (!state) {
        return;
    }

    const QStringList windows = state->tiledWindows();
    const QVector<QRect> zones = state->calculatedZones();

    // zones.size() may be less than windows.size() when maxWindows caps the layout.
    // Only the first zones.size() windows receive tiled geometries; the rest are untouched.
    if (zones.isEmpty()) {
        qCDebug(lcAutotile) << "AutotileEngine::applyTiling: no zones calculated for screen" << screenName;
        return;
    }
    if (zones.size() > windows.size()) {
        qCWarning(lcAutotile) << "AutotileEngine::applyTiling: zone count exceeds window count" << windows.size()
                              << "vs" << zones.size();
        return;
    }

    const int tileCount = zones.size();

    // Auto-float overflow windows that exceed maxWindows cap.
    // Daemon's windowFloatingChanged handler restores their pre-autotile geometry.
    // Batch: mutate state first, then collect signals for deferred emission.
    QStringList newlyOverflowed = m_overflow.applyOverflow(screenName, windows, tileCount);
    for (const QString& wid : std::as_const(newlyOverflowed)) {
        state->setFloating(wid, true);
    }

    // Build batch JSON and emit once to avoid race when effect applies many geometries.
    // Monocle tiles all windows to the same geometry (stacked); KWin's stacking
    // order handles visibility — no minimize/unminimize needed.
    const bool isMonocle = (effectiveAlgorithmId(screenName) == DBus::AutotileAlgorithm::Monocle);
    QJsonArray arr;
    for (int i = 0; i < tileCount; ++i) {
        const QRect& geo = zones[i];
        QJsonObject obj;
        obj[QLatin1String("windowId")] = windows[i];
        obj[QLatin1String("x")] = geo.x();
        obj[QLatin1String("y")] = geo.y();
        obj[QLatin1String("width")] = geo.width();
        obj[QLatin1String("height")] = geo.height();
        // Flag monocle entries so the effect can set KWin maximize state,
        // which makes Plasma panels recognize the window and unfloat.
        if (isMonocle) {
            obj[QLatin1String("monocle")] = true;
        }
        arr.append(obj);
    }
    Q_EMIT windowsTiled(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));

    // Emit deferred focus AFTER windowsTiled so KWin processes tiles first
    // (including the onComplete raise loop), then focuses the new window on top.
    if (!m_pendingFocusWindowId.isEmpty()) {
        Q_EMIT focusWindowRequested(m_pendingFocusWindowId);
        m_pendingFocusWindowId.clear();
    }

    // Emit overflow signals AFTER geometry batch — prevents re-entrant signal
    // handlers from triggering retile on partially-complete state.
    for (const QString& wid : std::as_const(newlyOverflowed)) {
        Q_EMIT windowFloatingChanged(wid, true, screenName);
    }
}

bool AutotileEngine::shouldTileWindow(const QString& windowId) const
{
    if (windowId.isEmpty()) {
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

QString AutotileEngine::screenForWindow(const QString& windowId) const
{
    // Check if already tracked
    if (m_windowToScreen.contains(windowId)) {
        return m_windowToScreen.value(windowId);
    }

    // R6 fix: Warn when falling back to primary screen — this may indicate a
    // missing screen name in windowOpened() or a stale m_windowToScreen entry.
    if (m_screenManager && m_screenManager->primaryScreen()) {
        qCWarning(lcAutotile) << "screenForWindow: window" << windowId
                              << "not in m_windowToScreen, falling back to primary screen";
        return m_screenManager->primaryScreen()->name();
    }

    qCWarning(lcAutotile) << "screenForWindow: no screen found for window" << windowId;
    return QString();
}

QRect AutotileEngine::screenGeometry(const QString& screenName) const
{
    if (!m_screenManager) {
        return QRect();
    }

    QScreen* screen = m_screenManager->screenByName(screenName);
    if (!screen) {
        return QRect();
    }

    return ScreenManager::actualAvailableGeometry(screen);
}

void AutotileEngine::resetMaxWindowsForAlgorithmSwitch(TilingAlgorithm* oldAlgo, TilingAlgorithm* newAlgo)
{
    if (!oldAlgo || !newAlgo)
        return;
    if (m_config->maxWindows == oldAlgo->defaultMaxWindows()) {
        m_config->maxWindows = newAlgo->defaultMaxWindows();
    }
}

void AutotileEngine::propagateGlobalSplitRatio()
{
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.value() && !hasPerScreenOverride(it.key(), QLatin1String("SplitRatio"))) {
            it.value()->setSplitRatio(m_config->splitRatio);
        }
    }
}

void AutotileEngine::propagateGlobalMasterCount()
{
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.value() && !hasPerScreenOverride(it.key(), QLatin1String("MasterCount"))) {
            it.value()->setMasterCount(m_config->masterCount);
        }
    }
}

void AutotileEngine::backfillWindows()
{
    for (const QString& screenName : m_autotileScreens) {
        // Prioritize recovering overflow-floated windows before inserting new ones.
        // This ensures previously-tiled windows return to tiling before brand-new
        // windows take their slots.
        {
            TilingState* bfState = stateForScreen(screenName);
            if (bfState && !m_overflow.isEmpty()) {
                QStringList unfloated = m_overflow.recoverIfRoom(
                    screenName, bfState->tiledWindowCount(), effectiveMaxWindows(screenName),
                    [bfState](const QString& wid) {
                        return bfState->isFloating(wid);
                    },
                    [bfState](const QString& wid) {
                        return bfState->containsWindow(wid);
                    });
                for (const QString& wid : unfloated) {
                    bfState->setFloating(wid, false);
                    Q_EMIT windowFloatingChanged(wid, false, screenName);
                }
            }
        }

        TilingState* state = stateForScreen(screenName);
        if (!state) {
            continue;
        }
        const int maxWin = effectiveMaxWindows(screenName);
        if (state->tiledWindowCount() >= maxWin) {
            continue;
        }
        // Collect candidates to avoid modifying m_windowToScreen during iteration
        // (insertWindow calls m_windowToScreen.insert which is unsafe during const iteration)
        QStringList candidates;
        for (auto it = m_windowToScreen.constBegin(); it != m_windowToScreen.constEnd(); ++it) {
            if (it.value() == screenName && !state->containsWindow(it.key()) && shouldTileWindow(it.key())) {
                candidates.append(it.key());
            }
        }
        for (const QString& windowId : candidates) {
            insertWindow(windowId, screenName);
            if (state->tiledWindowCount() >= maxWin) {
                break;
            }
        }
    }
}

void AutotileEngine::retileScreen(const QString& screenName)
{
    TilingState* state = stateForScreen(screenName);
    if (!state) {
        return;
    }

    // Step 1: Recover overflow windows when room is available.
    // Collect recovery list first, mutate state, then defer signal emission
    // until after the entire retile cycle completes (prevents re-entrant
    // signal handlers from seeing partially-modified state).
    QStringList unfloated;
    if (!m_overflow.isEmpty()) {
        unfloated = m_overflow.recoverIfRoom(
            screenName, state->tiledWindowCount(), effectiveMaxWindows(screenName),
            [state](const QString& wid) {
                return state->isFloating(wid);
            },
            [state](const QString& wid) {
                return state->containsWindow(wid);
            });
        for (const QString& wid : unfloated) {
            state->setFloating(wid, false);
        }
    }

    // Step 2-3: Recalculate layout and apply tiling (applyTiling also handles
    // new overflow detection and collects overflow signals internally).
    recalculateLayout(screenName);
    applyTiling(screenName);

    // Step 4: Emit all deferred signals after state is fully consistent.
    // Recovery signals first (unfloated windows), then overflow signals
    // (newly floated windows) were already handled inside applyTiling's
    // batch emit, and tilingChanged is emitted last.
    for (const QString& wid : unfloated) {
        Q_EMIT windowFloatingChanged(wid, false, screenName);
    }
    Q_EMIT tilingChanged(screenName);
}

void AutotileEngine::retileAfterOperation(const QString& screenName, bool operationSucceeded)
{
    if (!operationSucceeded) {
        return; // No change, no signal
    }

    if (!isAutotileScreen(screenName)) {
        return;
    }

    // When already inside retile(), still recalc and apply for this screen so
    // navigation (rotate, swap, etc.) is never dropped — user expects geometry
    // to update immediately. Do not clear m_retiling; let the outer retile() do that.
    if (m_retiling) {
        retileScreen(screenName);
        return;
    }

    QScopeGuard guard([this] {
        m_retiling = false;
    });
    m_retiling = true;
    retileScreen(screenName);
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

bool AutotileEngine::cleanupPendingOrderIfResolved(const QString& screenName)
{
    auto pit = m_pendingInitialOrders.find(screenName);
    if (pit == m_pendingInitialOrders.end()) {
        return false;
    }

    TilingState* state = m_screenStates.value(screenName);
    if (!state) {
        return false;
    }

    for (const QString& pendingWin : std::as_const(pit.value())) {
        if (!state->containsWindow(pendingWin)) {
            return false;
        }
    }

    qCDebug(lcAutotile) << "All pre-seeded windows resolved for screen" << screenName;
    m_pendingInitialOrders.erase(pit);
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

void AutotileEngine::setInnerGap(int gap)
{
    gap = std::clamp(gap, AutotileDefaults::MinGap, AutotileDefaults::MaxGap);
    if (m_config && m_config->innerGap != gap) {
        m_config->innerGap = gap;
        retile(QString());
    }
}

void AutotileEngine::setOuterGap(int gap)
{
    gap = std::clamp(gap, AutotileDefaults::MinGap, AutotileDefaults::MaxGap);
    if (m_config && m_config->outerGap != gap) {
        m_config->outerGap = gap;
        retile(QString());
    }
}

void AutotileEngine::setSmartGaps(bool enabled)
{
    if (m_config && m_config->smartGaps != enabled) {
        m_config->smartGaps = enabled;
        retile(QString());
    }
}

void AutotileEngine::setFocusNewWindows(bool enabled)
{
    if (m_config) {
        m_config->focusNewWindows = enabled;
    }
}

} // namespace PlasmaZones
