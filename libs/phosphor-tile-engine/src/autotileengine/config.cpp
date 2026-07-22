// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Qt headers
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <QVarLengthArray>

// Project headers
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorGeometry/GeometryUtils.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTileEngine/NavigationController.h>
#include <PhosphorTileEngine/PerScreenConfigResolver.h>
#include <PhosphorTiles/AlgorithmPreviewParams.h>
#include <PhosphorTiles/TilingAlgorithm.h>
// DwindleMemoryAlgorithm.h no longer needed — prepareTilingState() is virtual on PhosphorTiles::TilingAlgorithm
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "tileenginelogging.h"
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "engine_internal.h"

namespace PhosphorTileEngine {

namespace {
// Safety timeout for pending initial window orders that never arrive via D-Bus.
// If windows fail to open (e.g., app crash during startup), this prevents
// m_pendingInitialOrders from leaking state indefinitely.
constexpr int PendingOrderTimeoutMs = 10000;

// Filter a per-algorithm settings map down to the entries worth persisting: those
// that actually deviate from the algorithm's own defaults. A slot that merely
// echoes the defaults carries no user intent, so writing it would surface as a
// spurious "you changed this" row in the config profile diff. Both the
// save-before-switch block and the no-slot fallback in setAlgorithm() can leave
// such default-valued slots in the live map; filtering at write-back is the single
// choke point that keeps them off disk. Entries whose algorithm is unknown to the
// registry (e.g. an uninstalled scripted algorithm) are kept verbatim — they
// cannot be compared to defaults, and dropping them would lose the user's tuning.
QHash<QString, AlgorithmSettings> persistablePerAlgoSettings(const QHash<QString, AlgorithmSettings>& saved,
                                                             PhosphorTiles::ITileAlgorithmRegistry* registry)
{
    QHash<QString, AlgorithmSettings> result;
    for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) {
        auto* algo = registry ? registry->algorithm(it.key()) : nullptr;
        const bool matchesDefaults = algo && qFuzzyCompare(1.0 + it->splitRatio, 1.0 + algo->defaultSplitRatio())
            && it->masterCount == PhosphorTiles::AutotileDefaults::DefaultMasterCount
            && it->maxWindows == algo->defaultMaxWindows() && it->customParams.isEmpty();
        if (!matchesDefaults) {
            result.insert(it.key(), it.value());
        }
    }
    return result;
}
} // namespace

AutotileConfig* AutotileEngine::config() const noexcept
{
    return m_config.get();
}

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Zone-ordered window transitions
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::setInitialWindowOrder(const QString& screenId, const QStringList& rawWindowIds)
{
    if (rawWindowIds.isEmpty()) {
        return;
    }
    // Canonicalize every id up front so the pending order is consistent with
    // the keys used by PhosphorTiles::TilingState when windowOpened() arrives next.
    QStringList windowIds;
    windowIds.reserve(rawWindowIds.size());
    for (const QString& raw : rawWindowIds) {
        windowIds.append(canonicalizeWindowId(raw));
    }
    // Only take effect when the screen's PhosphorTiles::TilingState is empty (no prior windows —
    // including floating — from session restore). Uses windowCount() instead of
    // tiledWindows() to also detect floating-only states.
    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    if (state && state->windowCount() > 0) {
        qCDebug(PhosphorTileEngine::lcTileEngine) << "setInitialWindowOrder: screen" << screenId << "already has"
                                                  << state->windowCount() << "windows, ignoring pre-seeded order";
        return;
    }
    // Warn (but allow) if overwriting a pending order that hasn't been fully consumed
    if (m_pendingInitialOrders.contains(screenId)) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "setInitialWindowOrder: overwriting existing pending order for" << screenId;
    }
    m_pendingInitialOrders[screenId] = windowIds;
    // Mode-transition seeding is strict: the daemon explicitly computed an
    // order it wants preserved (zone order from the previous mode). Even if
    // windows arrive in a different sequence, the saved positions win.
    m_strictInitialOrderScreens.insert(screenId);
    uint64_t gen = ++m_pendingOrderGeneration[screenId];
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Pre-seeded window order for screen=" << screenId << "windows=" << windowIds;

    // Safety timeout: clean up if windows never arrive (e.g., app crash during startup).
    // Use a generation counter so that stale timers from overwritten calls become no-ops.
    QTimer::singleShot(PendingOrderTimeoutMs, this, [this, screenId, gen]() {
        if (m_pendingOrderGeneration.value(screenId) != gen) {
            return; // superseded by a newer setInitialWindowOrder call
        }
        if (m_pendingInitialOrders.remove(screenId)) {
            m_pendingOrderGeneration.remove(screenId);
            m_strictInitialOrderScreens.remove(screenId);
            qCWarning(PhosphorTileEngine::lcTileEngine)
                << "Pending initial order for screen" << screenId << "timed out after" << PendingOrderTimeoutMs
                << "ms - cleaning up stale entry";
        }
    });
}

QStringList AutotileEngine::tiledWindowOrder(const QString& screenId) const
{
    const TilingStateKey key = currentKeyForScreen(screenId);
    PhosphorTiles::TilingState* state = m_states.stateForKey(key);
    if (!state) {
        return {};
    }
    return state->tiledWindows();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Settings synchronization
// ═══════════════════════════════════════════════════════════════════════════════

PhosphorEngine::IAutotileSettings* AutotileEngine::autotileSettings() const
{
    return qobject_cast<PhosphorEngine::IAutotileSettings*>(engineSettings());
}

void AutotileEngine::writeBackTuning()
{
    auto* settings = engineSettings();
    if (!settings) {
        return;
    }
    const QSignalBlocker blocker(settings);
    if (auto* s = autotileSettings()) {
        s->setAutotileSplitRatio(m_config->splitRatio);
        s->setAutotileMasterCount(m_config->masterCount);
        s->setAutotilePerAlgorithmSettings(AutotileConfig::perAlgoToVariantMap(
            persistablePerAlgoSettings(m_config->savedAlgorithmSettings, m_algorithmRegistry)));
    }
}

void AutotileEngine::refreshConfigFromSettings()
{
    auto* s = autotileSettings();
    if (!s) {
        return;
    }

    bool configChanged = false;
    const int oldMaxWindows = m_config->maxWindows;
    const auto oldOverflow = m_config->overflowBehavior;
    // Set when an explicit global change below drops every per-key user-tuned
    // flag. The propagates at the end of this method then have to span every
    // state, matching that clear — see PropagateScope.
    bool masterCountDroppedTunings = false;
    bool splitRatioDroppedTunings = false;

#define SYNC_FIELD(field, getter)                                                                                      \
    do {                                                                                                               \
        auto newVal = s->getter();                                                                                     \
        if (m_config->field != newVal) {                                                                               \
            m_config->field = newVal;                                                                                  \
            configChanged = true;                                                                                      \
        }                                                                                                              \
    } while (0)

    if (!m_writeBackGuardTimer.isActive()) {
        const int newMasterCount = s->autotileMasterCount();
        if (m_config->masterCount != newMasterCount) {
            m_config->masterCount = newMasterCount;
            configChanged = true;
            // An explicit global master-count change (settings) overrides any
            // per-desktop tuning, on every desktop and activity — the same
            // meaning setGlobalMasterCount gives the D-Bus setter. This clear
            // spans every key, so the propagate below is widened to match it.
            m_userTunedMasterCount.clear();
            masterCountDroppedTunings = true;
        }
    }
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

    // maxWindows is per-algorithm data. The global key is only a fallback for
    // an algorithm with no saved slot yet (and the landing point for an explicit
    // external write via the D-Bus settings property); the per-algorithm restore
    // below overrides it whenever a slot exists. Skip the re-read while our own
    // write-back is in flight, matching the splitRatio/masterCount guards.
    if (!m_writeBackGuardTimer.isActive()) {
        SYNC_FIELD(maxWindows, autotileMaxWindows);
    }

    if (!m_writeBackGuardTimer.isActive()) {
        const qreal newRatio = s->autotileSplitRatio();
        if (!qFuzzyCompare(1.0 + m_config->splitRatio, 1.0 + newRatio)) {
            m_config->splitRatio = newRatio;
            configChanged = true;
            // An explicit global split-ratio change (settings) overrides any
            // per-desktop tuning, on every desktop and activity — the same
            // meaning setGlobalSplitRatio gives the D-Bus setter. This clear
            // spans every key, so the propagate below is widened to match it.
            m_userTunedSplitRatio.clear();
            splitRatioDroppedTunings = true;
        }
    }
    {
        const qreal newStep = s->autotileSplitRatioStep();
        if (!qFuzzyCompare(1.0 + m_config->splitRatioStep, 1.0 + newStep)) {
            m_config->splitRatioStep = newStep;
        }
    }

    {
        const auto newInsert = static_cast<AutotileConfig::InsertPosition>(s->autotileInsertPosition());
        if (m_config->insertPosition != newInsert) {
            m_config->insertPosition = newInsert;
            configChanged = true;
        }
    }

    {
        const auto newOverflow = s->autotileOverflowBehavior();
        if (m_config->overflowBehavior != newOverflow) {
            m_config->overflowBehavior = newOverflow;
            configChanged = true;
        }
    }

    {
        const auto newSaved = AutotileConfig::perAlgoFromVariantMap(s->autotilePerAlgorithmSettings());
        if (m_config->savedAlgorithmSettings != newSaved) {
            m_config->savedAlgorithmSettings = newSaved;
            configChanged = true;
        }
    }

#undef SYNC_FIELD

    const QString oldAlgorithmId = m_algorithmId;
    setAlgorithm(s->defaultAutotileAlgorithm());
    if (m_algorithmId != oldAlgorithmId) {
        configChanged = true;
    }

    if (m_algorithmId == oldAlgorithmId) {
        auto savedIt = m_config->savedAlgorithmSettings.constFind(m_algorithmId);
        if (savedIt != m_config->savedAlgorithmSettings.constEnd()) {
            m_config->splitRatio = savedIt->splitRatio;
            m_config->masterCount = savedIt->masterCount;
            m_config->maxWindows = savedIt->maxWindows;
        } else if (auto* algo = m_algorithmRegistry ? m_algorithmRegistry->algorithm(m_algorithmId) : nullptr) {
            // No saved slot for the current algorithm. The global maxWindows key
            // overrides the algorithm's own default ONLY when it has been set to a
            // non-default value (e.g. explicitly via D-Bus). While it still holds
            // the schema default it is ambient, not an override, so the algorithm's
            // own default is authoritative — otherwise switching to an algorithm
            // whose default differs from the generic global (e.g. grid at 9) would
            // silently clamp it to the global default on the next routine refresh.
            // Default-valued slots are no longer persisted (see
            // persistablePerAlgoSettings), so this on-demand fallback is what keeps
            // an untouched algorithm at its intended cap. splitRatio/masterCount are
            // left as the SYNC'd global values — those globals are real user
            // settings, unlike the legacy per-algorithm maxWindows global.
            if (s->autotileMaxWindows() == PhosphorTiles::AutotileDefaults::DefaultMaxWindows) {
                m_config->maxWindows = algo->defaultMaxWindows();
            }
        }
    }

    propagateGlobalSplitRatio(splitRatioDroppedTunings ? PropagateScope::AllContexts : PropagateScope::CurrentContext);
    propagateGlobalMasterCount(masterCountDroppedTunings ? PropagateScope::AllContexts
                                                         : PropagateScope::CurrentContext);

    // Float→Unlimited: backfill previously-overflowed floating windows
    const bool overflowBackfilled = oldOverflow == PhosphorTiles::AutotileOverflowBehavior::Float
        && m_config->overflowBehavior == PhosphorTiles::AutotileOverflowBehavior::Unlimited;
    if (overflowBackfilled) {
        backfillWindows();
    }

    if (m_config->maxWindows > oldMaxWindows && !overflowBackfilled) {
        backfillWindows();
    }

    // Update preview params so algorithm previews in the KCM reflect current values
    PhosphorTiles::AlgorithmPreviewParams previewParams;
    previewParams.algorithmId = m_algorithmId;
    previewParams.maxWindows = m_config->maxWindows;
    previewParams.masterCount = m_config->masterCount;
    previewParams.splitRatio = m_config->splitRatio;
    // Reuse the canonical serializer so the per-algorithm preview entries can't
    // drift from the on-disk form (every saved field, incl. maxWindows, in one
    // place).
    const QVariantMap serialized = AutotileConfig::perAlgoToVariantMap(m_config->savedAlgorithmSettings);
    for (auto it = serialized.constBegin(); it != serialized.constEnd(); ++it) {
        previewParams.savedAlgorithmSettings.insert(it.key(), it.value().toMap());
    }
    if (auto* reg = algorithmRegistry()) {
        reg->setPreviewParams(previewParams);
    }

    if (configChanged && isEnabled()) {
        m_settingsRetileTimer.start();
    }

    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "Settings: synced, algorithm=" << m_algorithmId << "autotileScreens=" << m_autotileScreens.size();
}

void AutotileEngine::applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides)
{
    m_configResolver->applyPerScreenConfig(screenId, overrides);
}

void AutotileEngine::clearPerScreenConfig(const QString& screenId)
{
    m_configResolver->clearPerScreenConfig(screenId);
}

void AutotileEngine::setContextGapProvider(std::function<QVariantMap(const QString& screenId)> provider)
{
    m_configResolver->setContextGapProvider(std::move(provider));
}

QVariantMap AutotileEngine::perScreenOverrides(const QString& screenId) const
{
    return m_configResolver->perScreenOverrides(screenId);
}

bool AutotileEngine::hasPerScreenOverride(const QString& screenId, const QString& key) const
{
    return m_configResolver->hasPerScreenOverride(screenId, key);
}

void AutotileEngine::updatePerScreenOverride(const QString& screenId, const QString& key, const QVariant& value)
{
    m_configResolver->updatePerScreenOverride(screenId, key, value);
}

void AutotileEngine::noteSplitRatioUserTuned(const QString& screenId)
{
    m_userTunedSplitRatio.insert(currentKeyForScreen(screenId));
}

void AutotileEngine::noteMasterCountUserTuned(const QString& screenId)
{
    m_userTunedMasterCount.insert(currentKeyForScreen(screenId));
}

int AutotileEngine::effectiveInnerGap(const QString& screenId) const
{
    return m_configResolver->effectiveInnerGap(screenId);
}

::PhosphorLayout::EdgeGaps AutotileEngine::effectiveOuterGaps(const QString& screenId) const
{
    return m_configResolver->effectiveOuterGaps(screenId);
}

bool AutotileEngine::effectiveSmartGaps(const QString& screenId) const
{
    return m_configResolver->effectiveSmartGaps(screenId);
}

bool AutotileEngine::effectiveRespectMinimumSize(const QString& screenId) const
{
    return m_configResolver->effectiveRespectMinimumSize(screenId);
}

int AutotileEngine::effectiveMaxWindows(const QString& screenId) const
{
    return m_configResolver->effectiveMaxWindows(screenId);
}

PhosphorTiles::AutotileInsertPosition AutotileEngine::effectiveInsertPosition(const QString& screenId) const
{
    return m_configResolver->effectiveInsertPosition(screenId);
}

qreal AutotileEngine::effectiveSplitRatioStep(const QString& screenId) const
{
    return m_configResolver->effectiveSplitRatioStep(screenId);
}

int AutotileEngine::runtimeMaxWindows() const
{
    return m_config->maxWindows;
}

QString AutotileEngine::effectiveAlgorithmId(const QString& screenId) const
{
    return m_configResolver->effectiveAlgorithmId(screenId);
}

PhosphorTiles::TilingAlgorithm* AutotileEngine::effectiveAlgorithm(const QString& screenId) const
{
    return m_configResolver->effectiveAlgorithm(screenId);
}

} // namespace PhosphorTileEngine
