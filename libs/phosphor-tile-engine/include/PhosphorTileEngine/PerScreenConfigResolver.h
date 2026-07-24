// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortileengine_export.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <PhosphorTiles/AutotileConstants.h>
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
     * @brief Drop a screen's in-memory overrides while REMEMBERING the algorithm
     *        they resolved to (autotile toggle-off).
     *
     * Unlike clearPerScreenConfig() this restores nothing on the TilingState and
     * schedules no retile — the caller is tearing the screen down. It also does
     * NOT wipe state bags, and that is the point: dropping the in-memory map is
     * not a configuration change. The persisted per-screen settings survive and
     * re-derive the same algorithm on re-enable, so a wipe here would destroy the
     * bags of every (desktop, activity) state the teardown left alive, on a
     * comparison that only reads as a change because this call is what caused it.
     *
     * The resolved id is recorded instead, so the next genuine change compares
     * against what the screen's states were actually built under. Use
     * forgetScreen() for an id that is never coming back.
     */
    void removeOverridesForScreen(const QString& screenId);

    /**
     * @brief Drop remembered algorithms for screens that are no longer connected.
     *
     * The mirror of the engine's stash purge, run on the same event: these ids
     * describe bags that have just been discarded, so keeping them would leave a
     * stale "old" side for a screen that is never coming back. Keyed on
     * connectedness, NOT autotile membership — a screen merely toggled out of
     * autotile keeps its memory, which is what marks it as pinned rather than
     * following the global algorithm.
     */
    void forgetRememberedAlgorithmsForUnknownScreens();

    /**
     * @brief Drop a screen's overrides AND the remembered algorithm, for an id
     *        that will never be reused (an orphaned virtual screen).
     *
     * No wipe: the caller is destroying every state of the id, so there is
     * nothing left to protect from crossing algorithms.
     */
    void forgetScreen(const QString& screenId);

    /**
     * @brief Apply a GLOBAL algorithm switch to every screen that follows it.
     *
     * Called by AutotileEngine::setAlgorithm with the outgoing and incoming
     * global ids. A screen follows the global one unless it is pinned by a
     * per-screen Algorithm override, and the pinning must be read from what the
     * screen's states were BUILT UNDER rather than from the live override map:
     * a toggle-off has already dropped that map, so a screen pinned by persisted
     * settings reads there as a follower. Acting on that reading wiped the bags
     * of screens whose effective algorithm never moved.
     *
     * Only screens holding at least one live state are visited. A screen with no
     * state anywhere keeps both its remembered id and its stashed bags, so a
     * switch away and back hands them over intact.
     *
     * This is the single entry point for "the effective algorithm moved" on a
     * global switch; per-screen changes reach the same wipe through
     * applyPerScreenConfig. Keeping one implementation is deliberate — two of
     * them disagreeing about the gate is what produced the bug above.
     */
    void applyGlobalAlgorithmChange(const QString& previousGlobalId, const QString& newGlobalId);

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

    /// Whether @p screenId followed the global algorithm that is being replaced,
    /// and so should take the wipe a global switch runs. A live Algorithm
    /// override always pins the screen; failing that the remembered "built
    /// under" id decides, because a teardown may have dropped the map. See the
    /// implementation for why the precedence is fixed rather than per caller.
    bool screenFollowsGlobalAlgorithm(const QString& screenId, const QString& previousGlobalId) const;

    /// The Algorithm id stored in @p map, or @p fallback when the key is absent
    /// OR stores an empty string. The empty case matters: effectiveAlgorithmId
    /// treats an empty stored id as "no override", so every other reader has to
    /// agree or the wipe fires on a change nobody else can see.
    static QString algorithmIdFromMap(const QVariantMap& map, const QString& fallback);

    /// What @p screenId's live states were last built under: the remembered id
    /// when the screen has been through a toggle-off, else derived from
    /// @p previousOverrides the way every caller used to derive it.
    QString rememberedAlgorithmId(const QString& screenId, const QVariantMap& previousOverrides) const;

    /// The per-context (window-rule) gap map for @p screenId, or an empty map
    /// when no provider is wired. Resolving it runs a full
    /// LayoutRegistry::resolveContextGaps on the daemon side, so callers that
    /// need more than one key resolve it once and pass it to the *FromMap
    /// helpers below rather than calling this per key.
    QVariantMap contextGapMap(const QString& screenId) const;

    /// Clamped per-context override for a single gap key (InnerGap / OuterGap),
    /// or nullopt when @p ctx lacks the key.
    static std::optional<int> contextGapFromMap(const QVariantMap& ctx, QLatin1String key);

    /// Per-context outer-gap override resolved as one atomic layer (per-side
    /// honoured only when UsePerSideOuterGap is set), mirroring the snapping
    /// pipeline. nullopt when the context layer carries no outer-gap info.
    std::optional<::PhosphorLayout::EdgeGaps> contextOuterGapsFromMap(const QVariantMap& ctx) const;

    /// The uniform outer-gap value the per-side resolution fills missing sides
    /// from: the context layer's uniform OuterGap, else the per-screen override,
    /// else the global config. Only a BASE for the missing sides — it skips the
    /// per-side layers that effectiveOuterGaps resolves on top of it.
    int outerGapBase(const QString& screenId, const QVariantMap& ctx) const;

    AutotileEngine* m_engine = nullptr;
    QHash<QString, QVariantMap> m_perScreenOverrides;
    /// screenId -> the effective algorithm its states were last built under.
    /// Written whenever a genuine change is resolved, and by the toggle-off
    /// teardown that drops the override map. Read as the "old" side of the next
    /// change, so a teardown that only the resolver forgot about is not mistaken
    /// for the user reconfiguring the screen.
    QHash<QString, QString> m_rememberedAlgorithmId;
    ContextGapProvider m_contextGapProvider{};
};

} // namespace PhosphorTileEngine
