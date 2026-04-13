// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PerScreenConfigResolver.h"
#include "AutotileEngine.h"
#include "AlgorithmRegistry.h"
#include "AutotileConfig.h"
#include "TilingAlgorithm.h"
#include "TilingState.h"
#include "core/constants.h"
#include "core/logging.h"

#include <limits>

namespace PlasmaZones {

PerScreenConfigResolver::PerScreenConfigResolver(AutotileEngine* engine)
    : m_engine(engine)
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-screen override storage
// ═══════════════════════════════════════════════════════════════════════════════

void PerScreenConfigResolver::applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides)
{
    if (screenId.isEmpty()) {
        return;
    }

    if (overrides.isEmpty()) {
        clearPerScreenConfig(screenId);
        return;
    }

    // Store overrides so effective*() helpers and connectToSettings handlers
    // can resolve per-screen values and skip screens with overrides.
    m_perScreenOverrides[screenId] = overrides;

    TilingState* state = m_engine->stateForScreen(screenId);
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
    if (m_engine->isAutotileScreen(screenId)) {
        m_engine->scheduleRetileForScreen(screenId);
    }

    qCDebug(lcAutotile) << "Applied per-screen config for" << screenId << "keys:" << overrides.keys();
}

void PerScreenConfigResolver::clearPerScreenConfig(const QString& screenId)
{
    if (!m_perScreenOverrides.remove(screenId)) {
        return;
    }
    // Restore global defaults on TilingState
    TilingState* state = m_engine->stateForScreen(screenId);
    if (state) {
        state->setSplitRatio(m_engine->config()->splitRatio);
        state->setMasterCount(m_engine->config()->masterCount);
    }

    // Schedule deferred retile (same rationale as applyPerScreenConfig)
    if (m_engine->isAutotileScreen(screenId)) {
        m_engine->scheduleRetileForScreen(screenId);
    }

    qCDebug(lcAutotile) << "Cleared per-screen config for" << screenId;
}

QVariantMap PerScreenConfigResolver::perScreenOverrides(const QString& screenId) const
{
    return m_perScreenOverrides.value(screenId);
}

bool PerScreenConfigResolver::hasPerScreenOverride(const QString& screenId, const QString& key) const
{
    auto it = m_perScreenOverrides.constFind(screenId);
    return it != m_perScreenOverrides.constEnd() && it->contains(key);
}

void PerScreenConfigResolver::updatePerScreenOverride(const QString& screenId, const QString& key,
                                                      const QVariant& value)
{
    auto it = m_perScreenOverrides.find(screenId);
    if (it == m_perScreenOverrides.end()) {
        qCWarning(lcAutotile) << "updatePerScreenOverride: no override map for screen" << screenId
                              << "- cannot update key" << key;
        return;
    }
    (*it)[key] = value;
}

void PerScreenConfigResolver::removeOverridesForScreen(const QString& screenId)
{
    m_perScreenOverrides.remove(screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Effective per-screen values
// ═══════════════════════════════════════════════════════════════════════════════

std::optional<QVariant> PerScreenConfigResolver::perScreenOverride(const QString& screenId, const QString& key) const
{
    auto it = m_perScreenOverrides.constFind(screenId);
    if (it != m_perScreenOverrides.constEnd()) {
        auto git = it->constFind(key);
        if (git != it->constEnd()) {
            return *git;
        }
    }
    return std::nullopt;
}

int PerScreenConfigResolver::effectiveInnerGap(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, QStringLiteral("InnerGap")))
        return qBound(AutotileDefaults::MinGap, v->toInt(), AutotileDefaults::MaxGap);
    return m_engine->config()->innerGap;
}

int PerScreenConfigResolver::effectiveOuterGap(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, QStringLiteral("OuterGap")))
        return qBound(AutotileDefaults::MinGap, v->toInt(), AutotileDefaults::MaxGap);
    return m_engine->config()->outerGap;
}

EdgeGaps PerScreenConfigResolver::effectiveOuterGaps(const QString& screenId) const
{
    // Check per-screen per-side overrides first
    auto topOv = perScreenOverride(screenId, QStringLiteral("OuterGapTop"));
    auto bottomOv = perScreenOverride(screenId, QStringLiteral("OuterGapBottom"));
    auto leftOv = perScreenOverride(screenId, QStringLiteral("OuterGapLeft"));
    auto rightOv = perScreenOverride(screenId, QStringLiteral("OuterGapRight"));

    // If any per-screen per-side override exists, build from those
    if (topOv || bottomOv || leftOv || rightOv) {
        // Use per-screen uniform gap as base, then per-side overrides on top
        const int base = effectiveOuterGap(screenId);
        return EdgeGaps{topOv ? qBound(AutotileDefaults::MinGap, topOv->toInt(), AutotileDefaults::MaxGap) : base,
                        bottomOv ? qBound(AutotileDefaults::MinGap, bottomOv->toInt(), AutotileDefaults::MaxGap) : base,
                        leftOv ? qBound(AutotileDefaults::MinGap, leftOv->toInt(), AutotileDefaults::MaxGap) : base,
                        rightOv ? qBound(AutotileDefaults::MinGap, rightOv->toInt(), AutotileDefaults::MaxGap) : base};
    }

    // Check per-screen uniform outer gap
    if (auto v = perScreenOverride(screenId, QStringLiteral("OuterGap"))) {
        const int gap = qBound(AutotileDefaults::MinGap, v->toInt(), AutotileDefaults::MaxGap);
        return EdgeGaps::uniform(gap);
    }

    // Fall back to global config
    const AutotileConfig* cfg = m_engine->config();
    if (cfg->usePerSideOuterGap) {
        return EdgeGaps{cfg->outerGapTop, cfg->outerGapBottom, cfg->outerGapLeft, cfg->outerGapRight};
    }
    return EdgeGaps::uniform(cfg->outerGap);
}

bool PerScreenConfigResolver::effectiveSmartGaps(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, QStringLiteral("SmartGaps")))
        return v->toBool();
    return m_engine->config()->smartGaps;
}

bool PerScreenConfigResolver::effectiveRespectMinimumSize(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, QStringLiteral("RespectMinimumSize")))
        return v->toBool();
    return m_engine->config()->respectMinimumSize;
}

int PerScreenConfigResolver::effectiveMaxWindows(const QString& screenId) const
{
    // Global Unlimited short-circuit: Krohnkite-style "no cap" mode bypasses
    // both the per-screen override and the algorithm-default lookup. A huge
    // sentinel (not INT_MAX, to avoid overflow if anything adds to it) is
    // passed to std::min in recalculateLayout, making the clamp idempotent.
    // Also opens onWindowAdded's gate (tiledWindowCount >= maxWin never true).
    if (m_engine->config()->overflowBehavior == AutotileOverflowBehavior::Unlimited) {
        return std::numeric_limits<int>::max() / 2;
    }

    // 1. Explicit per-screen MaxWindows override — highest priority
    if (auto v = perScreenOverride(screenId, PerScreenKeys::MaxWindows))
        return qBound(AutotileDefaults::MinMaxWindows, v->toInt(), AutotileDefaults::MaxMaxWindows);

    // 2. When the per-screen algorithm differs from the global algorithm,
    //    the global m_config->maxWindows may be for the WRONG algorithm.
    //    E.g. global=master-stack(maxWindows=4) but per-screen=bsp(default=5).
    //    Use the per-screen algorithm's default — but only if the user hasn't
    //    explicitly customized global maxWindows away from the global algo's default.
    const QString screenAlgo = effectiveAlgorithmId(screenId);
    if (screenAlgo != m_engine->m_algorithmId) {
        auto* registry = AlgorithmRegistry::instance();
        auto* screenAlgoPtr = registry->algorithm(screenAlgo);
        auto* globalAlgoPtr = registry->algorithm(m_engine->m_algorithmId);
        if (screenAlgoPtr) {
            // Only override with per-screen default if global is still at its algo's default
            if (!globalAlgoPtr || m_engine->config()->maxWindows == globalAlgoPtr->defaultMaxWindows()) {
                return screenAlgoPtr->defaultMaxWindows();
            }
            // User explicitly customized global maxWindows — honor it
            return m_engine->config()->maxWindows;
        }
        qCWarning(lcAutotile) << "effectiveMaxWindows: unknown per-screen algorithm" << screenAlgo << "for screen"
                              << screenId << "- falling back to global maxWindows";
    }

    // 3. Same algorithm globally and per-screen — use the global setting
    return m_engine->config()->maxWindows;
}

qreal PerScreenConfigResolver::effectiveSplitRatioStep(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, PerScreenKeys::SplitRatioStep))
        return qBound(ConfigDefaults::autotileSplitRatioStepMin(), v->toDouble(),
                      ConfigDefaults::autotileSplitRatioStepMax());
    return m_engine->config()->splitRatioStep;
}

QString PerScreenConfigResolver::effectiveAlgorithmId(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, PerScreenKeys::Algorithm))
        return v->toString();
    return m_engine->m_algorithmId;
}

TilingAlgorithm* PerScreenConfigResolver::effectiveAlgorithm(const QString& screenId) const
{
    return AlgorithmRegistry::instance()->algorithm(effectiveAlgorithmId(screenId));
}

} // namespace PlasmaZones
