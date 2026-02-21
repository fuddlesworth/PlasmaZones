// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Qt headers
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QScreen>

// KDE headers
#include <KSharedConfig>
#include <KConfigGroup>

// Project headers
#include "AutotileEngine.h"
#include "AlgorithmRegistry.h"
#include "core/geometryutils.h"
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

AutotileEngine::AutotileEngine(LayoutManager* layoutManager, WindowTrackingService* windowTracker,
                               ScreenManager* screenManager, QObject* parent)
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
        connect(m_windowTracker, &WindowTrackingService::windowZoneChanged, this,
                [this](const QString& windowId, const QString& zoneId) {
                    if (m_retiling)
                        return; // Ignore zone changes during retile
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
        retileAfterOperation(screenName, true);
    }

    // Collect windows from removed screens before pruning, then prune
    QStringList releasedWindows;
    QMutableHashIterator<QString, TilingState*> it(m_screenStates);
    while (it.hasNext()) {
        it.next();
        if (!m_autotileScreens.contains(it.key())) {
            releasedWindows.append(it.value()->tiledWindows());
            it.value()->deleteLater();
            it.remove();
        }
    }
    if (!releasedWindows.isEmpty()) {
        Q_EMIT windowsReleasedFromTiling(releasedWindows);
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

    // When switching algorithms, reset split ratio to new algorithm's default
    // if the current ratio is still at the old algorithm's default.
    // This ensures BSP starts at 0.5 (balanced) and MasterStack at 0.6 (60/40).
    // If the user has customized the ratio, it's preserved across switches.
    TilingAlgorithm* oldAlgo = registry->algorithm(m_algorithmId);
    TilingAlgorithm* newAlgo = registry->algorithm(newId);
    if (oldAlgo && newAlgo) {
        const qreal oldDefault = oldAlgo->defaultSplitRatio();
        if (qFuzzyCompare(1.0 + m_config->splitRatio, 1.0 + oldDefault)) {
            const qreal newDefault = newAlgo->defaultSplitRatio();
            m_config->splitRatio = newDefault;
            applyToAllStates([newDefault](TilingState* state) {
                state->setSplitRatio(newDefault);
            });
        }
    }

    m_algorithmId = newId;
    Q_EMIT algorithmChanged(m_algorithmId);

    // Retile with new algorithm if enabled
    if (isEnabled()) {
        retile();
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
// Settings synchronization
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::syncFromSettings(Settings* settings)
{
    if (!settings) {
        return;
    }

    // Cancel any pending debounced retile — we are about to do a full resync
    m_settingsRetileTimer.stop();
    m_pendingSettingsRetile = false;

    m_settings = settings;

    // Temporarily clear autotile screens to prevent double-retile during configuration.
    // setAlgorithm() triggers retile() if enabled, so we configure everything first.
    const QSet<QString> savedScreens = m_autotileScreens;
    m_autotileScreens.clear();

    // Apply all settings to config (single source of truth for mapping)
    m_config->algorithmId = settings->autotileAlgorithm();
    m_config->splitRatio = settings->autotileSplitRatio();
    m_config->masterCount = settings->autotileMasterCount();
    m_config->innerGap = settings->autotileInnerGap();
    m_config->outerGap = settings->autotileOuterGap();
    m_config->focusNewWindows = settings->autotileFocusNewWindows();
    m_config->smartGaps = settings->autotileSmartGaps();
    m_config->insertPosition = static_cast<AutotileConfig::InsertPosition>(settings->autotileInsertPositionInt());

    // Additional settings
    m_config->focusFollowsMouse = settings->autotileFocusFollowsMouse();
    m_config->respectMinimumSize = settings->autotileRespectMinimumSize();
    m_config->monocleHideOthers = settings->autotileMonocleHideOthers();
    m_config->monocleShowTabs = settings->autotileMonocleShowTabs();

    // Set algorithm on engine (won't retile since m_enabled is false)
    m_algorithmId = settings->autotileAlgorithm();
    // Validate algorithm exists
    auto* registry = AlgorithmRegistry::instance();
    if (!registry->hasAlgorithm(m_algorithmId)) {
        qCWarning(lcAutotile) << "Unknown algorithm" << m_algorithmId << "- using default";
        m_algorithmId = AlgorithmRegistry::defaultAlgorithmId();
    }

    // Propagate split ratio and master count to existing per-screen states
    applyToAllStates([this](TilingState* state) {
        state->setSplitRatio(m_config->splitRatio);
        state->setMasterCount(m_config->masterCount);
    });

    // Restore autotile screens and retile once
    // Note: enabled state is derived from layout assignments, not settings.
    // The autotileEnabled setting is a feature gate handled by the daemon.
    m_autotileScreens = savedScreens;
    if (isEnabled()) {
        retile();
    }

    qCInfo(lcAutotile) << "Settings synced - algorithm:" << m_algorithmId
                       << "autotileScreens:" << m_autotileScreens.size();
}

void AutotileEngine::connectToSettings(Settings* settings)
{
    if (!settings) {
        return;
    }

    // Disconnect from previous settings if any (handles the case where
    // syncFromSettings was called first, which sets m_settings)
    if (m_settings) {
        disconnect(m_settings, nullptr, this, nullptr);
        qCDebug(lcAutotile) << "Disconnected from previous settings";
    }
    // Also disconnect from the new settings object in case of repeated calls with the same pointer
    disconnect(settings, nullptr, this, nullptr);

    m_settings = settings;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Macros for settings connections
    // ═══════════════════════════════════════════════════════════════════════════════

    // Pattern 1: Update config field + schedule retile
#define CONNECT_SETTING_RETILE(signal, field, getter)                                                                  \
    connect(settings, &Settings::signal, this, [this]() {                                                              \
        if (m_settings) {                                                                                              \
            m_config->field = m_settings->getter();                                                                    \
            scheduleSettingsRetile();                                                                                  \
        }                                                                                                              \
    })

    // Pattern 2: Update config field only (no retile)
#define CONNECT_SETTING_NO_RETILE(signal, field, getter)                                                               \
    connect(settings, &Settings::signal, this, [this]() {                                                              \
        if (m_settings) {                                                                                              \
            m_config->field = m_settings->getter();                                                                    \
        }                                                                                                              \
    })

    // ═══════════════════════════════════════════════════════════════════════════════
    // Immediate-effect settings (no debounce)
    // ═══════════════════════════════════════════════════════════════════════════════

    // Note: autotileEnabledChanged is NOT connected here. The KCM checkbox acts
    // as a feature gate — engine enabled state is driven by layout selection
    // (applyEntry) and mode toggle in the daemon.

    connect(settings, &Settings::autotileAlgorithmChanged, this, [this]() {
        if (m_settings) {
            m_config->algorithmId = m_settings->autotileAlgorithm();
            setAlgorithm(m_settings->autotileAlgorithm());
        }
    });

    // ═══════════════════════════════════════════════════════════════════════════════
    // Settings that require retile (debounced)
    // ═══════════════════════════════════════════════════════════════════════════════

    connect(settings, &Settings::autotileSplitRatioChanged, this, [this]() {
        if (m_settings) {
            m_config->splitRatio = m_settings->autotileSplitRatio();
            for (TilingState* state : m_screenStates) {
                if (state) {
                    state->setSplitRatio(m_config->splitRatio);
                }
            }
            scheduleSettingsRetile();
        }
    });

    connect(settings, &Settings::autotileMasterCountChanged, this, [this]() {
        if (m_settings) {
            m_config->masterCount = m_settings->autotileMasterCount();
            for (TilingState* state : m_screenStates) {
                if (state) {
                    state->setMasterCount(m_config->masterCount);
                }
            }
            scheduleSettingsRetile();
        }
    });
    CONNECT_SETTING_RETILE(autotileInnerGapChanged, innerGap, autotileInnerGap);
    CONNECT_SETTING_RETILE(autotileOuterGapChanged, outerGap, autotileOuterGap);
    CONNECT_SETTING_RETILE(autotileSmartGapsChanged, smartGaps, autotileSmartGaps);
    CONNECT_SETTING_RETILE(autotileRespectMinimumSizeChanged, respectMinimumSize, autotileRespectMinimumSize);

    // ═══════════════════════════════════════════════════════════════════════════════
    // Settings that don't require retile (config update only)
    // ═══════════════════════════════════════════════════════════════════════════════

    CONNECT_SETTING_NO_RETILE(autotileFocusNewWindowsChanged, focusNewWindows, autotileFocusNewWindows);
    CONNECT_SETTING_NO_RETILE(autotileFocusFollowsMouseChanged, focusFollowsMouse, autotileFocusFollowsMouse);
    CONNECT_SETTING_NO_RETILE(autotileMonocleHideOthersChanged, monocleHideOthers, autotileMonocleHideOthers);
    CONNECT_SETTING_NO_RETILE(autotileMonocleShowTabsChanged, monocleShowTabs, autotileMonocleShowTabs);

    // InsertPosition requires cast
    connect(settings, &Settings::autotileInsertPositionChanged, this, [this]() {
        if (m_settings) {
            m_config->insertPosition =
                static_cast<AutotileConfig::InsertPosition>(m_settings->autotileInsertPositionInt());
        }
    });

#undef CONNECT_SETTING_RETILE
#undef CONNECT_SETTING_NO_RETILE
}

// ═══════════════════════════════════════════════════════════════════════════════
// Session Persistence
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::saveState()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup group = config->group(QStringLiteral("AutoTileState"));

    // Save global state
    group.writeEntry("algorithm", m_algorithmId);
    group.writeEntry("autotileScreens", QStringList(m_autotileScreens.begin(), m_autotileScreens.end()));

    // Save per-screen state as JSON array
    QJsonArray screensArray;
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        const TilingState* state = it.value();
        if (!state) {
            continue;
        }

        QJsonObject screenObj;
        screenObj[QStringLiteral("screen")] = it.key();
        screenObj[QStringLiteral("masterCount")] = state->masterCount();
        screenObj[QStringLiteral("splitRatio")] = state->splitRatio();

        // Note: Window order and floating state are NOT saved because window IDs
        // (stableIds) may not match across sessions. Only per-screen parameters
        // (masterCount, splitRatio) are persisted and restored by loadState().

        screensArray.append(screenObj);
    }

    group.writeEntry("screenStates", QString::fromUtf8(QJsonDocument(screensArray).toJson(QJsonDocument::Compact)));
    config->sync();

    qCInfo(lcAutotile) << "Saved autotile state:" << m_screenStates.size() << "screens";
}

void AutotileEngine::loadState()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup group = config->group(QStringLiteral("AutoTileState"));

    if (!group.exists()) {
        qCDebug(lcAutotile) << "No saved autotile state found";
        return;
    }

    // Restore algorithm silently — do NOT emit algorithmChanged here.
    // loadState() is called during Daemon::start() before the event loop runs.
    // Emitting algorithmChanged triggers navigation OSD which creates a
    // QML/LayerShellQt window. On Wayland, that surface creation can deadlock
    // with the compositor if it's simultaneously performing synchronous D-Bus
    // introspection against this daemon (QDBusInterface constructor blocks the
    // compositor thread). See also: "Don't pre-create overlay windows at startup."
    const QString savedAlgorithm = group.readEntry("algorithm", m_algorithmId);
    if (AlgorithmRegistry::instance()->hasAlgorithm(savedAlgorithm)) {
        m_algorithmId = savedAlgorithm;
    }

    // Parse per-screen state
    const QString statesJson = group.readEntry("screenStates", QString());
    if (statesJson.isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(statesJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcAutotile) << "Failed to parse saved autotile state:" << parseError.errorString();
        return;
    }

    const QJsonArray screensArray = doc.array();
    for (const QJsonValue& val : screensArray) {
        const QJsonObject screenObj = val.toObject();
        const QString screenName = screenObj[QStringLiteral("screen")].toString();
        if (screenName.isEmpty()) {
            continue;
        }

        TilingState* state = stateForScreen(screenName);
        if (!state) {
            continue;
        }

        // Restore per-screen parameters (not window order — windows haven't been
        // announced yet and stableIds may not match across sessions)
        state->setMasterCount(screenObj[QStringLiteral("masterCount")].toInt(m_config->masterCount));
        state->setSplitRatio(screenObj[QStringLiteral("splitRatio")].toDouble(m_config->splitRatio));
    }

    // Restore autotile screens set
    const QStringList savedScreensList = group.readEntry("autotileScreens", QStringList());
    m_autotileScreens = QSet<QString>(savedScreensList.begin(), savedScreensList.end());

    // Emit enabledChanged so UI/D-Bus consumers update after session restore.
    // The actual retiling is deferred until windows are announced by KWin effect.
    if (!m_autotileScreens.isEmpty()) {
        Q_EMIT enabledChanged(true);
        Q_EMIT autotileScreensChanged(QStringList(m_autotileScreens.begin(), m_autotileScreens.end()));
    }

    qCInfo(lcAutotile) << "Loaded autotile state: algorithm=" << m_algorithmId
                       << "autotileScreens=" << m_autotileScreens.size() << "screenStates=" << screensArray.size();
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

    // Only retile if autotiling is enabled on any screen
    if (isEnabled()) {
        retile();
        qCDebug(lcAutotile) << "Settings changed - retiled windows";
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
                recalculateLayout(key);
                applyTiling(key);
                Q_EMIT tilingChanged(key);
            }
        }
    } else {
        if (!isAutotileScreen(screenName)) {
            return;
        }
        recalculateLayout(screenName);
        applyTiling(screenName);
        Q_EMIT tilingChanged(screenName);
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
    QString screenName;
    TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenName, state);

    if (windows.isEmpty() || !state) {
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("swap_master"), QStringLiteral("no_windows"),
                                           QString(), QString(), screenName);
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("swap_master"), QStringLiteral("no_focus"), QString(),
                                           QString(), screenName);
        return;
    }

    const bool promoted = state->moveToTiledPosition(focused, 0);
    retileAfterOperation(screenName, promoted);

    if (promoted) {
        Q_EMIT navigationFeedbackRequested(true, QStringLiteral("swap_master"), QStringLiteral("master"), QString(),
                                           QString(), screenName);
    } else {
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("swap_master"), QStringLiteral("already_master"),
                                           QString(), QString(), screenName);
    }
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
    QString screenName;
    TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenName, state);
    if (windows.isEmpty()) {
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("focus_master"), QStringLiteral("no_windows"),
                                           QString(), QString(), screenName);
        return;
    }
    emitFocusRequestAtIndex(0, true);
    Q_EMIT navigationFeedbackRequested(true, QStringLiteral("focus_master"), QStringLiteral("master"), QString(),
                                       QString(), screenName);
}

void AutotileEngine::emitFocusRequestAtIndex(int indexOffset, bool useFirst)
{
    QString screenName;
    TilingState* state = nullptr;
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

void AutotileEngine::setFocusedWindow(const QString& windowId)
{
    onWindowFocused(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Split ratio adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::increaseMasterRatio(qreal delta)
{
    applyToAllStates([delta](TilingState* state) {
        // setSplitRatio handles clamping internally
        state->setSplitRatio(state->splitRatio() + delta);
    });
    QString screenName =
        m_activeScreen.isEmpty() && !m_autotileScreens.isEmpty() ? *m_autotileScreens.begin() : m_activeScreen;
    QString reason = delta >= 0 ? QStringLiteral("increased") : QStringLiteral("decreased");
    Q_EMIT navigationFeedbackRequested(true, QStringLiteral("master_ratio"), reason, QString(), QString(), screenName);
}

void AutotileEngine::decreaseMasterRatio(qreal delta)
{
    increaseMasterRatio(-delta);
}

void AutotileEngine::setGlobalSplitRatio(qreal ratio)
{
    ratio = std::clamp(ratio, AutotileDefaults::MinSplitRatio, AutotileDefaults::MaxSplitRatio);
    m_config->splitRatio = ratio;
    applyToAllStates([ratio](TilingState* state) {
        state->setSplitRatio(ratio);
    });
}

void AutotileEngine::setGlobalMasterCount(int count)
{
    count = std::clamp(count, AutotileDefaults::MinMasterCount, AutotileDefaults::MaxMasterCount);
    m_config->masterCount = count;
    applyToAllStates([count](TilingState* state) {
        state->setMasterCount(count);
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// Master count adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::increaseMasterCount()
{
    applyToAllStates([](TilingState* state) {
        state->setMasterCount(state->masterCount() + 1);
    });
    QString screenName =
        m_activeScreen.isEmpty() && !m_autotileScreens.isEmpty() ? *m_autotileScreens.begin() : m_activeScreen;
    Q_EMIT navigationFeedbackRequested(true, QStringLiteral("master_count"), QStringLiteral("increased"), QString(),
                                       QString(), screenName);
}

void AutotileEngine::decreaseMasterCount()
{
    applyToAllStates([](TilingState* state) {
        if (state->masterCount() > 1) {
            state->setMasterCount(state->masterCount() - 1);
        }
    });
    QString screenName =
        m_activeScreen.isEmpty() && !m_autotileScreens.isEmpty() ? *m_autotileScreens.begin() : m_activeScreen;
    Q_EMIT navigationFeedbackRequested(true, QStringLiteral("master_count"), QStringLiteral("decreased"), QString(),
                                       QString(), screenName);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window rotation and floating
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::rotateWindowOrder(bool clockwise)
{
    QString screenName;
    TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenName, state);

    if (windows.size() < 2 || !state) {
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("rotate"), QStringLiteral("nothing_to_rotate"),
                                           QString(), QString(), screenName);
        return; // Nothing to rotate with 0 or 1 window
    }

    // Rotate the window order
    bool rotated = state->rotateWindows(clockwise);
    retileAfterOperation(screenName, rotated);

    if (rotated) {
        QString reason = QStringLiteral("%1:%2")
                             .arg(clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise"))
                             .arg(windows.size());
        Q_EMIT navigationFeedbackRequested(true, QStringLiteral("rotate"), reason, QString(), QString(), screenName);
    } else {
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("rotate"), QStringLiteral("no_rotations"), QString(),
                                           QString(), screenName);
    }

    qCInfo(lcAutotile) << "Rotated windows" << (clockwise ? "clockwise" : "counterclockwise");
}

void AutotileEngine::toggleFocusedWindowFloat()
{
    QString screenName;
    TilingState* state = nullptr;
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
    retileAfterOperation(screenName, true); // Always retile after successful toggle

    bool isNowFloating = state->isFloating(focused);
    qCInfo(lcAutotile) << "Window" << focused << (isNowFloating ? "now floating" : "now tiled");
    Q_EMIT windowFloatingChanged(focused, isNowFloating, screenName);
}

void AutotileEngine::floatWindow(const QString& windowId)
{
    if (!warnIfEmptyWindowId(windowId, "floatWindow")) {
        return;
    }

    if (!isAutotileScreen(m_windowToScreen.value(windowId))) {
        return;
    }

    TilingState* state = stateForWindow(windowId);
    if (!state) {
        qCDebug(lcAutotile) << "floatWindow: window not tracked:" << windowId;
        return;
    }

    if (state->isFloating(windowId)) {
        qCDebug(lcAutotile) << "floatWindow: window already floating:" << windowId;
        return;
    }

    state->setFloating(windowId, true);
    QString screenName = m_windowToScreen.value(windowId);
    retileAfterOperation(screenName, true);

    qCInfo(lcAutotile) << "Window floated from autotile:" << windowId;
    Q_EMIT windowFloatingChanged(windowId, true, screenName);
}

void AutotileEngine::unfloatWindow(const QString& windowId)
{
    if (!warnIfEmptyWindowId(windowId, "unfloatWindow")) {
        return;
    }

    TilingState* state = stateForWindow(windowId);
    if (!state) {
        qCDebug(lcAutotile) << "unfloatWindow: window not tracked:" << windowId;
        return;
    }

    if (!state->isFloating(windowId)) {
        qCDebug(lcAutotile) << "unfloatWindow: window not floating:" << windowId;
        return;
    }

    state->setFloating(windowId, false);
    QString screenName = m_windowToScreen.value(windowId);
    retileAfterOperation(screenName, true);

    qCInfo(lcAutotile) << "Window unfloated to autotile:" << windowId;
    Q_EMIT windowFloatingChanged(windowId, false, screenName);
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

void AutotileEngine::windowClosed(const QString& windowId)
{
    if (!warnIfEmptyWindowId(windowId, "windowClosed")) {
        return;
    }

    onWindowRemoved(windowId);
}

void AutotileEngine::windowFocused(const QString& windowId, const QString& screenName)
{
    if (!warnIfEmptyWindowId(windowId, "windowFocused")) {
        return;
    }

    // Update screen mapping - always store when provided, even for new windows
    if (!screenName.isEmpty()) {
        m_windowToScreen[windowId] = screenName;
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

    constexpr int MaxWindowsPerScreen = 50;
    TilingState* state = stateForScreen(screenName);
    if (state && state->tiledWindowCount() >= MaxWindowsPerScreen) {
        qCDebug(lcAutotile) << "Max window limit reached for screen" << screenName;
        return;
    }

    const bool inserted = insertWindow(windowId, screenName);
    retileAfterOperation(screenName, inserted);

    if (inserted && m_config && m_config->focusNewWindows) {
        Q_EMIT focusWindowRequested(windowId);
    }
}

void AutotileEngine::onWindowRemoved(const QString& windowId)
{
    const QString screenName = m_windowToScreen.value(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    removeWindow(windowId);
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

    // In monocle mode with monocleHideOthers, update window visibility
    // when focus changes so the newly focused window is shown
    if (isAutotileScreen(m_windowToScreen.value(windowId)) && m_algorithmId == DBus::AutotileAlgorithm::Monocle
        && m_config->monocleHideOthers) {
        const QStringList windows = state->tiledWindows();
        if (windows.size() > 1) {
            emitMonocleVisibility(state, windows);
        }
    }
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

void AutotileEngine::removeWindow(const QString& windowId)
{
    m_windowMinSizes.remove(windowId);
    const QString screenName = m_windowToScreen.take(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    TilingState* state = m_screenStates.value(screenName);
    if (state) {
        state->removeWindow(windowId);
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

    TilingAlgorithm* algo = currentAlgorithm();
    if (!algo) {
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: no algorithm set";
        return;
    }

    const int windowCount = state->tiledWindowCount();
    if (windowCount == 0) {
        state->setCalculatedZones({}); // Clear zones when no windows
        return;
    }

    const QRect screen = screenGeometry(screenName);
    if (!screen.isValid()) {
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: invalid screen geometry";
        return;
    }

    qCDebug(lcAutotile) << "recalculateLayout: screen=" << screenName << "geometry=" << screen
                        << "windows=" << windowCount << "algo=" << m_algorithmId;

    // Calculate zone geometries using the algorithm, with gap-aware zones.
    // Algorithms apply gaps directly using their topology knowledge, eliminating
    // the fragile post-processing step that previously guessed adjacency.
    const bool skipGaps = m_config->smartGaps && windowCount == 1;
    const int innerGap = skipGaps ? 0 : m_config->innerGap;
    const int outerGap = skipGaps ? 0 : m_config->outerGap;
    // Build minSizes vector for the algorithm (when respectMinimumSize is enabled)
    QVector<QSize> minSizes;
    if (m_config->respectMinimumSize) {
        const QStringList windows = state->tiledWindows();
        // KWin reports min size in logical pixels (same as QScreen/zone geometry);
        // do not divide by devicePixelRatio or we under-report and steal too little.
        minSizes.resize(windows.size());
        for (int i = 0; i < windows.size(); ++i) {
            minSizes[i] = m_windowMinSizes.value(windows[i]);
        }
    }

    // Pass minSizes to algorithm so it can incorporate them directly into zone
    // calculations using its topology knowledge (split tree, column structure, etc.)
    QVector<QRect> zones = algo->calculateZones({windowCount, screen, state, innerGap, outerGap, minSizes});

    // Validate algorithm returned correct number of zones
    if (zones.size() != windowCount) {
        qCWarning(lcAutotile) << "AutotileEngine::recalculateLayout: algorithm returned" << zones.size() << "zones for"
                   << windowCount << "windows";
        return;
    }

    // Lightweight safety net: the algorithm handles min sizes directly, but
    // enforceWindowMinSizes catches any residual deficits from rounding or
    // edge cases the algorithm couldn't fully solve (e.g., unsatisfiable constraints).
    if (m_config->respectMinimumSize && !minSizes.isEmpty()) {
        const int threshold = m_config->innerGap + qMax(AutotileDefaults::GapEdgeThresholdPx, 12);
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

    if (windows.size() != zones.size()) {
        qCWarning(lcAutotile) << "AutotileEngine::applyTiling: window/zone count mismatch" << windows.size() << "vs"
                   << zones.size();
        return;
    }

    // Build batch JSON and emit once to avoid race when effect applies many geometries
    QJsonArray arr;
    for (int i = 0; i < windows.size(); ++i) {
        const QRect& geo = zones[i];
        QJsonObject obj;
        obj[QLatin1String("windowId")] = windows[i];
        obj[QLatin1String("x")] = geo.x();
        obj[QLatin1String("y")] = geo.y();
        obj[QLatin1String("width")] = geo.width();
        obj[QLatin1String("height")] = geo.height();
        arr.append(obj);
    }
    Q_EMIT windowsTiled(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));

    // Monocle visibility management: when algorithm is "monocle" and
    // monocleHideOthers is enabled, minimize all tiled windows except
    // the focused one (or the first window if none focused)
    if (m_algorithmId == DBus::AutotileAlgorithm::Monocle && m_config->monocleHideOthers && windows.size() > 1) {
        emitMonocleVisibility(state, windows);
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
        recalculateLayout(screenName);
        applyTiling(screenName);
        Q_EMIT tilingChanged(screenName);
        return;
    }

    QScopeGuard guard([this] {
        m_retiling = false;
    });
    m_retiling = true;
    recalculateLayout(screenName);
    applyTiling(screenName);
    Q_EMIT tilingChanged(screenName);
}

QStringList AutotileEngine::tiledWindowsForFocusedScreen(QString& outScreenName, TilingState*& outState)
{
    outState = nullptr;

    // Use the tracked active screen (set by onWindowFocused) to avoid
    // non-deterministic QHash iteration when multiple screens have focused windows
    if (!m_activeScreen.isEmpty() && m_screenStates.contains(m_activeScreen)) {
        TilingState* state = m_screenStates.value(m_activeScreen);
        if (state && !state->focusedWindow().isEmpty()) {
            outScreenName = m_activeScreen;
            outState = state;
            return state->tiledWindows();
        }
    }

    // Fallback: scan all states (e.g., if m_activeScreen is stale)
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        TilingState* state = it.value();
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
            TilingState* state = m_screenStates.value(outScreenName);
            if (state) {
                outState = state;
                return state->tiledWindows();
            }
        }
    }

    outScreenName.clear();
    return {};
}

void AutotileEngine::applyToAllStates(const std::function<void(TilingState*)>& operation)
{
    if (m_screenStates.isEmpty()) {
        return; // No states to modify
    }

    for (TilingState* state : m_screenStates) {
        if (state) {
            operation(state);
        }
    }

    if (isEnabled()) {
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

void AutotileEngine::emitMonocleVisibility(const TilingState* state, const QStringList& tiledWindows)
{
    if (!state || tiledWindows.isEmpty()) {
        return;
    }

    // Determine which window should be visible: focused, or first if none focused
    QString focused = state->focusedWindow();
    if (focused.isEmpty() || !tiledWindows.contains(focused)) {
        focused = tiledWindows.first();
    }

    // Build list of windows to hide (all tiled except focused)
    QStringList toHide;
    toHide.reserve(tiledWindows.size() - 1);
    for (const QString& wid : tiledWindows) {
        if (wid != focused) {
            toHide.append(wid);
        }
    }

    Q_EMIT monocleVisibilityChanged(focused, toHide);
}

} // namespace PlasmaZones
