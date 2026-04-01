// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsBridge.h"
#include "AutotileEngine.h"
#include "AlgorithmRegistry.h"
#include "AutotileConfig.h"
#include "TilingState.h"
#include "config/settings.h"
#include "core/constants.h"
#include "core/logging.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalBlocker>
#include "config/configbackend_qsettings.h"

namespace PlasmaZones {

namespace {
/// DRY helper: populate PreviewParams::savedAlgorithmSettings from AutotileConfig
void populatePreviewSavedSettings(AlgorithmRegistry::PreviewParams& params,
                                  const QHash<QString, AlgorithmSettings>& savedSettings)
{
    for (auto it = savedSettings.constBegin(); it != savedSettings.constEnd(); ++it) {
        QVariantMap entry{
            {PerAlgoKeys::MasterCount, it.value().masterCount},
            {PerAlgoKeys::SplitRatio, it.value().splitRatio},
        };
        if (!it.value().customParams.isEmpty()) {
            entry[PerAlgoKeys::CustomParams] = it.value().customParams;
        }
        params.savedAlgorithmSettings[it.key()] = entry;
    }
}
} // anonymous namespace

SettingsBridge::SettingsBridge(AutotileEngine* engine, QSettingsConfigBackend* configBackend)
    : m_engine(engine)
    , m_configBackend(configBackend)
{
    // Configure settings retile debounce timer
    // Coalesces rapid settings changes (e.g., slider adjustments) into single retile
    m_settingsRetileTimer.setSingleShot(true);
    m_settingsRetileTimer.setInterval(100); // 100ms debounce
    QObject::connect(&m_settingsRetileTimer, &QTimer::timeout, m_engine, [this]() {
        processSettingsRetile();
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// Settings synchronization
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsBridge::syncFromSettings(Settings* settings)
{
    if (!settings) {
        return;
    }

    m_settings = settings;

    // Track whether any config field actually changed. If individual signal
    // handlers (from runtime setters) already updated config, this detects
    // no changes and skips the redundant retile at the end.
    bool configChanged = false;

    AutotileConfig* cfg = m_engine->config();

    // Capture old maxWindows before updating — used for backfill below
    const int oldMaxWindows = cfg->maxWindows;

    // Apply all settings to config, tracking changes.
    // Note: algorithmId is excluded — it is synced to m_algorithmId (the
    // authoritative engine field) separately below with validation.
    // splitRatio uses qFuzzyCompare for floating-point safety.
#define SYNC_FIELD(field, getter)                                                                                      \
    do {                                                                                                               \
        auto newVal = settings->getter();                                                                              \
        if (cfg->field != newVal) {                                                                                    \
            cfg->field = newVal;                                                                                       \
            configChanged = true;                                                                                      \
        }                                                                                                              \
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
        if (!qFuzzyCompare(1.0 + cfg->splitRatio, 1.0 + newRatio)) {
            cfg->splitRatio = newRatio;
            configChanged = true;
        }
    }
    // Sync per-algorithm settings map from Settings
    {
        const auto newSaved = AutotileConfig::perAlgoFromVariantMap(settings->autotilePerAlgorithmSettings());
        if (cfg->savedAlgorithmSettings != newSaved) {
            cfg->savedAlgorithmSettings = newSaved;
            configChanged = true;
        }
    }

    // InsertPosition needs a cast
    {
        auto newInsert = static_cast<AutotileConfig::InsertPosition>(settings->autotileInsertPositionInt());
        if (cfg->insertPosition != newInsert) {
            cfg->insertPosition = newInsert;
            configChanged = true;
        }
    }

#undef SYNC_FIELD

    // Sync algorithm via setAlgorithm (handles validation + fallback + m_config sync)
    const QString oldAlgorithmId = m_engine->m_algorithmId;
    m_engine->setAlgorithm(settings->defaultAutotileAlgorithm());
    if (m_engine->m_algorithmId != oldAlgorithmId) {
        configChanged = true;
    }

    // When the active algorithm is in the saved map and setAlgorithm() early-returned
    // (no algorithm change), restore the saved values for the active algorithm so
    // the generic SYNC_FIELD splitRatio doesn't clobber the per-algorithm value.
    if (m_engine->m_algorithmId == oldAlgorithmId) {
        auto savedIt = cfg->savedAlgorithmSettings.constFind(m_engine->m_algorithmId);
        if (savedIt != cfg->savedAlgorithmSettings.constEnd()) {
            cfg->splitRatio = savedIt->splitRatio;
            cfg->masterCount = savedIt->masterCount;
        }
    }

    // Propagate split ratio and master count to screens WITHOUT per-screen overrides.
    // Screens with per-screen overrides are handled by updateAutotileScreens()
    // (called by the daemon after syncFromSettings returns).
    m_engine->propagateGlobalSplitRatio();
    m_engine->propagateGlobalMasterCount();

    // Backfill windows when maxWindows increased: windows rejected by the old
    // gate check in onWindowAdded() stay untiled unless we add them here.
    if (cfg->maxWindows > oldMaxWindows) {
        m_engine->backfillWindows();
    }

    // Update AlgorithmRegistry so preview generation uses the configured values.
    // Per-algorithm settings are stored in the savedAlgorithmSettings map so
    // generatePreviewZones() can look up any algorithm's saved params generically.
    AlgorithmRegistry::PreviewParams previewParams;
    previewParams.algorithmId = m_engine->m_algorithmId;
    previewParams.maxWindows = cfg->maxWindows;
    previewParams.masterCount = cfg->masterCount;
    previewParams.splitRatio = cfg->splitRatio;
    populatePreviewSavedSettings(previewParams, cfg->savedAlgorithmSettings);
    AlgorithmRegistry::setConfiguredPreviewParams(previewParams);

    if (configChanged && m_engine->isEnabled()) {
        // Cancel any pending debounced retile — we are doing a full resync
        m_settingsRetileTimer.stop();
        m_pendingSettingsRetile = false;
        // Cancel deferred retiles from setAlgorithm() — the immediate retile()
        // below covers all screens. Without this, the deferred retile fires on
        // the next event loop pass and emits a redundant windowsTiled D-Bus
        // signal with identical geometry data.
        m_engine->m_pendingRetileScreens.clear();
        m_engine->retile();
    }

    qCInfo(lcAutotile) << "Settings: synced, algorithm=" << m_engine->m_algorithmId
                       << "autotileScreens=" << m_engine->m_autotileScreens.size();
}

void SettingsBridge::connectToSettings(Settings* settings)
{
    if (!settings) {
        return;
    }

    // Disconnect from previous settings if any (handles the case where
    // syncFromSettings was called first, which sets m_settings)
    if (m_settings) {
        // SettingsBridge owns all m_settings → m_engine connections; disconnect all of them.
        QObject::disconnect(m_settings, nullptr, m_engine, nullptr);
        qCDebug(lcAutotile) << "Disconnected from previous settings";
    }
    // Also disconnect from the new settings object in case of repeated calls with the same pointer
    QObject::disconnect(settings, nullptr, m_engine, nullptr);

    m_settings = settings;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Macros for settings connections
    // ═══════════════════════════════════════════════════════════════════════════════

    // Pattern 1: Update config field + schedule retile
    // These handlers fire from runtime setters only (not from load() — load()
    // only emits settingsChanged, which triggers syncFromSettings).
#define CONNECT_SETTING_RETILE(signal, field, getter)                                                                  \
    QObject::connect(settings, &Settings::signal, m_engine, [this]() {                                                 \
        if (!m_settings)                                                                                               \
            return;                                                                                                    \
        m_engine->config()->field = m_settings->getter();                                                              \
        scheduleSettingsRetile();                                                                                      \
    })

    // Pattern 2: Update config field only (no retile)
#define CONNECT_SETTING_NO_RETILE(signal, field, getter)                                                               \
    QObject::connect(settings, &Settings::signal, m_engine, [this]() {                                                 \
        if (!m_settings)                                                                                               \
            return;                                                                                                    \
        m_engine->config()->field = m_settings->getter();                                                              \
    })

    // ═══════════════════════════════════════════════════════════════════════════════
    // Immediate-effect settings (no debounce)
    // ═══════════════════════════════════════════════════════════════════════════════

    // Note: autotileEnabledChanged is NOT connected here. The KCM checkbox acts
    // as a feature gate — engine enabled state is driven by layout selection
    // (applyEntry) and mode toggle in the daemon.

    QObject::connect(settings, &Settings::defaultAutotileAlgorithmChanged, m_engine, [this]() {
        if (!m_settings)
            return;
        m_engine->setAlgorithm(m_settings->defaultAutotileAlgorithm());
    });

    // ═══════════════════════════════════════════════════════════════════════════════
    // Settings that require retile (debounced)
    // ═══════════════════════════════════════════════════════════════════════════════

    QObject::connect(settings, &Settings::autotileSplitRatioChanged, m_engine, [this]() {
        if (!m_settings)
            return;
        m_engine->config()->splitRatio = m_settings->autotileSplitRatio();
        m_engine->propagateGlobalSplitRatio();
        scheduleSettingsRetile();
    });

    QObject::connect(settings, &Settings::autotileMasterCountChanged, m_engine, [this]() {
        if (!m_settings)
            return;
        m_engine->config()->masterCount = m_settings->autotileMasterCount();
        m_engine->propagateGlobalMasterCount();
        scheduleSettingsRetile();
    });

    QObject::connect(settings, &Settings::autotilePerAlgorithmSettingsChanged, m_engine, [this]() {
        if (!m_settings)
            return;
        auto newSaved = AutotileConfig::perAlgoFromVariantMap(m_settings->autotilePerAlgorithmSettings());
        m_engine->config()->savedAlgorithmSettings = newSaved;
        // If the active algorithm's settings changed, apply them and retile
        auto it = newSaved.constFind(m_engine->m_algorithmId);
        if (it != newSaved.constEnd()) {
            m_engine->config()->splitRatio = it->splitRatio;
            m_engine->config()->masterCount = it->masterCount;
            m_engine->propagateGlobalSplitRatio();
            m_engine->propagateGlobalMasterCount();
            scheduleSettingsRetile();
        }
        // Update AlgorithmRegistry preview params so previews reflect the new values
        AlgorithmRegistry::PreviewParams previewParams;
        previewParams.algorithmId = m_engine->m_algorithmId;
        previewParams.maxWindows = m_engine->config()->maxWindows;
        previewParams.masterCount = m_engine->config()->masterCount;
        previewParams.splitRatio = m_engine->config()->splitRatio;
        populatePreviewSavedSettings(previewParams, newSaved);
        AlgorithmRegistry::setConfiguredPreviewParams(previewParams);
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
    QObject::connect(settings, &Settings::autotileMaxWindowsChanged, m_engine, [this]() {
        if (!m_settings)
            return;
        const int oldMax = m_engine->config()->maxWindows;
        m_engine->config()->maxWindows = m_settings->autotileMaxWindows();

        // When max increases, try to add windows that exist on autotile screens
        // but were rejected when the previous limit was reached.
        if (m_engine->config()->maxWindows > oldMax) {
            m_engine->backfillWindows();
        }
        scheduleSettingsRetile();
    });

    // ═══════════════════════════════════════════════════════════════════════════════
    // Settings that don't require retile (config update only)
    // ═══════════════════════════════════════════════════════════════════════════════

    CONNECT_SETTING_NO_RETILE(autotileFocusNewWindowsChanged, focusNewWindows, autotileFocusNewWindows);
    CONNECT_SETTING_NO_RETILE(autotileFocusFollowsMouseChanged, focusFollowsMouse, autotileFocusFollowsMouse);

    // InsertPosition requires cast
    QObject::connect(settings, &Settings::autotileInsertPositionChanged, m_engine, [this]() {
        if (!m_settings)
            return;
        m_engine->config()->insertPosition =
            static_cast<AutotileConfig::InsertPosition>(m_settings->autotileInsertPositionInt());
    });

#undef CONNECT_SETTING_RETILE
#undef CONNECT_SETTING_NO_RETILE
}

void SettingsBridge::syncAlgorithmToSettings(const QString& algoId, qreal splitRatio, int maxWindows, int oldMaxWindows)
{
    if (!m_settings) {
        return;
    }

    const QSignalBlocker blocker(m_settings);
    if (maxWindows != oldMaxWindows) {
        m_settings->setAutotileMaxWindows(maxWindows);
    }
    m_settings->setDefaultAutotileAlgorithm(algoId);
    m_settings->setAutotileSplitRatio(splitRatio);
    m_settings->setAutotileMasterCount(m_engine->config()->masterCount);
    // Sync per-algorithm map so saved settings survive save/reload
    m_settings->setAutotilePerAlgorithmSettings(
        AutotileConfig::perAlgoToVariantMap(m_engine->config()->savedAlgorithmSettings));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Session persistence
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsBridge::saveState()
{
    // Use shared backend when available (daemon), create temporary otherwise (tests)
    std::unique_ptr<QSettingsConfigBackend> tempBackend;
    if (!m_configBackend) {
        tempBackend = QSettingsConfigBackend::createDefault();
    }
    QSettingsConfigBackend* backend = m_configBackend ? m_configBackend : tempBackend.get();
    auto group = backend->group(QStringLiteral("AutoTileState"));

    // Save global state (algorithm only — autotile screens are derived from
    // layout assignments at startup by updateAutotileScreens(), not persisted
    // here, to avoid stale data overriding the authoritative layout assignments)
    group->writeString(QStringLiteral("algorithm"), m_engine->m_algorithmId);

    // Save per-screen state as JSON array, including desktop/activity key
    QJsonArray screensArray;
    for (auto it = m_engine->m_screenStates.constBegin(); it != m_engine->m_screenStates.constEnd(); ++it) {
        const TilingState* state = it.value();
        if (!state) {
            continue;
        }

        const TilingStateKey& key = it.key();
        QJsonObject screenObj;
        screenObj[QLatin1String("screen")] = key.screenId;
        screenObj[QLatin1String("desktop")] = key.desktop;
        screenObj[QLatin1String("activity")] = key.activity;
        screenObj[QLatin1String("masterCount")] = state->masterCount();
        screenObj[QLatin1String("splitRatio")] = state->splitRatio();

        screensArray.append(screenObj);
    }

    group->writeString(QStringLiteral("screenStates"),
                       QString::fromUtf8(QJsonDocument(screensArray).toJson(QJsonDocument::Compact)));
    group.reset(); // release group before sync
    backend->sync();

    qCInfo(lcAutotile) << "Autotile state: saved," << m_engine->m_screenStates.size() << "states";
}

void SettingsBridge::loadState()
{
    std::unique_ptr<QSettingsConfigBackend> tempBackend;
    if (!m_configBackend) {
        tempBackend = QSettingsConfigBackend::createDefault();
    }
    QSettingsConfigBackend* backend = m_configBackend ? m_configBackend : tempBackend.get();
    auto group = backend->group(QStringLiteral("AutoTileState"));

    if (!group->hasKey(QStringLiteral("algorithm")) && !group->hasKey(QStringLiteral("screenStates"))) {
        qCDebug(lcAutotile) << "No saved autotile state found";
        return;
    }

    // Restore algorithm silently — do NOT emit algorithmChanged here.
    const QString savedAlgorithm = group->readString(QStringLiteral("algorithm"), m_engine->m_algorithmId);
    if (AlgorithmRegistry::instance()->hasAlgorithm(savedAlgorithm)) {
        m_engine->m_algorithmId = savedAlgorithm;
        m_engine->m_config->algorithmId = savedAlgorithm;
    }

    // Parse per-screen state
    const QString statesJson = group->readString(QStringLiteral("screenStates"));
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
    QSet<QString> processedKeys;
    for (const QJsonValue& val : screensArray) {
        const QJsonObject screenObj = val.toObject();
        const QString screenId = screenObj[QLatin1String("screen")].toString();
        if (screenId.isEmpty()) {
            continue;
        }

        // Restore desktop/activity context from saved state (defaults to 0/"" for
        // backward compatibility with pre-per-desktop save format).
        const int desktop = screenObj[QLatin1String("desktop")].toInt(0);
        const QString activity = screenObj[QLatin1String("activity")].toString();

        // Use stateForKey() to create the state under the exact saved key
        // without mutating the engine's current desktop/activity context.
        TilingStateKey loadKey;
        loadKey.screenId = screenId;
        loadKey.desktop = (desktop > 0) ? desktop : m_engine->m_currentDesktop;
        loadKey.activity = !activity.isEmpty() ? activity : m_engine->m_currentActivity;

        // Skip duplicate resolved keys (desktop=0 backward compat can map
        // multiple entries to the same key).
        const QString compositeKey =
            QStringLiteral("%1/%2/%3").arg(loadKey.screenId).arg(loadKey.desktop).arg(loadKey.activity);
        if (processedKeys.contains(compositeKey)) {
            continue;
        }
        processedKeys.insert(compositeKey);

        TilingState* state = m_engine->stateForKey(loadKey);

        if (!state) {
            continue;
        }

        // Restore per-screen parameters (not window order — windows haven't been
        // announced yet and stableIds may not match across sessions)
        state->setMasterCount(screenObj[QLatin1String("masterCount")].toInt(m_engine->config()->masterCount));
        state->setSplitRatio(screenObj[QLatin1String("splitRatio")].toDouble(m_engine->config()->splitRatio));
    }

    // Note: autotile screens are NOT restored here. The authoritative source is
    // layout assignments (persisted by LayoutManager). Daemon::connectLayoutSignals()
    // calls updateAutotileScreens() which derives the correct set from assignments
    // BEFORE this loadState() runs. Restoring a saved set here would overwrite the
    // correct value with stale data (e.g., screens from a previous autotile session
    // that the user has since toggled back to manual mode).

    qCInfo(lcAutotile) << "Autotile state: loaded, algorithm=" << m_engine->m_algorithmId
                       << "screenStates=" << screensArray.size();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Debounce helpers
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsBridge::scheduleSettingsRetile()
{
    m_pendingSettingsRetile = true;
    m_settingsRetileTimer.start();
}

void SettingsBridge::processSettingsRetile()
{
    if (!m_pendingSettingsRetile) {
        return;
    }

    m_pendingSettingsRetile = false;

    // Only retile if autotiling is enabled on any screen
    if (m_engine->isEnabled()) {
        m_engine->retile();
        qCDebug(lcAutotile) << "Settings: changed, retiled windows";
    }
}

} // namespace PlasmaZones
