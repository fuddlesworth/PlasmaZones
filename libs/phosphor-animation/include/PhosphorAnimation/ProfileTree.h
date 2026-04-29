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

class CurveRegistry;

/**
 * @brief Hierarchical profile storage with walk-up inheritance.
 *
 * A ProfileTree is a **sparse** map from dot-path strings
 * (see ProfilePaths.h) to Profile overrides, plus a single baseline
 * profile stored at the root. Clients look up per-event config via
 * `resolve(path)`, which returns the merged effective Profile after
 * walking the inheritance chain and filling library defaults.
 *
 * ## Inheritance semantics
 *
 * For a lookup path like `"window.open"`, resolve() walks:
 *
 *   1. `"window.open"`   (explicit leaf override)
 *   2. `"window"`        (category override)
 *   3. `"global"`        (baseline)
 *   4. library default   (fallback for any still-unset field)
 *
 * At each step, any **engaged** optional in the override replaces the
 * corresponding field in the accumulator. A `std::nullopt` field is a
 * "no opinion here" marker and leaves the accumulator alone. This is
 * the mechanism that lets a child explicitly reset a field to the
 * library default (set it to the default value explicitly) while a
 * silent child inherits the parent. See `Profile` for the full
 * rationale.
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
     * Walks path segments root-to-leaf, overlaying each override's
     * engaged fields onto the accumulator. Any field still unset after
     * the chain walk is filled from `Profile::Default*`. Always returns
     * a fully-populated Profile — every optional field in the result is
     * engaged (except `curve`, which may still be null if no chain
     * member supplied one).
     */
    Profile resolve(const QString& path) const;

    /**
     * @brief Explicit Profile override at @p path, if any.
     *
     * Unlike `resolve()`, this does NOT walk parents. Returns a default
     * Profile (every field unset) when @p path has no direct override;
     * use `hasOverride()` to distinguish absent from all-unset.
     */
    Profile directOverride(const QString& path) const;

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
     * This is the "global" profile — logically equivalent to
     * `setOverride("global", profile)` but stored separately so it
     * always participates in resolution regardless of whether callers
     * explicitly add `"global"` to the override map.
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
     *     "overrides":  [
     *         { "path": "window",        "profile": { Profile-JSON } },
     *         { "path": "window.open",   "profile": { Profile-JSON } },
     *         { "path": "zone.snapIn",   "profile": { Profile-JSON } }
     *     ]
     *   }
     * @endcode
     *
     * The `overrides` value is a JSON array (not object) so insertion
     * order round-trips losslessly — `QJsonObject` would sort keys
     * alphabetically and reshuffle the settings-UI ordering.
     */
    QJsonObject toJson() const;

    /// Parse a tree from the shape above. Invalid Profile entries fall
    /// back to a default-constructed Profile (every field unset), and
    /// entries with empty path strings are dropped.
    /// @p registry is forwarded to every nested @ref Profile::fromJson
    /// call. Per-process registries replace the prior
    /// `CurveRegistry::instance()` singleton.
    ///
    /// @p registry is used synchronously for curve resolution — the
    /// reference does not need to outlive the returned ProfileTree.
    /// See @ref Profile::fromJson for the same contract.
    static ProfileTree fromJson(const QJsonObject& obj, const CurveRegistry& registry);

    // ─────── Equality ───────

    bool operator==(const ProfileTree& other) const;
    bool operator!=(const ProfileTree& other) const
    {
        return !(*this == other);
    }

private:
    /// Overlay @p src onto @p dst: every engaged field in src replaces
    /// the corresponding field in dst. Unset fields in src are ignored.
    static void overlay(Profile& dst, const Profile& src);

    Profile m_baseline;
    QHash<QString, Profile> m_overrides;
    // Insertion-order mirror for deterministic overriddenPaths() / JSON.
    QStringList m_insertionOrder;
};

} // namespace PhosphorAnimation
