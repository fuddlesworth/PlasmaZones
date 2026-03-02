// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Qt headers
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QScreen>
#include <QSignalBlocker>

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
        // Defer retile via event-loop coalescing. On first activation, windows
        // haven't been announced yet (KWin effect sends them after receiving
        // autotileScreensChanged), so retiling an empty screen is wasted work.
        // On subsequent screen additions, windows are already known — the
        // one-event-loop-pass delay (~0ms) is negligible.
        scheduleRetileForScreen(screenName);
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
            m_perScreenOverrides.remove(it.key());
            m_pendingInitialOrders.remove(it.key());
            it.value()->deleteLater();
            it.remove();
        }
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
    for (auto pit = m_pendingRetileScreens.begin(); pit != m_pendingRetileScreens.end(); ) {
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
        const qreal newDefault = newAlgo->defaultSplitRatio();
        if (!qFuzzyCompare(1.0 + m_config->splitRatio, 1.0 + newDefault)) {
            m_config->splitRatio = newDefault;
        }
        propagateGlobalSplitRatio();

        // Same pattern for maxWindows: if the user hasn't customized it away
        // from the old algorithm's default, reset to the new algorithm's default.
        // Without this, switching from MasterStack (4) to BSP (5) keeps maxWindows=4.
        resetMaxWindowsForAlgorithmSwitch(oldAlgo, newAlgo);
    } else if (newAlgo) {
        // oldAlgo is nullptr (first-ever call or corrupted m_algorithmId).
        // Initialize config from the new algorithm's defaults.
        m_config->splitRatio = newAlgo->defaultSplitRatio();
        m_config->maxWindows = newAlgo->defaultMaxWindows();
        propagateGlobalSplitRatio();
    }

    // Persist ALL changed fields back to settings to avoid desync between
    // the engine's runtime state and the Settings object. During layout
    // cycling, several daemon/engine paths read from Settings (e.g.,
    // updateAutotileScreens reads autotileMaxWindows, syncFromSettings reads
    // autotileAlgorithm and splitRatio). Stale Settings values cause wrong
    // per-screen overrides, recursive setAlgorithm calls with the old KCM
    // algorithm, and splitRatio corruption.
    //
    // Block ALL signals from m_settings during these writes. Each setter
    // macro emits both its specific signal AND settingsChanged, which would
    // trigger the daemon's settingsChanged handler → syncFromSettings() →
    // setAlgorithm(stale KCM algo), producing a recursive corruption loop.
    // setAlgorithm() schedules its own deferred retile below.
    //
    // This runs outside the oldAlgo&&newAlgo guard so that Settings is always
    // synced — even on first-ever call when m_algorithmId was empty.
    if (m_settings) {
        const QSignalBlocker blocker(m_settings);
        if (m_config->maxWindows != oldMaxWindows) {
            m_settings->setAutotileMaxWindows(m_config->maxWindows);
        }
        m_settings->setAutotileAlgorithm(newId);
        m_settings->setAutotileSplitRatio(m_config->splitRatio);
    }

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
        qCDebug(lcAutotile) << "setInitialWindowOrder: screen" << screenName
                            << "already has" << state->windowCount() << "windows, ignoring pre-seeded order";
        return;
    }
    // Warn (but allow) if overwriting a pending order that hasn't been fully consumed
    if (m_pendingInitialOrders.contains(screenName)) {
        qCWarning(lcAutotile) << "setInitialWindowOrder: overwriting existing pending order for" << screenName;
    }
    m_pendingInitialOrders[screenName] = windowIds;
    qCInfo(lcAutotile) << "Pre-seeded window order for screen" << screenName << ":" << windowIds;
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
    if (!settings) {
        return;
    }

    m_settings = settings;

    // Track whether any config field actually changed. If individual signal
    // handlers (from runtime setters) already updated config, this detects
    // no changes and skips the redundant retile at the end.
    bool configChanged = false;

    // Capture old maxWindows before updating — used for backfill below
    const int oldMaxWindows = m_config->maxWindows;

    // Apply all settings to config, tracking changes.
    // Note: algorithmId is excluded — it is synced to m_algorithmId (the
    // authoritative engine field) separately below with validation.
    // splitRatio uses qFuzzyCompare for floating-point safety.
#define SYNC_FIELD(field, getter) \
    do { \
        auto newVal = settings->getter(); \
        if (m_config->field != newVal) { m_config->field = newVal; configChanged = true; } \
    } while (0)

    SYNC_FIELD(masterCount, autotileMasterCount);
    SYNC_FIELD(innerGap, autotileInnerGap);
    SYNC_FIELD(outerGap, autotileOuterGap);
    SYNC_FIELD(usePerSideOuterGap, autotileUsePerSideOuterGap);
    SYNC_FIELD(outerGapTop, autotileOuterGapTop);
    SYNC_FIELD(outerGapBottom, autotileOuterGapBottom);
    SYNC_FIELD(outerGapLeft, autotileOuterGapLeft);
    SYNC_FIELD(outerGapRight, autotileOuterGapRight);
    SYNC_FIELD(focusNewWindows, autotileFocusNewWindows);
    SYNC_FIELD(smartGaps, autotileSmartGaps);
    SYNC_FIELD(focusFollowsMouse, autotileFocusFollowsMouse);
    SYNC_FIELD(respectMinimumSize, autotileRespectMinimumSize);
    SYNC_FIELD(maxWindows, autotileMaxWindows);
    // splitRatio: qreal needs fuzzy comparison to avoid spurious change detection
    {
        const qreal newRatio = settings->autotileSplitRatio();
        if (!qFuzzyCompare(1.0 + m_config->splitRatio, 1.0 + newRatio)) {
            m_config->splitRatio = newRatio;
            configChanged = true;
        }
    }

    // InsertPosition needs a cast
    {
        auto newInsert = static_cast<AutotileConfig::InsertPosition>(settings->autotileInsertPositionInt());
        if (m_config->insertPosition != newInsert) {
            m_config->insertPosition = newInsert;
            configChanged = true;
        }
    }

#undef SYNC_FIELD

    // Sync algorithm via setAlgorithm (handles validation + fallback + m_config sync)
    const QString oldAlgorithmId = m_algorithmId;
    setAlgorithm(settings->autotileAlgorithm());
    if (m_algorithmId != oldAlgorithmId) {
        configChanged = true;
    }

    // Propagate split ratio and master count to screens WITHOUT per-screen overrides.
    // Screens with per-screen overrides are handled by updateAutotileScreens()
    // (called by the daemon after syncFromSettings returns).
    propagateGlobalSplitRatio();
    propagateGlobalMasterCount();

    // Backfill windows when maxWindows increased: windows rejected by the old
    // gate check in onWindowAdded() stay untiled unless we add them here.
    if (m_config->maxWindows > oldMaxWindows) {
        backfillWindows();
    }

    if (configChanged && isEnabled()) {
        // Cancel any pending debounced retile — we are doing a full resync
        m_settingsRetileTimer.stop();
        m_pendingSettingsRetile = false;
        // Cancel deferred retiles from setAlgorithm() — the immediate retile()
        // below covers all screens. Without this, the deferred retile fires on
        // the next event loop pass and emits a redundant windowsTiled D-Bus
        // signal with identical geometry data.
        m_pendingRetileScreens.clear();
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
    // These handlers fire from runtime setters only (not from load() — load()
    // only emits settingsChanged, which triggers syncFromSettings).
#define CONNECT_SETTING_RETILE(signal, field, getter)                                                                  \
    connect(settings, &Settings::signal, this, [this]() {                                                              \
        if (!m_settings) return;                                                                                       \
        m_config->field = m_settings->getter();                                                                        \
        scheduleSettingsRetile();                                                                                      \
    })

    // Pattern 2: Update config field only (no retile)
#define CONNECT_SETTING_NO_RETILE(signal, field, getter)                                                               \
    connect(settings, &Settings::signal, this, [this]() {                                                              \
        if (!m_settings) return;                                                                                       \
        m_config->field = m_settings->getter();                                                                        \
    })

    // ═══════════════════════════════════════════════════════════════════════════════
    // Immediate-effect settings (no debounce)
    // ═══════════════════════════════════════════════════════════════════════════════

    // Note: autotileEnabledChanged is NOT connected here. The KCM checkbox acts
    // as a feature gate — engine enabled state is driven by layout selection
    // (applyEntry) and mode toggle in the daemon.

    connect(settings, &Settings::autotileAlgorithmChanged, this, [this]() {
        if (!m_settings) return;
        setAlgorithm(m_settings->autotileAlgorithm());
    });

    // ═══════════════════════════════════════════════════════════════════════════════
    // Settings that require retile (debounced)
    // ═══════════════════════════════════════════════════════════════════════════════

    connect(settings, &Settings::autotileSplitRatioChanged, this, [this]() {
        if (!m_settings) return;
        m_config->splitRatio = m_settings->autotileSplitRatio();
        propagateGlobalSplitRatio();
        scheduleSettingsRetile();
    });

    connect(settings, &Settings::autotileMasterCountChanged, this, [this]() {
        if (!m_settings) return;
        m_config->masterCount = m_settings->autotileMasterCount();
        propagateGlobalMasterCount();
        scheduleSettingsRetile();
    });
    CONNECT_SETTING_RETILE(autotileInnerGapChanged, innerGap, autotileInnerGap);
    CONNECT_SETTING_RETILE(autotileOuterGapChanged, outerGap, autotileOuterGap);
    CONNECT_SETTING_RETILE(autotileUsePerSideOuterGapChanged, usePerSideOuterGap, autotileUsePerSideOuterGap);
    CONNECT_SETTING_RETILE(autotileOuterGapTopChanged, outerGapTop, autotileOuterGapTop);
    CONNECT_SETTING_RETILE(autotileOuterGapBottomChanged, outerGapBottom, autotileOuterGapBottom);
    CONNECT_SETTING_RETILE(autotileOuterGapLeftChanged, outerGapLeft, autotileOuterGapLeft);
    CONNECT_SETTING_RETILE(autotileOuterGapRightChanged, outerGapRight, autotileOuterGapRight);
    CONNECT_SETTING_RETILE(autotileSmartGapsChanged, smartGaps, autotileSmartGaps);
    CONNECT_SETTING_RETILE(autotileRespectMinimumSizeChanged, respectMinimumSize, autotileRespectMinimumSize);

    // MaxWindows needs a custom handler: when the limit increases, backfill
    // windows that were previously rejected by onWindowAdded's gate check.
    connect(settings, &Settings::autotileMaxWindowsChanged, this, [this]() {
        if (!m_settings) return;
        const int oldMax = m_config->maxWindows;
        m_config->maxWindows = m_settings->autotileMaxWindows();

        // When max increases, try to add windows that exist on autotile screens
        // but were rejected when the previous limit was reached.
        if (m_config->maxWindows > oldMax) {
            backfillWindows();
        }
        scheduleSettingsRetile();
    });

    // ═══════════════════════════════════════════════════════════════════════════════
    // Settings that don't require retile (config update only)
    // ═══════════════════════════════════════════════════════════════════════════════

    CONNECT_SETTING_NO_RETILE(autotileFocusNewWindowsChanged, focusNewWindows, autotileFocusNewWindows);
    CONNECT_SETTING_NO_RETILE(autotileFocusFollowsMouseChanged, focusFollowsMouse, autotileFocusFollowsMouse);

    // InsertPosition requires cast
    connect(settings, &Settings::autotileInsertPositionChanged, this, [this]() {
        if (!m_settings) return;
        m_config->insertPosition =
            static_cast<AutotileConfig::InsertPosition>(m_settings->autotileInsertPositionInt());
    });

#undef CONNECT_SETTING_RETILE
#undef CONNECT_SETTING_NO_RETILE
}

void AutotileEngine::applyPerScreenConfig(const QString& screenName, const QVariantMap& overrides)
{
    if (screenName.isEmpty()) {
        return;
    }

    if (overrides.isEmpty()) {
        clearPerScreenConfig(screenName);
        return;
    }

    // Store overrides so effective*() helpers and connectToSettings handlers
    // can resolve per-screen values and skip screens with overrides.
    m_perScreenOverrides[screenName] = overrides;

    TilingState* state = stateForScreen(screenName);
    if (!state) {
        return;
    }

    // Apply TilingState-level overrides (splitRatio, masterCount)
    auto it = overrides.constFind(QStringLiteral("SplitRatio"));
    if (it != overrides.constEnd()) {
        state->setSplitRatio(qBound(AutotileDefaults::MinSplitRatio, it->toDouble(), AutotileDefaults::MaxSplitRatio));
    }

    it = overrides.constFind(QStringLiteral("MasterCount"));
    if (it != overrides.constEnd()) {
        state->setMasterCount(qBound(AutotileDefaults::MinMasterCount, it->toInt(), AutotileDefaults::MaxMasterCount));
    }

    // If algorithm changed and split ratio wasn't explicitly overridden,
    // reset to the new algorithm's default (matching setAlgorithm() logic).
    it = overrides.constFind(QStringLiteral("Algorithm"));
    if (it != overrides.constEnd()) {
        QString algoId = it->toString();
        auto* registry = AlgorithmRegistry::instance();
        TilingAlgorithm* newAlgo = registry->algorithm(algoId);
        if (newAlgo) {
            if (!overrides.contains(QStringLiteral("SplitRatio"))) {
                state->setSplitRatio(newAlgo->defaultSplitRatio());
            }
        }
    }

    // Gap overrides (InnerGap, OuterGap, SmartGaps) and RespectMinimumSize are
    // resolved at retile time via effective*() helpers in recalculateLayout().

    // Schedule a deferred retile so the new config takes effect. Deferred (not
    // immediate) to coalesce with other pending retiles — e.g., when applyEntry()
    // triggers both updateAutotileScreens() → applyPerScreenConfig() and
    // setAlgorithm() → scheduleRetileForScreen(), a single retile fires with
    // all state consistent, avoiding the double-D-Bus-signal problem that caused
    // stagger generation conflicts and window overlap during algorithm switches.
    if (isAutotileScreen(screenName)) {
        scheduleRetileForScreen(screenName);
    }

    qCDebug(lcAutotile) << "Applied per-screen config for" << screenName
                        << "keys:" << overrides.keys();
}

void AutotileEngine::clearPerScreenConfig(const QString& screenName)
{
    if (!m_perScreenOverrides.remove(screenName)) {
        return;
    }
    // Restore global defaults on TilingState
    TilingState* state = m_screenStates.value(screenName);
    if (state) {
        state->setSplitRatio(m_config->splitRatio);
        state->setMasterCount(m_config->masterCount);
    }

    // Schedule deferred retile (same rationale as applyPerScreenConfig)
    if (isAutotileScreen(screenName)) {
        scheduleRetileForScreen(screenName);
    }

    qCDebug(lcAutotile) << "Cleared per-screen config for" << screenName;
}

QVariantMap AutotileEngine::perScreenOverrides(const QString& screenName) const
{
    return m_perScreenOverrides.value(screenName);
}

bool AutotileEngine::hasPerScreenOverride(const QString& screenName, const QString& key) const
{
    auto it = m_perScreenOverrides.constFind(screenName);
    return it != m_perScreenOverrides.constEnd() && it->contains(key);
}

std::optional<QVariant> AutotileEngine::perScreenOverride(const QString& screenName, const QString& key) const
{
    auto it = m_perScreenOverrides.constFind(screenName);
    if (it != m_perScreenOverrides.constEnd()) {
        auto git = it->constFind(key);
        if (git != it->constEnd()) {
            return *git;
        }
    }
    return std::nullopt;
}

int AutotileEngine::effectiveInnerGap(const QString& screenName) const
{
    if (auto v = perScreenOverride(screenName, QStringLiteral("InnerGap")))
        return qBound(AutotileDefaults::MinGap, v->toInt(), AutotileDefaults::MaxGap);
    return m_config->innerGap;
}

int AutotileEngine::effectiveOuterGap(const QString& screenName) const
{
    if (auto v = perScreenOverride(screenName, QStringLiteral("OuterGap")))
        return qBound(AutotileDefaults::MinGap, v->toInt(), AutotileDefaults::MaxGap);
    return m_config->outerGap;
}

EdgeGaps AutotileEngine::effectiveOuterGaps(const QString& screenName) const
{
    // Check per-screen per-side overrides first
    auto topOv = perScreenOverride(screenName, QStringLiteral("OuterGapTop"));
    auto bottomOv = perScreenOverride(screenName, QStringLiteral("OuterGapBottom"));
    auto leftOv = perScreenOverride(screenName, QStringLiteral("OuterGapLeft"));
    auto rightOv = perScreenOverride(screenName, QStringLiteral("OuterGapRight"));

    // If any per-screen per-side override exists, build from those
    if (topOv || bottomOv || leftOv || rightOv) {
        // Use per-screen uniform gap as base, then per-side overrides on top
        const int base = effectiveOuterGap(screenName);
        return EdgeGaps{
            topOv ? qBound(AutotileDefaults::MinGap, topOv->toInt(), AutotileDefaults::MaxGap) : base,
            bottomOv ? qBound(AutotileDefaults::MinGap, bottomOv->toInt(), AutotileDefaults::MaxGap) : base,
            leftOv ? qBound(AutotileDefaults::MinGap, leftOv->toInt(), AutotileDefaults::MaxGap) : base,
            rightOv ? qBound(AutotileDefaults::MinGap, rightOv->toInt(), AutotileDefaults::MaxGap) : base
        };
    }

    // Check per-screen uniform outer gap
    if (auto v = perScreenOverride(screenName, QStringLiteral("OuterGap"))) {
        const int gap = qBound(AutotileDefaults::MinGap, v->toInt(), AutotileDefaults::MaxGap);
        return EdgeGaps::uniform(gap);
    }

    // Fall back to global config
    if (m_config->usePerSideOuterGap) {
        return EdgeGaps{m_config->outerGapTop, m_config->outerGapBottom,
                        m_config->outerGapLeft, m_config->outerGapRight};
    }
    return EdgeGaps::uniform(m_config->outerGap);
}

bool AutotileEngine::effectiveSmartGaps(const QString& screenName) const
{
    if (auto v = perScreenOverride(screenName, QStringLiteral("SmartGaps")))
        return v->toBool();
    return m_config->smartGaps;
}

bool AutotileEngine::effectiveRespectMinimumSize(const QString& screenName) const
{
    if (auto v = perScreenOverride(screenName, QStringLiteral("RespectMinimumSize")))
        return v->toBool();
    return m_config->respectMinimumSize;
}

int AutotileEngine::effectiveMaxWindows(const QString& screenName) const
{
    // 1. Explicit per-screen MaxWindows override — highest priority
    if (auto v = perScreenOverride(screenName, QLatin1String("MaxWindows")))
        return qBound(AutotileDefaults::MinMaxWindows, v->toInt(), AutotileDefaults::MaxMaxWindows);

    // 2. When the per-screen algorithm differs from the global algorithm,
    //    the global m_config->maxWindows may be for the WRONG algorithm.
    //    E.g. global=master-stack(maxWindows=4) but per-screen=bsp(default=5).
    //    Use the per-screen algorithm's default — but only if the user hasn't
    //    explicitly customized global maxWindows away from the global algo's default.
    const QString screenAlgo = effectiveAlgorithmId(screenName);
    if (screenAlgo != m_algorithmId) {
        auto* registry = AlgorithmRegistry::instance();
        auto* screenAlgoPtr = registry->algorithm(screenAlgo);
        auto* globalAlgoPtr = registry->algorithm(m_algorithmId);
        if (screenAlgoPtr) {
            // Only override with per-screen default if global is still at its algo's default
            if (!globalAlgoPtr || m_config->maxWindows == globalAlgoPtr->defaultMaxWindows()) {
                return screenAlgoPtr->defaultMaxWindows();
            }
            // User explicitly customized global maxWindows — honor it
            return m_config->maxWindows;
        }
        qCWarning(lcAutotile) << "effectiveMaxWindows: unknown per-screen algorithm"
                               << screenAlgo << "for screen" << screenName
                               << "- falling back to global maxWindows";
    }

    // 3. Same algorithm globally and per-screen — use the global setting
    return m_config->maxWindows;
}

QString AutotileEngine::effectiveAlgorithmId(const QString& screenName) const
{
    if (auto v = perScreenOverride(screenName, QLatin1String("Algorithm")))
        return v->toString();
    return m_algorithmId;
}

TilingAlgorithm* AutotileEngine::effectiveAlgorithm(const QString& screenName) const
{
    return AlgorithmRegistry::instance()->algorithm(effectiveAlgorithmId(screenName));
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
        QMetaObject::invokeMethod(this, &AutotileEngine::processPendingRetiles,
                                  Qt::QueuedConnection);
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

void AutotileEngine::swapFocusedInDirection(const QString& direction, const QString& action)
{
    const bool forward = (direction == QLatin1String("right") || direction == QLatin1String("down"));

    QString screenName;
    TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenName, state);

    if (windows.size() < 2 || !state) {
        Q_EMIT navigationFeedbackRequested(false, action,
                                           QStringLiteral("nothing_to_swap"),
                                           QString(), QString(), screenName);
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        Q_EMIT navigationFeedbackRequested(false, action,
                                           QStringLiteral("no_focus"),
                                           QString(), QString(), screenName);
        return;
    }

    const int currentIndex = windows.indexOf(focused);
    if (currentIndex < 0) {
        Q_EMIT navigationFeedbackRequested(false, action,
                                           QStringLiteral("no_focus"),
                                           QString(), QString(), screenName);
        return;
    }

    int targetIndex = forward ? currentIndex + 1 : currentIndex - 1;
    // Wrap around
    if (targetIndex < 0) {
        targetIndex = windows.size() - 1;
    } else if (targetIndex >= windows.size()) {
        targetIndex = 0;
    }

    const QString targetWindow = windows.at(targetIndex);
    const bool swapped = state->swapWindowsById(focused, targetWindow);
    retileAfterOperation(screenName, swapped);

    Q_EMIT navigationFeedbackRequested(swapped, action, direction,
                                       QString(), QString(), screenName);
}

void AutotileEngine::focusInDirection(const QString& direction, const QString& action)
{
    const bool forward = (direction == QLatin1String("right") || direction == QLatin1String("down"));

    QString screenName;
    TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenName, state);

    if (windows.isEmpty() || !state) {
        Q_EMIT navigationFeedbackRequested(false, action,
                                           QStringLiteral("no_windows"),
                                           QString(), QString(), screenName);
        return;
    }

    const QString focused = state->focusedWindow();
    const int currentIndex = qMax(0, windows.indexOf(focused));
    const int targetIndex = (currentIndex + (forward ? 1 : -1) + windows.size()) % windows.size();

    Q_EMIT focusWindowRequested(windows.at(targetIndex));
    Q_EMIT navigationFeedbackRequested(true, action, direction,
                                       QString(), QString(), screenName);
}

void AutotileEngine::moveFocusedToPosition(int position)
{
    QString screenName;
    TilingState* state = nullptr;
    const QStringList windows = tiledWindowsForFocusedScreen(screenName, state);

    if (windows.isEmpty() || !state) {
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("snap"),
                                           QStringLiteral("no_windows"),
                                           QString(), QString(), screenName);
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("snap"),
                                           QStringLiteral("no_focus"),
                                           QString(), QString(), screenName);
        return;
    }

    // position is 1-based (from snap-to-zone-N shortcuts), convert to 0-based
    const int targetIndex = qBound(0, position - 1, windows.size() - 1);
    const bool moved = state->moveToTiledPosition(focused, targetIndex);
    retileAfterOperation(screenName, moved);

    if (moved) {
        Q_EMIT navigationFeedbackRequested(true, QStringLiteral("snap"),
                                           QStringLiteral("position_%1").arg(position),
                                           QString(), QString(), screenName);
    } else {
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("snap"),
                                           QStringLiteral("already_at_position"),
                                           QString(), QString(), screenName);
    }
}

void AutotileEngine::toggleFocusedWindowFloat()
{
    QString screenName;
    TilingState* state = nullptr;
    tiledWindowsForFocusedScreen(screenName, state);

    if (!state) {
        qCWarning(lcAutotile) << "toggleFocusedWindowFloat: no state found for focused screen"
                               << "(m_activeScreen=" << m_activeScreen << ")";
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"),
                                           QStringLiteral("no_focused_screen"),
                                           QString(), QString(), m_activeScreen);
        return;
    }

    const QString focused = state->focusedWindow();
    if (focused.isEmpty()) {
        qCWarning(lcAutotile) << "toggleFocusedWindowFloat: no focused window on screen" << screenName;
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"),
                                           QStringLiteral("no_focused_window"),
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
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"),
                                           QStringLiteral("no_screen"),
                                           QString(), QString(), QString());
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
                qCInfo(lcAutotile) << "toggleWindowFloat: window" << windowId
                                   << "found on screen" << resolvedScreen
                                   << "(caller reported" << screenName << ")";
                break;
            }
        }
    }

    if (!state) {
        qCWarning(lcAutotile) << "toggleWindowFloat: window" << windowId
                               << "not found in any autotile state";
        Q_EMIT navigationFeedbackRequested(false, QStringLiteral("float"),
                                           QStringLiteral("window_not_tracked"),
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
    qCInfo(lcAutotile) << "Window" << windowId << (isNowFloating ? "now floating" : "now tiled")
                       << "on screen" << screenName;
    Q_EMIT windowFloatingChanged(windowId, isNowFloating, screenName);
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
    m_overflow.clearOverflow(windowId); // Now user-floated, not overflow
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
    m_overflow.clearOverflow(windowId); // No longer overflow
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
            qCInfo(lcAutotile) << "Window" << windowId << "moved from" << oldScreen
                               << "to" << screenName << "- migrating";
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
        qCDebug(lcAutotile) << "Max window limit reached for screen" << screenName
                            << "(max=" << maxWin << ")";
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
            qCDebug(lcAutotile) << "Inserted pre-seeded window" << windowId
                                << "at position" << insertAt << "(desired:" << desiredPos << ")";
        }
        // Clean up pending order when all pre-seeded windows have been inserted (or closed)
        if (inserted) {
            bool allResolved = true;
            for (const QString& pendingWin : pendingOrder) {
                if (pendingWin != windowId && !state->containsWindow(pendingWin)) {
                    allResolved = false;
                    break;
                }
            }
            if (allResolved) {
                m_pendingInitialOrders.remove(screenName);
                qCDebug(lcAutotile) << "All pre-seeded windows resolved for screen" << screenName;
            }
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

    // Restore floating state if this window was floating before autotile was deactivated
    if (m_savedFloatingWindows.remove(windowId)) {
        state->setFloating(windowId, true);
        qCInfo(lcAutotile) << "Restored floating state for window" << windowId << "on screen" << screenName;
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
    for (auto pit = m_pendingInitialOrders.begin(); pit != m_pendingInitialOrders.end(); ) {
        QStringList& pendingList = pit.value();
        pendingList.removeAll(windowId);
        if (pendingList.isEmpty()) {
            pit = m_pendingInitialOrders.erase(pit);
        } else {
            // Check if remaining pending windows are all resolved (in state or removed)
            TilingState* pendingState = m_screenStates.value(pit.key());
            bool allResolved = true;
            if (pendingState) {
                for (const QString& pendingWin : std::as_const(pendingList)) {
                    if (!pendingState->containsWindow(pendingWin)) {
                        allResolved = false;
                        break;
                    }
                }
            }
            if (allResolved && pendingState) {
                qCDebug(lcAutotile) << "Pending order resolved after window removal for screen" << pit.key();
                pit = m_pendingInitialOrders.erase(pit);
            } else {
                ++pit;
            }
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
    if (effectiveRespectMinimumSize(screenName) && !minSizes.isEmpty()
        && algoId != DBus::AutotileAlgorithm::Monocle) {
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
        qCWarning(lcAutotile) << "AutotileEngine::applyTiling: zone count exceeds window count" << windows.size() << "vs"
                   << zones.size();
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
    if (!oldAlgo || !newAlgo) return;
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
                    [bfState](const QString& wid) { return bfState->isFloating(wid); },
                    [bfState](const QString& wid) { return bfState->containsWindow(wid); });
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
            if (it.value() == screenName && !state->containsWindow(it.key())
                && shouldTileWindow(it.key())) {
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
            [state](const QString& wid) { return state->isFloating(wid); },
            [state](const QString& wid) { return state->containsWindow(wid); });
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

} // namespace PlasmaZones
