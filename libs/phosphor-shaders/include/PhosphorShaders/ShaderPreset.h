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

#include <cstddef>
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

/// How many families there are, for the one-slot-per-family arrays that index by
/// `static_cast<std::size_t>(family)`.
///
/// Declared beside the enum, and static_asserted against it, so adding a family
/// cannot leave a four-element array behind: the array grows with the constant
/// instead of silently indexing past its end. The assert is what makes the
/// coupling a compile error rather than a convention.
constexpr std::size_t ShaderFamilyCount = 4;
static_assert(static_cast<std::size_t>(ShaderFamily::Overlay) + 1 == ShaderFamilyCount,
              "ShaderFamilyCount must match the number of ShaderFamily enumerators");

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
    /// Stable id, MEANT to be unique within (family, packId) — the settings app
    /// mints a UUID for each user preset so a rename never breaks an assignment
    /// pointing at it, and a pack-declared preset uses its key in the metadata
    /// `presets` block. Nothing enforces it, though: a hand-written preset file may
    /// declare any id, or none, in which case the loader falls back to the
    /// filename stem. So an id is a UUID when the app created it and arbitrary text
    /// otherwise; `isUsableId` is what keeps the arbitrary case safe, and
    /// `setUserPresets` warns when two files claim one id.
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

    /// Longest display name `fromJson` keeps, matching the cap the settings
    /// app's rename dialog applies to a typed name. Counted in UTF-16 units,
    /// the same unit `QString::size()` and that dialog use.
    static constexpr qsizetype MaxNameChars = 128;

    /// Longest id `isUsableId` accepts. An id becomes a path component, so this
    /// also keeps a pathological one out of a filename.
    static constexpr qsizetype MaxIdChars = 256;

    /// Most parameter entries `fromJson` keeps, and the cap the settings bridge
    /// applies when it WRITES a preset. One number for both halves: the bridge
    /// carried its own copy, so a hand-written file reached resolveParams with no
    /// cap at all while a preset the app saved was bounded. A pack declares at
    /// most 48 parameters (the `parameters` maxItems in every family's metadata
    /// schema), so this is deliberately above that: an entry naming no declared
    /// parameter is inert at resolve time, and refusing a preset over it would
    /// discard the usable keys beside it.
    static constexpr qsizetype MaxParams = 64;

    /// Longest string VALUE kept in a preset's parameter map (an image path or a
    /// colour literal). Shared with the settings bridge for the same reason
    /// MaxParams is.
    static constexpr qsizetype MaxValueChars = 1024;

    /// Whether @p id is safe to use as both an identity and a path component.
    ///
    /// Rejects empty, over-long, `.`, `..`, and anything containing a path
    /// separator or a NUL. The write side builds `<dir>/<id>.json`, so an id
    /// from a hand-editable file is a traversal vector; this is the check that
    /// closes it at the parse boundary.
    static bool isUsableId(const QString& id);

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
