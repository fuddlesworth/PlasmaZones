// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortileengine_export.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <QHash>
#include <QString>
#include <QVariantMap>
#include <functional>
#include <optional>

namespace PhosphorTiles {
class TilingAlgorithm;
}

namespace PhosphorTileEngine {

namespace PerScreenKeys = PhosphorEngine::PerScreenKeys;

class AutotileConfig;
class AutotileEngine;

/**
 * @brief Resolves per-screen configuration overrides for autotiling
 *
 * PerScreenConfigResolver manages per-screen autotile overrides (gaps,
 * algorithm, split ratio, master count, etc.) and resolves effective values
 * by falling back to the global AutotileConfig when no override exists.
 *
 * Uses a back-pointer to AutotileEngine for access to global config, algorithm
 * registry, and tiling state. Declared as a friend class in AutotileEngine.
 *
 * @see AutotileEngine for the owning engine
 * @see AutotileConfig for global configuration
 */
class PHOSPHORTILEENGINE_EXPORT PerScreenConfigResolver
{
public:
    explicit PerScreenConfigResolver(AutotileEngine* engine);

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-screen override storage
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Apply per-screen configuration overrides
     *
     * Merges per-screen autotile settings into the PhosphorTiles::TilingState for a screen.
     * Overrides take precedence over global config for that screen.
     *
     * @param screenId Screen to configure
     * @param overrides Key-value map of autotile settings
     */
    void applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides);

    /**
     * @brief Clear per-screen configuration overrides
     *
     * Removes all overrides for the screen and restores global defaults
     * on its PhosphorTiles::TilingState.
     *
     * @param screenId Screen to clear overrides for
     */
    void clearPerScreenConfig(const QString& screenId);

    /**
     * @brief Get currently applied per-screen overrides for comparison
     */
    QVariantMap perScreenOverrides(const QString& screenId) const;

    /**
     * @brief Check if a screen has a per-screen override for a specific key
     */
    bool hasPerScreenOverride(const QString& screenId, const QString& key) const;

    /**
     * @brief Update a single per-screen override value in-place
     *
     * Used by shortcut handlers to persist runtime-adjusted values (e.g. split
     * ratio, master count) back into the stored override map so they survive
     * settings reloads and applyPerScreenConfig round-trips.
     */
    void updatePerScreenOverride(const QString& screenId, const QString& key, const QVariant& value);

    /**
     * @brief Remove all overrides for a screen (used during screen removal)
     *
     * Unlike clearPerScreenConfig() this restores nothing on the TilingState and
     * schedules no retile — the caller is tearing the screen down. It does still
     * wipe per-algorithm state bags when dropping an Algorithm override changes
     * the screen's effective algorithm, because the caller's teardown may leave
     * other (desktop, activity) states of the screen alive.
     */
    void removeOverridesForScreen(const QString& screenId);

    /**
     * @brief Inject a per-context (window-rule) gap-override provider.
     *
     * Returns a QVariantMap keyed by the same PerScreenKeys gap keys this
     * resolver already understands (InnerGap, OuterGap, OuterGap{Top,Bottom,
     * Left,Right}, UsePerSideOuterGap) for the screen's CURRENT context
     * (desktop / activity, resolved by the daemon closure). These take
     * precedence over the static per-screen overrides, matching the snapping
     * gap pipeline where a context-rule gap override is the highest-priority
     * layer. Engine library stays settings-agnostic (LGPL boundary): the daemon
     * adapts its LayoutRegistry::resolveContextGaps result into the map. Empty /
     * unset → no context override (the historical behaviour).
     */
    using ContextGapProvider = std::function<QVariantMap(const QString& screenId)>;
    void setContextGapProvider(ContextGapProvider provider)
    {
        m_contextGapProvider = std::move(provider);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Effective per-screen values (per-screen override → global fallback)
    // ═══════════════════════════════════════════════════════════════════════════

    int effectiveInnerGap(const QString& screenId) const;
    int effectiveOuterGap(const QString& screenId) const;
    ::PhosphorLayout::EdgeGaps effectiveOuterGaps(const QString& screenId) const;
    bool effectiveSmartGaps(const QString& screenId) const;
    bool effectiveRespectMinimumSize(const QString& screenId) const;
    int effectiveMaxWindows(const QString& screenId) const;
    PhosphorTiles::AutotileOverflowBehavior effectiveOverflowBehavior(const QString& screenId) const;
    PhosphorTiles::AutotileInsertPosition effectiveInsertPosition(const QString& screenId) const;
    /// The per-screen custom-parameter override map (a SetAlgorithmParam rule the
    /// daemon injected), or an empty map when none. Layered over the global
    /// per-algorithm config by the engine; the daemon has already gated it on the
    /// screen's effective algorithm.
    QVariantMap effectiveCustomParamsOverride(const QString& screenId) const;
    qreal effectiveSplitRatioStep(const QString& screenId) const;
    QString effectiveAlgorithmId(const QString& screenId) const;
    PhosphorTiles::TilingAlgorithm* effectiveAlgorithm(const QString& screenId) const;

private:
    std::optional<QVariant> perScreenOverride(const QString& screenId, const QString& key) const;

    /// Wipe the per-algorithm state bags (script state, and split tree when the
    /// incoming algorithm lacks memory) on every (desktop, activity) state of
    /// `screenId` when its effective algorithm changed. State bags never cross
    /// algorithms; see the implementation comment for why this lives here.
    void wipeStateBagsOnEffectiveAlgorithmChange(const QString& screenId, const QString& oldEffectiveId,
                                                 const QString& newEffectiveId);

    /// Clamped per-context override for a single gap key (InnerGap / OuterGap),
    /// or nullopt when no provider is wired or the context map lacks the key.
    std::optional<int> contextGap(const QString& screenId, QLatin1String key) const;

    /// Per-context outer-gap override resolved as one atomic layer (per-side
    /// honoured only when UsePerSideOuterGap is set), mirroring the snapping
    /// pipeline. nullopt when the context layer carries no outer-gap info.
    std::optional<::PhosphorLayout::EdgeGaps> contextOuterGaps(const QString& screenId) const;

    AutotileEngine* m_engine = nullptr;
    QHash<QString, QVariantMap> m_perScreenOverrides;
    ContextGapProvider m_contextGapProvider{};
};

} // namespace PhosphorTileEngine
