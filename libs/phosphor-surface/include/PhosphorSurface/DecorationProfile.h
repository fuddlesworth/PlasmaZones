// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurface/phosphorsurface_export.h>

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

namespace PhosphorSurfaceShaders {

/**
 * @brief Per-surface decoration shader-pack chain and its per-pack parameters.
 *
 * Parallel to `PhosphorAnimationShaders::ShaderProfile` which configures a
 * single transition *effect* per event, `DecorationProfile` configures the
 * persistent *decoration* of a surface: an ordered chain of surface shader
 * packs (border, glow, …) plus the per-pack parameters that style them.
 * Border width, radius and colour are NOT decoration fields of their own —
 * they are the `border` pack's parameters, carried in `parameters`. Both are
 * resolved through a separate tree (`DecorationProfileTree`) over a dot-path
 * surface namespace (window.tiled, popup.snapAssist, …) so the two concerns
 * evolve independently.
 *
 * ## "Set" vs "unset" semantics
 *
 * Same contract as `ShaderProfile`: `std::optional` fields distinguish
 * "inherit from parent" (nullopt) from "I explicitly chose this value"
 * (engaged). A leaf with an engaged `chain` wins over a category that sets
 * one. A leaf with `chain = std::nullopt` inherits whatever the category or
 * baseline says.
 *
 * An explicitly-empty `chain` (engaged optional containing an empty list)
 * means "no decoration shader packs for this surface" — the surface runs no
 * surface shader. This lets a child disable a parent's pack chain.
 */
class PHOSPHORSURFACE_EXPORT DecorationProfile
{
public:
    /// Ordered surface-pack ids, resolved by id from `SurfaceShaderRegistry`,
    /// e.g. {"border", "glow"}. `std::nullopt` = inherit. Engaged-but-empty
    /// list = explicitly no packs.
    std::optional<QStringList> chain;

    /// Per-pack parameter overrides. Shape: { packId -> { paramId -> value } }.
    /// Keys are pack ids from `chain`; inner keys are parameter ids declared
    /// in the pack's `SurfaceShaderEffect::parameters`. `std::nullopt` =
    /// inherit. Engaged-but-empty map = explicitly use all defaults.
    std::optional<QVariantMap> parameters;

    // ─────── Effective getters ───────

    QStringList effectiveChain() const
    {
        return chain.value_or(QStringList());
    }
    QVariantMap effectiveParameters() const
    {
        return parameters.value_or(QVariantMap());
    }

    DecorationProfile withDefaults() const;

    // ─────── Serialization ───────

    static constexpr auto JsonFieldChain = "chain";
    static constexpr auto JsonFieldParameters = "parameters";

    QJsonObject toJson() const;
    static DecorationProfile fromJson(const QJsonObject& obj);

    // ─────── Overlay ───────

    /// Overlay @p src onto @p dst: every engaged field in src replaces
    /// the corresponding field in dst. Unset fields in src are skipped.
    static void overlay(DecorationProfile& dst, const DecorationProfile& src);

    // ─────── Equality ───────

    bool operator==(const DecorationProfile& other) const;
    bool operator!=(const DecorationProfile& other) const
    {
        return !(*this == other);
    }
};

} // namespace PhosphorSurfaceShaders
