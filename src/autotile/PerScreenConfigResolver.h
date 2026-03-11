// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "core/constants.h"
#include <QHash>
#include <QString>
#include <QVariantMap>
#include <optional>

namespace PlasmaZones {

class AutotileConfig;
class AutotileEngine;
class TilingAlgorithm;

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
class PLASMAZONES_EXPORT PerScreenConfigResolver
{
public:
    explicit PerScreenConfigResolver(AutotileEngine* engine);

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-screen override storage
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Apply per-screen configuration overrides
     *
     * Merges per-screen autotile settings into the TilingState for a screen.
     * Overrides take precedence over global config for that screen.
     *
     * @param screenName Screen to configure
     * @param overrides Key-value map of autotile settings
     */
    void applyPerScreenConfig(const QString& screenName, const QVariantMap& overrides);

    /**
     * @brief Clear per-screen configuration overrides
     *
     * Removes all overrides for the screen and restores global defaults
     * on its TilingState.
     *
     * @param screenName Screen to clear overrides for
     */
    void clearPerScreenConfig(const QString& screenName);

    /**
     * @brief Get currently applied per-screen overrides for comparison
     */
    QVariantMap perScreenOverrides(const QString& screenName) const;

    /**
     * @brief Check if a screen has a per-screen override for a specific key
     */
    bool hasPerScreenOverride(const QString& screenName, const QString& key) const;

    /**
     * @brief Remove all overrides for a screen (used during screen removal)
     */
    void removeOverridesForScreen(const QString& screenName);

    // ═══════════════════════════════════════════════════════════════════════════
    // Effective per-screen values (per-screen override → global fallback)
    // ═══════════════════════════════════════════════════════════════════════════

    int effectiveInnerGap(const QString& screenName) const;
    int effectiveOuterGap(const QString& screenName) const;
    EdgeGaps effectiveOuterGaps(const QString& screenName) const;
    bool effectiveSmartGaps(const QString& screenName) const;
    bool effectiveRespectMinimumSize(const QString& screenName) const;
    int effectiveMaxWindows(const QString& screenName) const;
    QString effectiveAlgorithmId(const QString& screenName) const;
    TilingAlgorithm* effectiveAlgorithm(const QString& screenName) const;

private:
    std::optional<QVariant> perScreenOverride(const QString& screenName, const QString& key) const;

    AutotileEngine* m_engine = nullptr;
    QHash<QString, QVariantMap> m_perScreenOverrides;
};

} // namespace PlasmaZones
