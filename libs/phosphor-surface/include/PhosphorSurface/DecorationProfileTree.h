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
 * plus its per-pack parameters) instead of `ShaderProfile`. Keyed on a
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
    /// Two-layer seed overlay: returns a copy of this tree with @p seeds
    /// injected as the LOWEST-precedence layer — the same seed model as
    /// `PhosphorAnimation::PhosphorProfileRegistry::resolveWithInheritance`'s
    /// low-precedence owner tag, expressed as a tree-to-tree merge.
    ///
    /// For each seed override path (and the seed baseline), a seed field is
    /// copied in ONLY where this tree leaves room for it:
    ///  - If this tree engages `chain` at the path or anywhere on its walk-up
    ///    (baseline or an ancestor override), the path is skipped entirely —
    ///    the user built their own look there, and injecting the seed's
    ///    parameters or disable set under a foreign chain is meaningless.
    ///    An engaged-but-empty chain therefore keeps a surface explicitly
    ///    undecorated.
    ///  - Otherwise `parameters` and `disabledPacks` each inject only when
    ///    this tree engages that SAME field nowhere on the walk-up either, so
    ///    a parameters-only retune (at the path or at an ancestor) keeps the
    ///    seed chain while the user's map wins — an injected leaf field would
    ///    otherwise shadow an engaged ancestor map in resolve()'s
    ///    deepest-last overlay.
    DecorationProfileTree withSeedDefaults(const DecorationProfileTree& seeds) const;
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
