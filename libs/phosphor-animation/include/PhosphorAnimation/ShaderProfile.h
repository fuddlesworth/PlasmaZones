// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

#include <optional>

namespace PhosphorShaders {
class ShaderPresetRegistry;
}

namespace PhosphorAnimationShaders {

/**
 * @brief Per-event shader effect selection and configuration.
 *
 * Parallel to `PhosphorAnimation::Profile` which configures *motion*
 * (curve, duration, stagger), `ShaderProfile` configures *visual effect*
 * (which shader, with what parameters). Both use the same dot-path event
 * namespace (window.open, editor.snapIn, etc.) but are resolved through
 * separate trees so the two concerns evolve independently.
 *
 * ## "Set" vs "unset" semantics
 *
 * Same contract as `Profile`: `std::optional` fields distinguish "inherit
 * from parent" (nullopt) from "I explicitly chose this value" (engaged).
 * A leaf with `effectId = "dissolve"` wins over a category that says
 * `effectId = "slide"`. A leaf with `effectId = std::nullopt` inherits
 * whatever the category or baseline says.
 *
 * An explicitly-empty `effectId` (engaged optional containing empty
 * string) means "no shader effect for this event" — the event animates
 * with motion only, no transition shader. This lets a child disable a
 * parent's shader without disabling the parent's motion profile.
 */
class PHOSPHORANIMATION_EXPORT ShaderProfile
{
public:
    /// Which animation shader effect to apply. Resolved by id from
    /// `AnimationShaderRegistry`. `std::nullopt` = inherit.
    /// Empty string = explicitly no effect.
    std::optional<QString> effectId;

    /// Per-event parameter overrides for the shader. Keys are parameter
    /// ids declared in the effect's `AnimationShaderEffect::parameters`.
    /// `std::nullopt` = inherit. Engaged-but-empty map = explicitly use
    /// all defaults.
    ///
    /// When `presetId` is engaged these are DELTAS on top of the preset
    /// rather than the whole tuning: a key present here overrides the
    /// preset's value for it, and every key absent here follows the preset.
    /// That is what lets a retuned preset move an assignment that has its
    /// own edits without discarding them.
    std::optional<QVariantMap> parameters;

    /// The preset `parameters` are deltas against, resolved by id from
    /// `PhosphorShaders::ShaderPresetRegistry` against the RESOLVED effect.
    /// `std::nullopt` = inherit. Engaged-but-empty string = explicitly no
    /// preset, so `parameters` is the whole tuning.
    ///
    /// Independent of `effectId`, exactly as `parameters` already is, so a
    /// preset-only override rides the cascade instead of severing it. A
    /// presetId inherited across a node that changed the pack is INERT, not
    /// wrong: the registry keys presets by (family, packId, presetId), so a
    /// preset belonging to another pack simply does not resolve and the
    /// assignment falls back to its own parameters — the same shape a
    /// parameter id the resolved pack does not declare already has.
    std::optional<QString> presetId;

    // ─────── Effective getters ───────

    QString effectiveEffectId() const
    {
        return effectId.value_or(QString());
    }
    QVariantMap effectiveParameters() const
    {
        return parameters.value_or(QVariantMap());
    }
    QString effectivePresetId() const
    {
        return presetId.value_or(QString());
    }

    ShaderProfile withDefaults() const;

    // ─────── Serialization ───────

    static constexpr auto JsonFieldEffectId = "effectId";
    static constexpr auto JsonFieldParameters = "parameters";
    static constexpr auto JsonFieldPresetId = "presetId";

    QJsonObject toJson() const;
    static ShaderProfile fromJson(const QJsonObject& obj);

    // ─────── Overlay ───────

    /// Overlay @p src onto @p dst: every engaged field in src replaces
    /// the corresponding field in dst. Unset fields in src are skipped.
    static void overlay(ShaderProfile& dst, const ShaderProfile& src);

    // ─────── Equality ───────

    bool operator==(const ShaderProfile& other) const;
    bool operator!=(const ShaderProfile& other) const
    {
        return !(*this == other);
    }
};

/// Flatten @p profile's preset reference into its `parameters`.
///
/// Returns a copy whose `parameters` hold the preset's values overlaid with the
/// profile's own edits, and whose `presetId` is cleared to say the preset has
/// already been applied. Consumers downstream keep reading
/// `effectiveParameters()` and never have to know a preset was involved.
///
/// MUST be called AFTER the tree walk-up, never per node before it: a node can
/// carry a preset while inheriting its pack from an ancestor, and presets are
/// keyed by (family, packId, presetId), so flattening early would look the
/// preset up against an empty pack id and silently resolve nothing. Because it
/// clears the reference it applied, a second call is a no-op.
///
/// A preset id naming no preset keeps the profile's own parameters, which is the
/// look it had before it pointed at one. That covers an assignment outliving its
/// preset and an id inherited across a node that changed pack.
///
/// The surface family has had this since presets arrived; the animation family
/// did not, so the same four steps were hand-written in the daemon and the
/// compositor, and four further animation consumers never flattened at all.
/// Anything resolving an animation profile should call this rather than
/// re-deriving it.
PHOSPHORANIMATION_EXPORT ShaderProfile withPresetsResolved(const ShaderProfile& profile,
                                                           const PhosphorShaders::ShaderPresetRegistry& presets);

} // namespace PhosphorAnimationShaders
