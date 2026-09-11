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
    /// The stored parameter map, without judgement.
    ///
    /// For the two callers that legitimately want the RAW values while a preset is
    /// still engaged: the flatten itself, which needs them as the delta set, and an
    /// editor showing the user what this assignment stores of its own. Named so
    /// those reads state their intent instead of sharing a spelling with the reads
    /// that want the effective answer.
    QVariantMap storedParameters() const
    {
        return parameters.value_or(QVariantMap());
    }

    /// The parameter map a CONSUMER should render.
    ///
    /// Identical to `storedParameters()` once the preset has been applied, and a
    /// loud warning when it has not. "Flattened" is a convention here rather than a
    /// type — the only marker is that `presetId` was reset() — so nothing in the
    /// type system stops a consumer from reading this on a RAW profile, where the
    /// answer is plausible and wrong because the preset's values are simply
    /// missing. Four of nine animation and surface consumers did exactly that, on
    /// an invariant documented in three places.
    ///
    /// So the getter says so itself, in BOTH builds. A debug-only assert would have
    /// caught none of those four in a user session, which is the whole reason they
    /// survived review. It warns rather than refusing because the result is
    /// degraded, not dangerous, and a hard failure on a render path is worse than a
    /// wrong colour.
    ///
    /// What would make this impossible rather than merely loud is moving the getter
    /// off the raw profile so a missed site fails to compile. That is a bigger
    /// change than a remediation pass should make to a library with four consumers,
    /// and it is recorded as the follow-up rather than attempted here.
    QVariantMap effectiveParameters() const
    {
        if (presetId && !presetId->isEmpty()) {
            qWarning(
                "PhosphorAnimation: ShaderProfile::effectiveParameters() read on a profile whose preset is NOT "
                "yet applied (effectId=%s presetId=%s). The preset's values are missing from the result — flatten "
                "with withPresetsResolved() after the tree walk-up, never per node.",
                qUtf8Printable(effectId.value_or(QString())), qUtf8Printable(*presetId));
        }
        return parameters.value_or(QVariantMap());
    }
    /// As with the decoration twin's `presetIdFor`, the callers are tests. It
    /// stays for symmetry with the two accessors above: every field on this
    /// value type answers the same way, and a reader who finds two of three is
    /// left wondering which spelling is the intended one.
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
