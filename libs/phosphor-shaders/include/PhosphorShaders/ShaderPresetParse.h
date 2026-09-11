// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/phosphorshaders_export.h>

#include <PhosphorFsLoader/PackPathGuard.h>

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

/// Resolve a pack-declared relative path against @p packDir, refusing anything
/// that escapes the pack.
///
/// Delegates to `PhosphorFsLoader::resolveWithinDirectory` rather than
/// hand-rolling a lexical check: a lexical-only check misses a symlink inside
/// the pack pointing out of it, and mixing canonical with lexical fails open.
/// Subdirectories INSIDE the pack stay legal (`"shaders/effect.frag"`), because
/// containment is checked on the resolved canonical path rather than by
/// refusing separators. A name that does not exist yet resolves lexically, so a
/// pack referencing a file it does not ship is rejected later by the existence
/// checks rather than here.
///
/// @p policy is REQUIRED here, deliberately — there is no default on this
/// declaration, so every family has to state its own. `Reject` is right for
/// everything a PACK FILE declares: a pack ships its own assets, so an absolute
/// path can only be a mistake or an escape. Only a value the USER supplied at
/// runtime (a file picker, D-Bus) may pass `Trust`.
///
/// Returns an empty string when the path is refused, and when @p declaredName
/// is itself empty — an empty declared name is ABSENT, not an escape, and
/// warning about it points the pack author at the wrong problem.
PHOSPHORSHADERS_EXPORT QString resolveWithinPack(const QDir& packDir, const QString& declaredName,
                                                 PhosphorFsLoader::AbsolutePathPolicy policy,
                                                 const QLoggingCategory& log);

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
PHOSPHORSHADERS_EXPORT PackPresets parsePackPresets(const QDir& packDir, const QSet<QString>& imageParamIds,
                                                    const QJsonObject& root, const QLoggingCategory& log);

} // namespace PhosphorShaders
