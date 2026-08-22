// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace PhosphorAnimationShaders {

/**
 * @brief Hierarchical ShaderProfile storage with walk-up inheritance.
 *
 * Same inheritance semantics as `PhosphorAnimation::ProfileTree` but
 * carrying `ShaderProfile` payloads instead of `Profile`. The two trees
 * share the same dot-path namespace (ProfilePaths) so a consumer can
 * resolve both motion and shader config for the same event path.
 *
 * ## Walk-up inheritance
 *
 * For `"window.appearance.open"`, resolve() walks:
 *   1. `"window.appearance.open"`  (leaf override)
 *   2. `"window.appearance"`       (appearance contract group — the "All Appearance" node)
 *   3. `"window"`                  (category — the all-windows node)
 *   4. `"global"`                  (the assignable root node — a real chain member
 *                                   a user can store an override on)
 *   5. the tree baseline           (setBaseline(), NOT the same thing as 4)
 *   6. library default             (empty ShaderProfile — no effect)
 *
 * Steps 4 and 5 are two independent levels and cutting one does not cut the
 * other. That distinction is load-bearing for both exceptions below: an
 * isolation that drops only the baseline still lets whatever the user assigned
 * on the `global` node cascade in.
 *
 * At each step, engaged optionals replace the accumulator. Unset fields
 * pass through.
 *
 * EXCEPTION 1, a leaf that inherits NOTHING: the interactive-drag leaf
 * (`window.movement.move`) and the tab-switch leaf (`scrolling.tabSwitch`)
 * take no inherited shader — resolve() reads only their direct override. The two
 * are in the predicate for different reasons. The drag leaf's ancestor picks are
 * single-surface crossfade packs by construction and cannot drive the held drag
 * transition (see EventClassMove in ProfilePaths.h). The tab leaf's class is
 * OPT-IN rather than universal-permissive — a pack must declare
 * `appliesTo: ["tab"]` to be offered (see EventClassTab in ProfilePaths.h) — so
 * every pack a picker offers on an ancestor FOR THAT ANCESTOR'S OWN SAKE is
 * refused here. Note the leaf has three levels above it, not one:
 * parentPath("scrolling") is `global`, not empty, and the baseline sits above
 * that. The one pack that would survive the gate is a HYBRID declaring `tab`
 * beside the ancestor's class (`["tab","appearance"]` is assignable on `global`,
 * `["tab","strip"]` on `scrolling`), so isolating the leaf is a policy choice for
 * that case rather than a proof: a hybrid the user engaged for window appearance
 * must not silently start driving tab swaps. For everything else the refusal is
 * structural, and it happens when the transition BEGINS
 * (shaderEffectAppliesToEventPath), not at install.
 * Predicate: shaderPathResolvesInIsolation().
 *
 * EXCEPTION 2, a SUBTREE that inherits normally within itself while nothing
 * above it reaches in: today the `shell` root. Inheritance between the root and
 * its legs works as usual, so a pack on the root cascades to them, but resolve()
 * substitutes an empty baseline AND trims the chain back to the root, closing
 * steps 4 and 5 together. Predicate: shaderPathIsolationRoot().
 *
 * ## Thread safety
 *
 * Value type, not internally synchronized. Same as ProfileTree.
 */
class PHOSPHORANIMATION_EXPORT ShaderProfileTree
{
public:
    ShaderProfileTree() = default;

    ShaderProfileTree(const ShaderProfileTree&) = default;
    ShaderProfileTree& operator=(const ShaderProfileTree&) = default;
    ShaderProfileTree(ShaderProfileTree&&) = default;
    ShaderProfileTree& operator=(ShaderProfileTree&&) = default;

    // ─────── Lookup ───────

    ShaderProfile resolve(const QString& path) const;
    ShaderProfile directOverride(const QString& path) const;
    bool hasOverride(const QString& path) const;
    QStringList overriddenPaths() const;

    // ─────── Mutation ───────

    void setOverride(const QString& path, const ShaderProfile& profile);
    bool clearOverride(const QString& path);
    void clearAllOverrides();

    // ─────── Baseline ───────

    ShaderProfile baseline() const
    {
        return m_baseline;
    }
    void setBaseline(const ShaderProfile& profile);

    // ─────── Serialization ───────

    QJsonObject toJson() const;
    static ShaderProfileTree fromJson(const QJsonObject& obj);

    // ─────── Equality ───────

    bool operator==(const ShaderProfileTree& other) const;
    bool operator!=(const ShaderProfileTree& other) const
    {
        return !(*this == other);
    }

private:
    ShaderProfile m_baseline;
    QHash<QString, ShaderProfile> m_overrides;
    QStringList m_insertionOrder;
};

/// True when @p path resolves its shader in ISOLATION: ShaderProfileTree::
/// resolve reads only the direct override at the path, so no ancestor or
/// baseline shader ever applies there — and, symmetrically, an override AT
/// the path can never shadow an ancestor's shader. Two members today, the
/// interactive-drag leaf (ProfilePaths::WindowMove) and the tab-switch leaf
/// (ProfilePaths::ScrollingTabSwitch); see EXCEPTION 1 in the walk-up
/// inheritance note above. Exposed so shadowing-aware consumers
/// (e.g. the settings "shadowing children" banner walk) share the
/// resolver's definition instead of re-deriving it from a path prefix.
PHOSPHORANIMATION_EXPORT bool shaderPathResolvesInIsolation(const QString& path);

/// The subtree root @p path resolves WITHIN, or an empty string when it
/// resolves the ordinary way (from the tree baseline, down the whole chain).
///
/// The weaker sibling of the predicate above, and the shape a whole FAMILY of
/// foreign surfaces needs rather than a lone leaf: inheritance still works
/// normally between the root and the leaf, so a pack set on the root cascades
/// to its legs, but nothing ABOVE the root reaches in — not the `global` node,
/// not the baseline. Today that root is ProfilePaths::Shell, whose surfaces
/// belong to plasmashell rather than to any application: what the user chose
/// for their own windows must not start playing on the system tray, and
/// engaging a pack inside the subtree is the whole opt-in. The decoration tree
/// isolates its own `shell` subtree the same way and for the same reason (see
/// PhosphorSurface's decorationPathIsBaselineIsolated).
///
/// Exported for the same reason the predicate above is: a second copy of "where
/// does inheritance start" drifts from the resolver. Consumed by resolve() and by
/// the kwin-effect's event resolution (shader_config_dbus.cpp), which gates THREE
/// things on it: the window-filtering call (shouldAnimateWindow is SKIPPED for an
/// isolated path — it would reject every plasmashell surface outright on its
/// structural clause, and its Animations.WindowFiltering knobs and
/// ExcludeAnimations rules are all authored about application windows), the
/// rule tier (resolved windowless), and the cascade-coverage
/// diagnostic. Anything reasoning about what a `shell.*` path inherits should call
/// this rather than re-derive it from a path prefix.
PHOSPHORANIMATION_EXPORT QString shaderPathIsolationRoot(const QString& path);

/// Resolve @p path against @p tree, applying the built-in per-event default
/// shader (ProfilePaths::defaultShaderEffectIdForPath, e.g. "window-morph" for
/// window snap events) when the path is TRULY UNSET — i.e. neither it nor any
/// ancestor carries an override. An explicit "None" (an engaged-empty
/// override) IS an override, so it is respected and the default is NOT applied.
///
/// SSOT for "what shader does this event use", shared by the kwin-effect
/// resolution and the settings UI so the built-in default both plays at runtime
/// and shows as the current value in settings — without persisting the default
/// into the user's config (it's computed, not stored).
PHOSPHORANIMATION_EXPORT ShaderProfile resolveShaderWithDefault(const ShaderProfileTree& tree, const QString& path);

} // namespace PhosphorAnimationShaders
