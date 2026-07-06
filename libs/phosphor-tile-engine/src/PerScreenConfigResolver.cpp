// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTileEngine/PerScreenConfigResolver.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorTiles/AutotileConstants.h>
#include "tileenginelogging.h"

namespace PhosphorTileEngine {

namespace PerScreenKeys = PhosphorEngine::PerScreenKeys;

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

    PhosphorTiles::TilingState* state = m_engine->tilingStateForScreen(screenId);
    if (!state) {
        return;
    }

    // Apply PhosphorTiles::TilingState-level overrides (splitRatio, masterCount)
    auto it = overrides.constFind(QString(PerScreenKeys::SplitRatio));
    if (it != overrides.constEnd()) {
        state->setSplitRatio(qBound(PhosphorTiles::AutotileDefaults::MinSplitRatio, it->toDouble(),
                                    PhosphorTiles::AutotileDefaults::MaxSplitRatio));
    }

    it = overrides.constFind(QString(PerScreenKeys::MasterCount));
    if (it != overrides.constEnd()) {
        state->setMasterCount(qBound(PhosphorTiles::AutotileDefaults::MinMasterCount, it->toInt(),
                                     PhosphorTiles::AutotileDefaults::MaxMasterCount));
    }

    // If algorithm changed and split ratio wasn't explicitly overridden,
    // reset to the new algorithm's default (matching setAlgorithm() logic).
    it = overrides.constFind(QString(PerScreenKeys::Algorithm));
    if (it != overrides.constEnd()) {
        QString algoId = it->toString();
        auto* registry = m_engine->algorithmRegistry();
        PhosphorTiles::TilingAlgorithm* newAlgo = registry->algorithm(algoId);
        if (newAlgo) {
            if (!overrides.contains(QString(PerScreenKeys::SplitRatio))) {
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

    qCDebug(PhosphorTileEngine::lcTileEngine)
        << "Applied per-screen config for" << screenId << "keys:" << overrides.keys();
}

void PerScreenConfigResolver::clearPerScreenConfig(const QString& screenId)
{
    if (!m_perScreenOverrides.remove(screenId)) {
        return;
    }
    // Restore global defaults on PhosphorTiles::TilingState
    PhosphorTiles::TilingState* state = m_engine->tilingStateForScreen(screenId);
    if (state) {
        state->setSplitRatio(m_engine->config()->splitRatio);
        state->setMasterCount(m_engine->config()->masterCount);
    }

    // Schedule deferred retile (same rationale as applyPerScreenConfig)
    if (m_engine->isAutotileScreen(screenId)) {
        m_engine->scheduleRetileForScreen(screenId);
    }

    qCDebug(PhosphorTileEngine::lcTileEngine) << "Cleared per-screen config for" << screenId;
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
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "updatePerScreenOverride: no override map for screen" << screenId << "- cannot update key" << key;
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

namespace {
// Clamp a raw gap value to [MinGap, MaxGap]. The ceiling is tied to the
// snapping side by a static_assert in the daemon; the floor is a fixed 0 on
// both sides (snapping has no MinGap constant to tie against).
int clampGap(int v)
{
    return qBound(PhosphorTiles::AutotileDefaults::MinGap, v, PhosphorTiles::AutotileDefaults::MaxGap);
}
} // namespace

std::optional<int> PerScreenConfigResolver::contextGap(const QString& screenId, QLatin1String key) const
{
    if (!m_contextGapProvider) {
        return std::nullopt;
    }
    const QVariantMap ctx = m_contextGapProvider(screenId);
    const auto it = ctx.constFind(key);
    if (it == ctx.constEnd()) {
        return std::nullopt;
    }
    return clampGap(it->toInt());
}

std::optional<::PhosphorLayout::EdgeGaps> PerScreenConfigResolver::contextOuterGaps(const QString& screenId) const
{
    // Resolve the per-context (window-rule) outer-gap override as ONE atomic
    // layer, mirroring the snapping pipeline (GeometryUtils::
    // resolveOuterGapsFromMap): per-side values are honoured only when the rule
    // set UsePerSideOuterGap, and if the layer yields any outer gap it wins
    // wholesale — no per-key blending with the static per-screen layer below.
    if (!m_contextGapProvider) {
        return std::nullopt;
    }
    const QVariantMap ctx = m_contextGapProvider(screenId);
    if (ctx.isEmpty()) {
        return std::nullopt;
    }
    const auto uniformIt = ctx.constFind(QString(PerScreenKeys::OuterGap));
    const bool usePerSide = ctx.value(QString(PerScreenKeys::UsePerSideOuterGap), false).toBool();
    if (usePerSide) {
        const auto topIt = ctx.constFind(QString(PerScreenKeys::OuterGapTop));
        const auto bottomIt = ctx.constFind(QString(PerScreenKeys::OuterGapBottom));
        const auto leftIt = ctx.constFind(QString(PerScreenKeys::OuterGapLeft));
        const auto rightIt = ctx.constFind(QString(PerScreenKeys::OuterGapRight));
        if (topIt != ctx.constEnd() || bottomIt != ctx.constEnd() || leftIt != ctx.constEnd()
            || rightIt != ctx.constEnd()) {
            // Missing sides fall back to the layer's own uniform OuterGap, else
            // the global config — matching resolveOuterGapsFromMap's base.
            const int base =
                (uniformIt != ctx.constEnd()) ? clampGap(uniformIt->toInt()) : m_engine->config()->outerGap;
            return ::PhosphorLayout::EdgeGaps{(topIt != ctx.constEnd()) ? clampGap(topIt->toInt()) : base,
                                              (bottomIt != ctx.constEnd()) ? clampGap(bottomIt->toInt()) : base,
                                              (leftIt != ctx.constEnd()) ? clampGap(leftIt->toInt()) : base,
                                              (rightIt != ctx.constEnd()) ? clampGap(rightIt->toInt()) : base};
        }
    }
    if (uniformIt != ctx.constEnd()) {
        return ::PhosphorLayout::EdgeGaps::uniform(clampGap(uniformIt->toInt()));
    }
    return std::nullopt;
}

int PerScreenConfigResolver::effectiveInnerGap(const QString& screenId) const
{
    if (auto ctx = contextGap(screenId, PerScreenKeys::InnerGap))
        return *ctx;
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::InnerGap)))
        return clampGap(v->toInt());
    return m_engine->config()->innerGap;
}

int PerScreenConfigResolver::effectiveOuterGap(const QString& screenId) const
{
    if (auto ctx = contextGap(screenId, PerScreenKeys::OuterGap))
        return *ctx;
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::OuterGap)))
        return clampGap(v->toInt());
    return m_engine->config()->outerGap;
}

::PhosphorLayout::EdgeGaps PerScreenConfigResolver::effectiveOuterGaps(const QString& screenId) const
{
    // Highest precedence: the per-context override, resolved as one atomic layer.
    if (auto ctx = contextOuterGaps(screenId))
        return *ctx;

    // Per-screen per-side overrides next.
    auto topOv = perScreenOverride(screenId, QString(PerScreenKeys::OuterGapTop));
    auto bottomOv = perScreenOverride(screenId, QString(PerScreenKeys::OuterGapBottom));
    auto leftOv = perScreenOverride(screenId, QString(PerScreenKeys::OuterGapLeft));
    auto rightOv = perScreenOverride(screenId, QString(PerScreenKeys::OuterGapRight));

    // If any per-screen per-side override exists, build from those
    if (topOv || bottomOv || leftOv || rightOv) {
        // Use per-screen uniform gap as base, then per-side overrides on top
        const int base = effectiveOuterGap(screenId);
        return ::PhosphorLayout::EdgeGaps{
            topOv ? clampGap(topOv->toInt()) : base, bottomOv ? clampGap(bottomOv->toInt()) : base,
            leftOv ? clampGap(leftOv->toInt()) : base, rightOv ? clampGap(rightOv->toInt()) : base};
    }

    // Check per-screen uniform outer gap
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::OuterGap))) {
        return ::PhosphorLayout::EdgeGaps::uniform(clampGap(v->toInt()));
    }

    // Fall back to global config
    const AutotileConfig* cfg = m_engine->config();
    if (cfg->usePerSideOuterGap) {
        return ::PhosphorLayout::EdgeGaps{cfg->outerGapTop, cfg->outerGapBottom, cfg->outerGapLeft, cfg->outerGapRight};
    }
    return ::PhosphorLayout::EdgeGaps::uniform(cfg->outerGap);
}

bool PerScreenConfigResolver::effectiveSmartGaps(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::SmartGaps)))
        return v->toBool();
    return m_engine->config()->smartGaps;
}

QVariantMap PerScreenConfigResolver::effectiveCustomParamsOverride(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::CustomParams))) {
        return v->toMap();
    }
    return {};
}

PhosphorTiles::AutotileOverflowBehavior
PerScreenConfigResolver::effectiveOverflowBehavior(const QString& screenId) const
{
    // Per-screen override (config store OR a folded-in context rule) → global config.
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::OverflowBehavior))) {
        const int clamped = qBound(0, v->toInt(), 1);
        return static_cast<PhosphorTiles::AutotileOverflowBehavior>(clamped);
    }
    return m_engine->config()->overflowBehavior;
}

PhosphorTiles::AutotileInsertPosition PerScreenConfigResolver::effectiveInsertPosition(const QString& screenId) const
{
    // Per-screen override (config store OR a folded-in tiling rule) → global config.
    // The stored value is the enum's underlying int, clamped to the valid range.
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::InsertPosition))) {
        const int clamped = qBound(PhosphorTiles::AutotileDefaults::MinInsertPosition, v->toInt(),
                                   PhosphorTiles::AutotileDefaults::MaxInsertPosition);
        return static_cast<PhosphorTiles::AutotileInsertPosition>(clamped);
    }
    return m_engine->config()->insertPosition;
}

bool PerScreenConfigResolver::effectiveRespectMinimumSize(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, QString(PerScreenKeys::RespectMinimumSize)))
        return v->toBool();
    return m_engine->config()->respectMinimumSize;
}

int PerScreenConfigResolver::effectiveMaxWindows(const QString& screenId) const
{
    // 1. Explicit per-screen MaxWindows override — highest priority. Wins even
    //    over global Unlimited mode so users can clamp individual screens
    //    (e.g. keep a secondary monitor at maxWindows=3 while the primary
    //    runs unlimited).
    if (auto v = perScreenOverride(screenId, PerScreenKeys::MaxWindows))
        return qBound(PhosphorTiles::AutotileDefaults::MinMaxWindows, v->toInt(),
                      PhosphorTiles::AutotileDefaults::MaxMaxWindows);

    // 2. Unlimited: Krohnkite-style "no cap". Per-screen (a context
    //    SetOverflowBehavior rule or per-screen config override else global). The
    //    sentinel (PhosphorTiles::AutotileDefaults::UnlimitedMaxWindowsSentinel) is
    //    passed to std::min in recalculateLayout, making the clamp idempotent. Also
    //    opens onWindowAdded's gate (tiledWindowCount >= maxWin is never true).
    if (effectiveOverflowBehavior(screenId) == PhosphorTiles::AutotileOverflowBehavior::Unlimited) {
        return PhosphorTiles::AutotileDefaults::UnlimitedMaxWindowsSentinel;
    }

    // 3. When the per-screen algorithm differs from the global algorithm,
    //    the global m_config->maxWindows may be for the WRONG algorithm.
    //    E.g. global=master-stack(maxWindows=4) but per-screen=bsp(default=5).
    //    Use the per-screen algorithm's default — but only if the user hasn't
    //    explicitly customized global maxWindows away from the global algo's default.
    const QString screenAlgo = effectiveAlgorithmId(screenId);
    if (screenAlgo != m_engine->m_algorithmId) {
        auto* registry = m_engine->algorithmRegistry();
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
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "effectiveMaxWindows: unknown per-screen algorithm" << screenAlgo << "for screen" << screenId
            << "- falling back to global maxWindows";
    }

    // 4. Same algorithm globally and per-screen — use the global setting
    return m_engine->config()->maxWindows;
}

qreal PerScreenConfigResolver::effectiveSplitRatioStep(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, PerScreenKeys::SplitRatioStep))
        return qBound(PhosphorTiles::AutotileDefaults::MinSplitRatioStep, v->toDouble(),
                      PhosphorTiles::AutotileDefaults::MaxSplitRatioStep);
    return m_engine->config()->splitRatioStep;
}

QString PerScreenConfigResolver::effectiveAlgorithmId(const QString& screenId) const
{
    if (auto v = perScreenOverride(screenId, PerScreenKeys::Algorithm))
        return v->toString();
    return m_engine->m_algorithmId;
}

PhosphorTiles::TilingAlgorithm* PerScreenConfigResolver::effectiveAlgorithm(const QString& screenId) const
{
    return m_engine->algorithmRegistry()->algorithm(effectiveAlgorithmId(screenId));
}

} // namespace PhosphorTileEngine
