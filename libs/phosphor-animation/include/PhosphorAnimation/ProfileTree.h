// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace PhosphorAnimation {

/**
 * @brief Hierarchical profile storage with walk-up inheritance.
 *
 * A ProfileTree is a **sparse** map from dot-path strings
 * (see ProfilePaths.h) to Profile overrides, plus a single baseline
 * profile stored at the root. Clients look up per-event config via
 * `resolve(path)`, which returns the merged effective Profile after
 * walking the inheritance chain.
 *
 * ## Inheritance semantics
 *
 * For a lookup path like `"window.open"`, resolve() walks:
 *
 *   1. `"window.open"`   (explicit leaf override)
 *   2. `"window"`        (category override)
 *   3. `"global"`        (root)
 *   4. library default   (fallback if everything above is empty)
 *
 * Each step **supplies missing fields**: if `"window.open"` sets only
 * `duration = 300`, the curve / stagger / mode fields are inherited
 * from `"window"` if present there, else `"global"`, else library
 * defaults. A null `curve` is the sentinel for "inherit".
 *
 * ## Why a sparse tree, not a fixed enum
 *
 * This matches niri / Hyprland / Quickshell customization: users and
 * plugins introduce new event paths freely (`"widget.toast.slideIn"`,
 * `"overview.compose"`, …) without the library knowing. The tree is
 * schema-less beyond the dot-path convention.
 *
 * ## Thread safety
 *
 * ProfileTree is **not** internally synchronized — it's a value type.
 * Callers that share a tree across threads must add external locking
 * or pass by value (profiles copy cheaply since the curve is a shared
 * pointer).
 */
class PHOSPHORANIMATION_EXPORT ProfileTree
{
public:
    ProfileTree() = default;

    ProfileTree(const ProfileTree&) = default;
    ProfileTree& operator=(const ProfileTree&) = default;
    ProfileTree(ProfileTree&&) = default;
    ProfileTree& operator=(ProfileTree&&) = default;

    // ─────── Lookup ───────

    /**
     * @brief Resolve the effective Profile for @p path.
     *
     * Walks path segments right-to-left, merging fields from child
     * toward root. Missing fields (null curve, zero duration, etc.) in
     * a level are filled from the next parent up. A path with no
     * override anywhere resolves to the library default Profile
     * (outCubic bezier, 150 ms, no stagger, all-at-once).
     *
     * Always returns a valid Profile — never null.
     */
    Profile resolve(const QString& path) const;

    /**
     * @brief Explicit Profile override at @p path, if any.
     *
     * Unlike `resolve()`, this does NOT walk parents. Returns a default
     * Profile (all zero / null) when @p path has no direct override;
     * use `hasOverride()` to distinguish absent from zero-valued.
     */
    Profile override_(const QString& path) const;

    /// True if @p path has a direct override (not inherited).
    bool hasOverride(const QString& path) const;

    /**
     * @brief Every path with a direct override, in insertion order.
     *
     * Useful for serialization iteration + settings UI enumeration of
     * "which events has the user customized". Does NOT include implied
     * paths that only resolve via inheritance.
     */
    QStringList overriddenPaths() const;

    // ─────── Mutation ───────

    /// Install an explicit override at @p path. Replaces any prior
    /// override at the same path. Empty @p path is rejected (no-op).
    void setOverride(const QString& path, const Profile& profile);

    /// Remove the override at @p path. No-op if there wasn't one.
    /// Returns true if an override was removed.
    bool clearOverride(const QString& path);

    /// Remove every override. The baseline Profile is unchanged.
    void clearAllOverrides();

    // ─────── Baseline ───────

    /**
     * @brief The baseline profile returned when nothing overrides a path.
     *
     * This is the "global" profile — equivalent to `setOverride("global",
     * profile)` but stored separately so it always participates in
     * resolution regardless of whether callers explicitly add `"global"`
     * to the override map. Changing the baseline affects every path
     * that doesn't have a full chain of overrides above it.
     */
    Profile baseline() const
    {
        return m_baseline;
    }
    void setBaseline(const Profile& profile);

    // ─────── Serialization ───────

    /**
     * @brief Serialize the entire tree to a single JSON object.
     *
     * Shape:
     * @code
     *   {
     *     "baseline":   { Profile-JSON },
     *     "overrides":  {
     *         "window":        { Profile-JSON },
     *         "window.open":   { Profile-JSON },
     *         "zone.snapIn":   { Profile-JSON },
     *         …
     *     }
     *   }
     * @endcode
     *
     * The `overrides` map keys are dot-paths; values are per-path
     * Profile JSON (see Profile::toJson()).
     */
    QJsonObject toJson() const;

    /// Parse a tree from the shape above. Unknown keys are ignored.
    /// Invalid Profile entries within `overrides` fall back to defaults.
    static ProfileTree fromJson(const QJsonObject& obj);

    // ─────── Equality ───────

    bool operator==(const ProfileTree& other) const;
    bool operator!=(const ProfileTree& other) const
    {
        return !(*this == other);
    }

private:
    /// Merge non-default fields from @p src into @p dst.
    /// @p dst is the child (higher priority); @p src is the parent.
    /// Only fields that equal the Profile default in @p dst get filled.
    static void mergeFromParent(Profile& dst, const Profile& src);

    Profile m_baseline;
    QHash<QString, Profile> m_overrides;
    // Insertion-order mirror for deterministic overriddenPaths() / JSON.
    QStringList m_insertionOrder;
};

} // namespace PhosphorAnimation
