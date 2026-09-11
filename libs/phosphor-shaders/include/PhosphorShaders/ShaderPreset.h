// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/phosphorshaders_export.h>

#include <QHash>
#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QStringView>
#include <QVariant>
#include <QVariantMap>

#include <optional>

namespace PhosphorShaders {

/// Which shader family a preset belongs to.
///
/// The four families keep separate pack-id namespaces, so a preset is only
/// ever offered for the family it was written against. The wire token doubles
/// as the subdirectory name under the user preset root, which is what tells the
/// loader which family a preset file belongs to — the file itself carries only
/// its pack id, so moving one between family directories re-homes it.
enum class ShaderFamily {
    Animation,
    Surface,
    Pointer,
    Overlay,
};

/// Wire token for @p family: "animation" / "surface" / "pointer" / "overlay".
PHOSPHORSHADERS_EXPORT QLatin1StringView shaderFamilyToken(ShaderFamily family);

/// A pack's declared numeric range for one parameter: `{ min, max }`, either of
/// which may be an invalid QVariant when the pack declares only one side.
using PresetValueRange = QPair<QVariant, QVariant>;

/// The declared ranges for a pack's parameters, by parameter id.
///
/// Preset values are clamped to these before they reach a consumer. A pack's
/// declared `min`/`max` used to be enforced only by the settings slider, so a
/// hand-written preset file could put any number into a uniform — and at least
/// six bundled overlay packs derive a GLSL loop bound from one
/// (`int octaves = int(p_octaves ...)` feeding `for (i < octaves)`), which makes
/// an out-of-range value a GPU stall rather than a cosmetic mistake.
using PresetValueBounds = QHash<QString, PresetValueRange>;

/// Collect `PresetValueBounds` from any family's declared parameter list.
///
/// A template because the four families each have their own `ParameterInfo`
/// type in their own library; all four carry `id`, `minValue` and `maxValue`,
/// which is all this needs. Lets a caller seeding presets pass the bounds in
/// the same breath without this library depending on four pack registries.
template<typename ParameterList>
PresetValueBounds presetBoundsFrom(const ParameterList& parameters)
{
    PresetValueBounds bounds;
    for (const auto& parameter : parameters) {
        if (parameter.minValue.isValid() || parameter.maxValue.isValid()) {
            bounds.insert(parameter.id, PresetValueRange(parameter.minValue, parameter.maxValue));
        }
    }
    return bounds;
}

/// Inverse of `shaderFamilyToken`, or `std::nullopt` for an unknown token.
/// A preset file naming a family this build does not know is skipped rather
/// than guessed at.
PHOSPHORSHADERS_EXPORT std::optional<ShaderFamily> shaderFamilyFromToken(QStringView token);

/**
 * @brief One named parameter tuning for one shader pack.
 *
 * Two provenances share this type and one id namespace:
 *
 *   • **Pack-declared** — the `presets` block of a pack's `metadata.json`.
 *     Read-only: the pack author ships them and an update replaces them.
 *     The id is the preset's key in that block.
 *   • **User** — a JSON file under the user preset root. Editable, and the
 *     thing an assignment normally points at.
 *
 * An assignment stores `presetId` plus only the parameters the user changed
 * afterwards, so the effective parameter map is `params` overlaid with those
 * deltas. That is what makes editing a preset move every assignment bound to
 * it: the assignment holds a reference, never a copy.
 */
struct PHOSPHORSHADERS_EXPORT ShaderPreset
{
    /// Stable id, unique within (family, packId). A user preset uses a UUID
    /// so a rename never breaks an assignment that points at it; a
    /// pack-declared preset uses its key in the metadata `presets` block.
    QString id;

    /// Display name. Free-form, and NOT an identity — two presets may share
    /// one, and renaming changes nothing an assignment depends on.
    QString name;

    /// The pack this preset tunes. A preset is only offered for its own pack:
    /// parameter ids mean nothing across packs.
    QString packId;

    /// The tuning itself: { paramId -> value }. Names a subset of the pack's
    /// declared parameters; ids the pack does not declare are inert at resolve
    /// time.
    QVariantMap params;

    /// True for a pack-declared preset. Such a preset can be duplicated into a
    /// user preset but never edited or deleted in place — it belongs to the
    /// pack, and the next pack update would overwrite the edit anyway.
    bool readOnly = false;

    /// Absolute path of the file backing a user preset. Empty for a
    /// pack-declared one, which has no file of its own.
    QString sourcePath;

    bool isValid() const
    {
        return !id.isEmpty() && !packId.isEmpty();
    }

    /// Serialise a USER preset. Pack-declared presets are written by their
    /// pack's `metadata.json` and never round-trip through here.
    QJsonObject toJson() const;

    /// Parse a user preset file body. Returns an invalid preset (empty `id`)
    /// when the object is malformed; the caller logs and skips.
    ///
    /// @p fallbackId names the preset when the file omits `id`, so a
    /// hand-written file dropped into the preset directory still loads with a
    /// stable identity (its filename stem).
    static ShaderPreset fromJson(const QJsonObject& obj, const QString& fallbackId = QString());

    bool operator==(const ShaderPreset& other) const;
    bool operator!=(const ShaderPreset& other) const
    {
        return !(*this == other);
    }
};

/// Overlay @p deltas onto @p base and return the result — the one definition
/// of how a preset and an assignment's own parameter edits combine.
///
/// Every consumer resolves through this, so "the preset supplies the value
/// unless this assignment overrode it" means the same thing in the daemon, the
/// compositor and the settings preview. A key present in @p deltas wins even
/// when its value equals the preset's; sameness is not a reason to drop an
/// explicit override, because the preset may change later and the user's
/// choice must not silently start following it.
PHOSPHORSHADERS_EXPORT QVariantMap overlayPresetDeltas(const QVariantMap& base, const QVariantMap& deltas);

} // namespace PhosphorShaders
