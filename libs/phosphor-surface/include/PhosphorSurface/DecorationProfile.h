// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurface/phosphorsurface_export.h>

#include <PhosphorShaders/ShaderPresetRegistry.h>

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
// For qWarning / qUtf8Printable in effectiveParameters() below. Named explicitly
// rather than relied on through another Qt header: the unity build hides a missing
// include, and the non-unity build is the only place it surfaces.
#include <QMutex>
#include <QSet>
#include <QtGlobal>

#include <algorithm>
#include <optional>

namespace PhosphorSurfaceShaders {

/// Hard cap on a decoration chain's outer padding, in device-independent px.
/// EVERY consumer that composes a decoration chain clamps to this, so a runaway
/// per-pack padding request cannot inflate the render canvas without bound. Shared
/// here so they cannot drift onto different caps — and there are more of them than
/// the two this used to name: the compositor's window-decoration builder, the
/// daemon's overlay decoration builder, the settings app's decoration preview, and
/// the shell's own chrome. No count is quoted on purpose; the set grows.
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

    /// Per-pack preset selection. Shape: { packId -> presetId }, resolved by
    /// id from `PhosphorShaders::ShaderPresetRegistry` against that pack.
    /// `std::nullopt` = inherit. Engaged-but-empty map = explicitly no
    /// presets, so `parameters` is the whole tuning for every layer.
    ///
    /// Naturally pack-keyed, like `parameters`, so a chain can carry a preset
    /// on one layer and hand-tuned values on the next. Where a pack appears
    /// here, its entry in `parameters` holds DELTAS on top of the preset: a
    /// parameter present there overrides the preset's value, and every
    /// parameter absent follows the preset. An entry naming a pack the
    /// resolved chain does not contain is inert, the same as a `parameters`
    /// entry for such a pack.
    std::optional<QVariantMap> presetIds;

    // ─────── Effective getters ───────

    QStringList effectiveChain() const
    {
        return chain.value_or(QStringList());
    }
    /// The stored per-pack parameter map, without judgement.
    ///
    /// For a caller that legitimately wants the RAW values while a preset is still
    /// engaged: the flatten itself, which needs them as the delta set, and the
    /// settings controller's edit-facing reads, which show and write what this node
    /// stores of its own. Named so those reads state their intent instead of sharing
    /// a spelling with the reads that want the effective answer. Mirrors the
    /// animation twin's `storedParameters()`.
    QVariantMap storedParameters() const
    {
        return parameters.value_or(QVariantMap());
    }

    /// The parameter map a RENDERER should consume.
    ///
    /// Identical to `storedParameters()` once the presets have been applied, and a
    /// loud warning when they have not. "Flattened" is a convention here rather than
    /// a type — the only marker is that the consumed `presetIds` entries were
    /// cleared — so nothing in the type system stops a consumer from reading this on
    /// a RAW profile, where the answer is plausible and wrong because the presets'
    /// values are simply missing and the pack's declared min/max is unenforced.
    ///
    /// The animation twin grew this warning first, and its absence here is exactly
    /// why the shell's chrome went unflattened unnoticed: every OTHER surface
    /// consumer happened to flatten, so nothing pointed at the one that did not.
    ///
    /// In BOTH builds, for the reason the twin gives: a debug-only assert would have
    /// caught none of those sites in a user session. It warns rather than refusing
    /// because the result is degraded, not dangerous.
    ///
    /// Only a NON-EMPTY preset id counts as unapplied. An empty-string entry is the
    /// blocking sentinel ("this pack follows no preset"), which the flatten
    /// deliberately PRESERVES, so warning on it would fire on every correctly
    /// flattened profile that carries one.
    QVariantMap effectiveParameters() const
    {
        // ONCE per (pack, presetId). This getter is read from the compositor's decorate
        // path and the shell's chain resolve, so an unflattened consumer would repeat the
        // warning every frame it draws. The animation twin latches the same way, and for
        // the reason the decoration parser nearby chose qCDebug: the level is right for a
        // contract violation, the repetition is not.
        if (presetIds) {
            for (auto it = presetIds->constBegin(); it != presetIds->constEnd(); ++it) {
                if (it.value().toString().isEmpty()) {
                    continue;
                }
                // Guarded: this getter is const on a value type whose class doc says it is not
                // internally synchronized, and contains()+insert() on a shared QSet is not
                // atomic (only the static's INITIALISATION is). The set is bounded by the
                // user's own config, so growth is not the concern; a torn read is.
                //
                // The latch is process-wide, which is why no test asserts "warns exactly
                // once": a slot doing that would pass or fail on slot ORDER within the
                // binary. A test that wants to see the warning has to use a (pack, preset)
                // pair no earlier slot has touched, which the key makes possible.
                static QMutex latchMutex;
                static QSet<QString> reported;
                const QMutexLocker latchLock(&latchMutex);
                const QString key = it.key() + QLatin1Char('\x1f') + it.value().toString();
                if (!reported.contains(key)) {
                    reported.insert(key);
                    qWarning(
                        "PhosphorSurface: DecorationProfile::effectiveParameters() read on a profile whose preset "
                        "is NOT yet applied (pack=%s presetId=%s). The preset's values are missing from the result "
                        "and its declared range is unenforced — flatten with withPresetsResolved() after the tree "
                        "walk-up, never per node. Reported once per pack and preset.",
                        qUtf8Printable(it.key()), qUtf8Printable(it.value().toString()));
                }
                break;
            }
        }
        return parameters.value_or(QVariantMap());
    }
    QStringList effectiveDisabledPacks() const
    {
        return disabledPacks.value_or(QStringList());
    }
    QVariantMap effectivePresetIds() const
    {
        return presetIds.value_or(QVariantMap());
    }
    /// The preset chosen for @p packId, or an empty string when that layer
    /// uses none.
    ///
    /// Only tests call this today, and it stays anyway: it is one of the
    /// `effective*` family every field on this value type carries, and the
    /// alternative is tests reading `presetIds->value(...).toString()` by hand,
    /// which re-spells the nullopt case at each site. Not dead code to prune.
    QString presetIdFor(const QString& packId) const
    {
        return presetIds ? presetIds->value(packId).toString() : QString();
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
    static constexpr auto JsonFieldPresetIds = "presetIds";

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

/// Flatten @p profile's per-pack preset references into its `parameters`.
///
/// Returns a copy whose `parameters` hold, for every pack that named a preset,
/// the preset's values overlaid with that pack's own edits — and whose
/// `presetIds` is cleared to say the presets have already been applied.
/// Consumers downstream therefore keep reading `effectiveParameters()` and
/// never have to know a preset was involved.
///
/// @p family selects the preset namespace, because a decoration chain is
/// resolved for two different pack families: `surface` for the window and
/// popup surfaces, `pointer` for the cursor chain. Passing the wrong one
/// resolves nothing rather than resolving the wrong thing, since the registry
/// keys presets by (family, packId, presetId).
///
/// A pack whose preset id names no preset keeps its own parameters, which is
/// the look it had before it pointed at one. That covers an assignment
/// outliving its preset and a presetIds entry for a pack the resolved chain no
/// longer contains.
PHOSPHORSURFACE_EXPORT DecorationProfile withPresetsResolved(const DecorationProfile& profile,
                                                             const PhosphorShaders::ShaderPresetRegistry& presets,
                                                             PhosphorShaders::ShaderFamily family);

} // namespace PhosphorSurfaceShaders
