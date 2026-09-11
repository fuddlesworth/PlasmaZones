// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QHash>
#include <QJsonObject>
// Forward-declared rather than included: only a const reference to it appears in
// this header (withPresetsResolved), so the definition is a .cpp concern.
namespace PhosphorShaders {
class ShaderPresetRegistry;
}

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace PlasmaZones {

/**
 * @brief One zone-overlay shader selection: which pack, with what parameters.
 *
 * The overlay analogue of PhosphorAnimationShaders::ShaderProfile, but
 * without optional fields: an override node replaces the baseline wholly
 * (id AND params together), so there is no per-field inherit to encode.
 * An empty shaderId means "no shader" — as the baseline that is simply
 * the unset default, as an override it explicitly suppresses a baseline
 * shader for that layout.
 */
class PLASMAZONES_EXPORT OverlayShaderProfile
{
public:
    QString shaderId;

    /// The shader's tuning. When `presetId` is set these are DELTAS on top of
    /// the preset: a parameter present here overrides the preset's value for
    /// it, and every parameter absent here follows the preset. That is what
    /// lets a retuned preset move this assignment without discarding the
    /// edits made on top of it.
    QVariantMap parameters;

    /// The preset `parameters` are deltas against, resolved by id from
    /// `PhosphorShaders::ShaderPresetRegistry` against `shaderId`. Empty
    /// means no preset, so `parameters` is the whole tuning.
    ///
    /// A preset id naming no preset for `shaderId` is inert rather than
    /// wrong — the registry misses and the assignment falls back to its own
    /// parameters, which is the look it would have had with no preset at all.
    /// That is what an assignment outliving its preset degrades to.
    ///
    /// The default member initializer is load-bearing rather than decorative:
    /// this is an aggregate, and a good many call sites brace-initialize it
    /// positionally as `{shaderId, parameters}`. Without the initializer every
    /// one of those becomes a -Wmissing-field-initializers warning, and making
    /// them all spell out an empty third field would be churn for a value that
    /// already means exactly what they intend — no preset.
    QString presetId{};

    bool isEmpty() const
    {
        return shaderId.isEmpty() && parameters.isEmpty() && presetId.isEmpty();
    }

    static constexpr auto JsonFieldShaderId = "shaderId";
    static constexpr auto JsonFieldParameters = "parameters";
    static constexpr auto JsonFieldPresetId = "presetId";

    QJsonObject toJson() const;
    static OverlayShaderProfile fromJson(const QJsonObject& obj);

    bool operator==(const OverlayShaderProfile& other) const
    {
        // JSON-normalized parameter compare: a parameter map built in C++
        // (int variants) must compare equal to the same values read back
        // from disk (doubles), or a re-applied identical assignment would
        // defeat the settings setters' value-equality no-op gates.
        return shaderId == other.shaderId && presetId == other.presetId
            && QJsonObject::fromVariantMap(parameters) == QJsonObject::fromVariantMap(other.parameters);
    }
    bool operator!=(const OverlayShaderProfile& other) const
    {
        return !(*this == other);
    }
};

/**
 * @brief Zone-overlay shader assignments: a global baseline plus
 *        per-layout overrides.
 *
 * The overlay counterpart of the animation ShaderProfileTree and the
 * decoration DecorationProfileTree, but FLAT: paths are layout UUIDs
 * (braced `QUuid::toString()` form, per the project convention), not a
 * dot-path hierarchy, and the only inheritance step is override →
 * baseline. resolve() returns the layout's override when one exists,
 * otherwise the baseline.
 *
 * Persisted as one nested JSON entry under
 * `Overlays/OverlayShaderTree`:
 * `{ "baseline": {node}, "overrides": { "{uuid}": {node} } }`.
 *
 * Value type, not internally synchronized. Same as the sibling trees.
 *
 * ## Why there are four of these, and what should be shared
 *
 * Recorded here because this audit found the four trees had already diverged in
 * ways nobody intended, and the next person to add a fifth should know which half
 * of the duplication is deliberate.
 *
 * One fully GENERIC tree would be the wrong target. The four genuinely differ in
 * key space (layout UUIDs here, dot-paths there), inheritance model (one step here,
 * a full walk-up there), payload shape and parse dependency — and collapsing them
 * would force optionals onto this type, which deliberately has none, or cost the
 * other two their per-field inheritance.
 *
 * The CONTAINER is a different matter, and it is written out three times:
 * setOverride / clearOverride / overriddenPaths, the insertion-order bookkeeping,
 * the toJson array loop, the order-sensitive equality, the empty-path guard. Those
 * copies HAVE drifted: this type dropped its insertion-order list entirely in
 * 46ed8cdef while the other two kept theirs, it normalises keys at the schema
 * while the decoration tree normalises inside fromJson, and the empty-path guard
 * exists three times over. A `PathKeyedTree<Payload>` carrying baseline,
 * overrides, insertion order, serialisation and equality — with the payload
 * supplying only fromJson / toJson / bounded() — is the abstraction all four
 * re-derive.
 *
 * The lesson of this audit's sanitizer work is the reason it is worth doing: the
 * bound CONSTANTS were lifted into a shared header while four hand-written
 * traversals stayed, and the SHAPE rules diverged anyway. The numbers were never
 * the hard part.
 */
class PLASMAZONES_EXPORT OverlayShaderTree
{
public:
    OverlayShaderTree() = default;

    // ─────── Lookup ───────

    /// Override for @p layoutId when present, else the baseline.
    OverlayShaderProfile resolve(const QString& layoutId) const;
    OverlayShaderProfile directOverride(const QString& layoutId) const;
    bool hasOverride(const QString& layoutId) const;
    /// Overridden layout ids in sorted order — deterministic and identical
    /// whether the tree was built in-session or reloaded from JSON (whose
    /// object keys are sorted), so no consumer can come to depend on an
    /// order that does not survive a restart.
    QStringList overriddenLayouts() const;
    bool isEmpty() const;

    // ─────── Mutation ───────

    void setOverride(const QString& layoutId, const OverlayShaderProfile& profile);
    bool clearOverride(const QString& layoutId);

    // ─────── Baseline ───────

    OverlayShaderProfile baseline() const
    {
        return m_baseline;
    }
    void setBaseline(const OverlayShaderProfile& profile);

    // ─────── Serialization ───────

    static constexpr auto JsonFieldBaseline = "baseline";
    static constexpr auto JsonFieldOverrides = "overrides";

    QJsonObject toJson() const;
    static OverlayShaderTree fromJson(const QJsonObject& obj);

    // ─────── Equality ───────

    bool operator==(const OverlayShaderTree& other) const;
    bool operator!=(const OverlayShaderTree& other) const
    {
        return !(*this == other);
    }

private:
    OverlayShaderProfile m_baseline;
    QHash<QString, OverlayShaderProfile> m_overrides;
};

/// Flatten @p profile's preset reference into its `parameters`.
///
/// Returns a copy whose `parameters` are the EFFECTIVE tuning (the preset's
/// values with this assignment's own edits laid over the top, clamped to the
/// pack's declared ranges) and whose `presetId` is cleared to say the preset has
/// already been applied. Consumers keep reading `parameters` and never have to
/// know a preset was involved.
///
/// The overlay twin of `PhosphorAnimationShaders::withPresetsResolved` and
/// `PhosphorSurfaceShaders::withPresetsResolved`, and it lives HERE, beside the
/// type, for the reason they do: the flatten is a property of the profile, not of
/// whichever service happens to resolve one. OverlayService had it open-coded at
/// two call sites, which made the service carry preset-resolution knowledge for
/// three of four families — growing a class its own file-size exception note
/// concedes is already under god-object pressure — and meant the rule route and
/// the tree route each had their own copy of the same three lines.
///
/// A profile naming no preset, and a @p presets that resolves it to nothing, both
/// come back with `parameters` exactly as stored. That is the documented miss
/// behaviour: an assignment can outlive the preset it points at, and it then
/// renders the way it did before it pointed at one.
PLASMAZONES_EXPORT OverlayShaderProfile withPresetsResolved(const OverlayShaderProfile& profile,
                                                            const PhosphorShaders::ShaderPresetRegistry& presets);

} // namespace PlasmaZones
