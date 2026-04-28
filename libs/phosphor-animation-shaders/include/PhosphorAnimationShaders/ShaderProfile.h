// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimationShaders/phosphoranimationshaders_export.h>

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

#include <optional>

namespace PhosphorAnimationShaders {

/**
 * @brief Per-event shader effect selection and configuration.
 *
 * Parallel to `PhosphorAnimation::Profile` which configures *motion*
 * (curve, duration, stagger), `ShaderProfile` configures *visual effect*
 * (which shader, with what parameters). Both use the same dot-path event
 * namespace (window.open, zone.snapIn, etc.) but are resolved through
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
class PHOSPHORANIMATIONSHADERS_EXPORT ShaderProfile
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
    std::optional<QVariantMap> parameters;

    // ─────── Effective getters ───────

    QString effectiveEffectId() const
    {
        return effectId.value_or(QString());
    }
    QVariantMap effectiveParameters() const
    {
        return parameters.value_or(QVariantMap());
    }

    ShaderProfile withDefaults() const;

    // ─────── Serialization ───────

    static constexpr auto JsonFieldEffectId = "effectId";
    static constexpr auto JsonFieldParameters = "parameters";

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

} // namespace PhosphorAnimationShaders
