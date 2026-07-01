// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/phosphorsurface_export.h>

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace PhosphorSurfaceShaders {

/**
 * @brief Hierarchical DecorationProfile storage with walk-up inheritance.
 *
 * Same inheritance semantics as `PhosphorAnimationShaders::ShaderProfileTree`
 * but carrying `DecorationProfile` payloads (a decoration shader-pack chain
 * plus border/titlebar appearance) instead of `ShaderProfile`. Keyed on a
 * dot-path surface namespace (DecorationSupportedPaths) so a consumer can
 * resolve the decoration config for any surface.
 *
 * ## Walk-up inheritance
 *
 * For `"window.tiled"`, resolve() walks:
 *   1. baseline           (global default)
 *   2. `"window"`         (category override)
 *   3. `"window.tiled"`   (leaf override)
 *
 * Overlay order is shallowest-first so the deepest (most specific) override
 * wins: each engaged optional replaces the accumulator, unset fields pass
 * through.
 *
 * ## Thread safety
 *
 * Value type, not internally synchronized. Same as ShaderProfileTree.
 */
class PHOSPHORSURFACE_EXPORT DecorationProfileTree
{
public:
    DecorationProfileTree() = default;

    DecorationProfileTree(const DecorationProfileTree&) = default;
    DecorationProfileTree& operator=(const DecorationProfileTree&) = default;
    DecorationProfileTree(DecorationProfileTree&&) = default;
    DecorationProfileTree& operator=(DecorationProfileTree&&) = default;

    // ─────── Lookup ───────

    DecorationProfile resolve(const QString& surfacePath) const;
    /// Returns the override stored directly at @p surfacePath, or a
    /// default-constructed (all-unset) profile when none exists — which is
    /// indistinguishable from a real all-unset override. Pair with hasOverride()
    /// before trusting a non-empty-looking result.
    DecorationProfile directOverride(const QString& surfacePath) const;
    bool hasOverride(const QString& surfacePath) const;
    QStringList overriddenPaths() const;

    // ─────── Mutation ───────

    void setOverride(const QString& surfacePath, const DecorationProfile& profile);
    bool clearOverride(const QString& surfacePath);
    void clearAllOverrides();

    // ─────── Baseline ───────

    DecorationProfile baseline() const
    {
        return m_baseline;
    }
    void setBaseline(const DecorationProfile& profile);

    // ─────── Serialization ───────

    QJsonObject toJson() const;
    static DecorationProfileTree fromJson(const QJsonObject& obj);

    // ─────── Equality ───────

    bool operator==(const DecorationProfileTree& other) const;
    bool operator!=(const DecorationProfileTree& other) const
    {
        return !(*this == other);
    }

private:
    DecorationProfile m_baseline;
    QHash<QString, DecorationProfile> m_overrides;
    QStringList m_insertionOrder;
};

} // namespace PhosphorSurfaceShaders
