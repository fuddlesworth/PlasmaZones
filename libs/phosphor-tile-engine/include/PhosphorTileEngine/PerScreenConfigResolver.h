// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortileengine_export.h>
#include <PhosphorEngineApi/PerScreenKeys.h>
#include <QHash>
#include <QString>
#include <QVariantMap>
#include <optional>

namespace PhosphorTiles {
class TilingAlgorithm;
}

namespace PhosphorTileEngine {

namespace PerScreenKeys = PhosphorEngineApi::PerScreenKeys;

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
     */
    void removeOverridesForScreen(const QString& screenId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Effective per-screen values (per-screen override → global fallback)
    // ═══════════════════════════════════════════════════════════════════════════

    int effectiveInnerGap(const QString& screenId) const;
    int effectiveOuterGap(const QString& screenId) const;
    ::PhosphorLayout::EdgeGaps effectiveOuterGaps(const QString& screenId) const;
    bool effectiveSmartGaps(const QString& screenId) const;
    bool effectiveRespectMinimumSize(const QString& screenId) const;
    int effectiveMaxWindows(const QString& screenId) const;
    qreal effectiveSplitRatioStep(const QString& screenId) const;
    QString effectiveAlgorithmId(const QString& screenId) const;
    PhosphorTiles::TilingAlgorithm* effectiveAlgorithm(const QString& screenId) const;

private:
    std::optional<QVariant> perScreenOverride(const QString& screenId, const QString& key) const;

    AutotileEngine* m_engine = nullptr;
    QHash<QString, QVariantMap> m_perScreenOverrides;
};

} // namespace PhosphorTileEngine
