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
/// family has an `image` parameter type; animation, surface and pointer packs
/// declare textures in a separate top-level list and pass an empty set here,
/// which reduces the path branch below to a no-op for them.
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
/// back to its declared default rather than binding an arbitrary file. A preset
/// left with no values at all is omitted entirely.
///
/// ## If a non-overlay family ever gains an image parameter, read this first
///
/// Two things are correct today ONLY because the other three families pass an
/// empty @p imageParamIds, so their preset maps never hold a resolved path:
///
///  - `AnimationShaderEffect::toJson` writes its preset map out verbatim, while
///    `fromJson` re-parses every path under `AbsolutePathPolicy::Reject`. The
///    values here are ABSOLUTE once resolved, so the round trip would silently
///    drop them and `fromJson(toJson(x)) != x`.
///  - `PointerShaderRegistry::effectContentSignature` catches a preset edit
///    because a pack's presets live inside its metadata.json, which the
///    signature covers. A preset-declared TEXTURE is a different file, and only
///    DECLARED texture paths reach `effectWatchPaths` — so retuning a preset's
///    image would not re-register the pack.
///
/// Neither is reachable now, and neither is cheap to notice later: the first is
/// a silent value loss and the second a stale pack. Handle both in the same
/// change that widens the set, not afterwards.
PHOSPHORSHADERS_EXPORT PackPresets parsePackPresets(const QDir& packDir, const QSet<QString>& imageParamIds,
                                                    const QJsonObject& root, const QLoggingCategory& log);

} // namespace PhosphorShaders
