// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/phosphorshaders_export.h>

// For resolveWithinPack, which parsePackPresets runs every image-typed preset
// value through. It is the guard for EVERY file a pack names, not a preset
// concern, so it lives in its own header rather than here.
#include <PhosphorShaders/ShaderPackPaths.h>

#include <QDir>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVariantMap>

QT_BEGIN_NAMESPACE
class QLoggingCategory;
QT_END_NAMESPACE

namespace PhosphorShaders {

/// Pack-declared named parameter presets: preset name → { paramId → value }.
///
/// The same shape for every shader family. A preset names a subset of the
/// pack's declared parameters; ids the pack does not declare are inert at
/// resolve time, so parsing does not reject them (the offline pack validator
/// is where an author hears about a typo).
using PackPresets = QMap<QString, QVariantMap>;

/// Parse the `presets` object of a pack metadata @p root.
///
/// @p imageParamIds names the pack's image-typed parameters, whose preset
/// values are pack-declared PATHS rather than plain values. Only the overlay
/// family has a SUPPORTED `image` parameter type, and the other three carry
/// textures in a separate top-level list — but none of them passes a statically
/// empty set here. All four derive this set from their declared parameter types,
/// and `type` is read raw from a hand-editable metadata.json with no enum
/// validation, so a pack writing `"type": "image"` under any family turns the path
/// branch below on. That is why the branch is not a no-op anywhere and why the
/// empty-pack-directory refusal in the implementation is a fail-closed guard
/// rather than a formality.
///
/// Image-typed preset values must be containment-checked at PARSE time with the
/// Reject policy, exactly like an image param's `default`. They cannot be
/// trusted at translate time: a preset reaches `translateParamsToUniforms`
/// through `storedParams` (the user picked the preset), so the provenance
/// heuristic there (`storedParams.contains(id)` → Trust) would wave a
/// pack-manufactured `"tex": "/home/user/.ssh/id_rsa"` straight through — the
/// very escape the `default` path already closes. Gate it here, where the
/// value's true (pack) provenance is known.
///
/// A refused image value is DROPPED from the preset, so that parameter falls
/// back to its declared default rather than binding an arbitrary file.
///
/// An author-declared empty preset (`"Default": {}`) is KEPT: it legitimately
/// means "this preset is the pack's declared defaults", and dropping it also hid
/// it from the offline validator, which lints the parsed map. A preset left empty
/// only because every one of its values was refused IS omitted, because it cannot
/// do what it says. The implementation states that rule beside the test that
/// distinguishes the two.
///
/// ## If a non-overlay family ever SUPPORTS an image parameter, read this first
///
/// Two things are correct today ONLY because no non-overlay family binds a preset
/// image: the overlay family is the one whose loader honours the type, and the
/// other three either refuse it (pointer) or reach this function with a pack
/// directory the fail-closed guard rejects.
///
///  - `AnimationShaderEffect::toJson` writes its preset map out verbatim, while
///    `fromJson` re-parses every path under `AbsolutePathPolicy::Reject`. The
///    values here are ABSOLUTE once resolved, so the round trip would silently
///    drop them and `fromJson(toJson(x)) != x`.
///  - `PointerShaderRegistry::effectContentSignature` does catch a preset being
///    RETUNED, because a pack's presets live inside its metadata.json and the
///    signature hashes that file. What it cannot catch is an edit to the image
///    FILE a preset names: only DECLARED texture paths reach `effectWatchPaths`,
///    so repainting a preset-declared texture neither re-registers nor reloads
///    the pack.
///
/// Neither is reachable now, and neither is cheap to notice later: the first is
/// a silent value loss and the second a stale pack. Handle both in the same
/// change that widens the set, not afterwards.
PHOSPHORSHADERS_EXPORT PackPresets parsePackPresets(const QDir& packDir, const QSet<QString>& imageParamIds,
                                                    const QJsonObject& root, const QLoggingCategory& log);

} // namespace PhosphorShaders
