// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurface/phosphorsurface_export.h>

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <optional>

namespace PhosphorSurfaceShaders {

/// Hard cap on a decoration chain's outer padding, in device-independent px.
/// Both decoration composers clamp to this — the compositor's window-decoration
/// builder and the daemon's overlay decoration builder — so a runaway per-pack
/// padding request can't inflate the render canvas without bound. Shared here so
/// the two binaries cannot drift onto different caps.
inline constexpr int kMaxDecorationOuterPaddingPx = 128;

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

    /// Pack ids from `chain` that are toggled OFF (the per-layer disable
    /// toggle — same semantics as a rule's `enabled` flag, inverted so an
    /// absent field means "everything on"). A disabled pack stays in the
    /// chain with its parameters intact; only `enabledChain()` filters it,
    /// so re-enabling restores the exact prior look. `std::nullopt` =
    /// inherit. Engaged-but-empty list = explicitly nothing disabled.
    std::optional<QStringList> disabledPacks;

    // ─────── Effective getters ───────

    QStringList effectiveChain() const
    {
        return chain.value_or(QStringList());
    }
    QVariantMap effectiveParameters() const
    {
        return parameters.value_or(QVariantMap());
    }
    QStringList effectiveDisabledPacks() const
    {
        return disabledPacks.value_or(QStringList());
    }

    /// The chain the RENDERERS consume: `effectiveChain()` minus the disabled
    /// packs. Edit-facing consumers (the settings chain editor) keep reading
    /// `effectiveChain()` so a disabled layer still shows in the list.
    QStringList enabledChain() const
    {
        QStringList out = effectiveChain();
        if (disabledPacks && !disabledPacks->isEmpty()) {
            out.erase(std::remove_if(out.begin(), out.end(),
                                     [this](const QString& id) {
                                         return disabledPacks->contains(id);
                                     }),
                      out.end());
        }
        return out;
    }

    DecorationProfile withDefaults() const;

    // ─────── Serialization ───────

    static constexpr auto JsonFieldChain = "chain";
    static constexpr auto JsonFieldParameters = "parameters";
    static constexpr auto JsonFieldDisabledPacks = "disabledPacks";

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
