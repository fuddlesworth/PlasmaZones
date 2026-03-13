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

namespace PlasmaZones {

PerScreenConfigResolver::PerScreenConfigResolver(AutotileEngine* engine)
    : m_engine(engine)
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-screen override storage
// ═══════════════════════════════════════════════════════════════════════════════

void PerScreenConfigResolver::applyPerScreenConfig(const QString& screenName, const QVariantMap& overrides)
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

    TilingState* state = m_engine->stateForScreen(screenName);
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
    if (m_engine->isAutotileScreen(screenName)) {
        m_engine->scheduleRetileForScreen(screenName);
    }

    qCDebug(lcAutotile) << "Applied per-screen config for" << screenName << "keys:" << overrides.keys();
}

void PerScreenConfigResolver::clearPerScreenConfig(const QString& screenName)
{
    if (!m_perScreenOverrides.remove(screenName)) {
        return;
    }
    // Restore global defaults on TilingState
    TilingState* state = m_engine->stateForScreen(screenName);
    if (state) {
        state->setSplitRatio(m_engine->config()->splitRatio);
        state->setMasterCount(m_engine->config()->masterCount);
    }

    // Schedule deferred retile (same rationale as applyPerScreenConfig)
    if (m_engine->isAutotileScreen(screenName)) {
        m_engine->scheduleRetileForScreen(screenName);
    }

    qCDebug(lcAutotile) << "Cleared per-screen config for" << screenName;
}

QVariantMap PerScreenConfigResolver::perScreenOverrides(const QString& screenName) const
{
    return m_perScreenOverrides.value(screenName);
}

bool PerScreenConfigResolver::hasPerScreenOverride(const QString& screenName, const QString& key) const
{
    auto it = m_perScreenOverrides.constFind(screenName);
    return it != m_perScreenOverrides.constEnd() && it->contains(key);
}

void PerScreenConfigResolver::removeOverridesForScreen(const QString& screenName)
{
    m_perScreenOverrides.remove(screenName);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Effective per-screen values
// ═══════════════════════════════════════════════════════════════════════════════

std::optional<QVariant> PerScreenConfigResolver::perScreenOverride(const QString& screenName, const QString& key) const
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

int PerScreenConfigResolver::effectiveInnerGap(const QString& screenName) const
{
    if (auto v = perScreenOverride(screenName, QStringLiteral("InnerGap")))
        return qBound(AutotileDefaults::MinGap, v->toInt(), AutotileDefaults::MaxGap);
    return m_engine->config()->innerGap;
}

int PerScreenConfigResolver::effectiveOuterGap(const QString& screenName) const
{
    if (auto v = perScreenOverride(screenName, QStringLiteral("OuterGap")))
        return qBound(AutotileDefaults::MinGap, v->toInt(), AutotileDefaults::MaxGap);
    return m_engine->config()->outerGap;
}

EdgeGaps PerScreenConfigResolver::effectiveOuterGaps(const QString& screenName) const
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
        return EdgeGaps{topOv ? qBound(AutotileDefaults::MinGap, topOv->toInt(), AutotileDefaults::MaxGap) : base,
                        bottomOv ? qBound(AutotileDefaults::MinGap, bottomOv->toInt(), AutotileDefaults::MaxGap) : base,
                        leftOv ? qBound(AutotileDefaults::MinGap, leftOv->toInt(), AutotileDefaults::MaxGap) : base,
                        rightOv ? qBound(AutotileDefaults::MinGap, rightOv->toInt(), AutotileDefaults::MaxGap) : base};
    }

    // Check per-screen uniform outer gap
    if (auto v = perScreenOverride(screenName, QStringLiteral("OuterGap"))) {
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

bool PerScreenConfigResolver::effectiveSmartGaps(const QString& screenName) const
{
    if (auto v = perScreenOverride(screenName, QStringLiteral("SmartGaps")))
        return v->toBool();
    return m_engine->config()->smartGaps;
}

bool PerScreenConfigResolver::effectiveRespectMinimumSize(const QString& screenName) const
{
    if (auto v = perScreenOverride(screenName, QStringLiteral("RespectMinimumSize")))
        return v->toBool();
    return m_engine->config()->respectMinimumSize;
}

int PerScreenConfigResolver::effectiveMaxWindows(const QString& screenName) const
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
                              << screenName << "- falling back to global maxWindows";
    }

    // 3. Same algorithm globally and per-screen — use the global setting
    return m_engine->config()->maxWindows;
}

QString PerScreenConfigResolver::effectiveAlgorithmId(const QString& screenName) const
{
    if (auto v = perScreenOverride(screenName, QLatin1String("Algorithm")))
        return v->toString();
    return m_engine->m_algorithmId;
}

TilingAlgorithm* PerScreenConfigResolver::effectiveAlgorithm(const QString& screenName) const
{
    return AlgorithmRegistry::instance()->algorithm(effectiveAlgorithmId(screenName));
}

} // namespace PlasmaZones
