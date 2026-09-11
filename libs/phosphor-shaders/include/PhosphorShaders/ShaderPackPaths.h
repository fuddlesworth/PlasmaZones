// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/phosphorshaders_export.h>

#include <PhosphorFsLoader/PackPathGuard.h>

#include <QDir>
#include <QString>

QT_BEGIN_NAMESPACE
class QLoggingCategory;
QT_END_NAMESPACE

namespace PhosphorShaders {

/// Resolve a pack-declared relative path against @p packDir, refusing anything
/// that escapes the pack.
///
/// **This is the path-traversal guard for every file a shader pack names**, not
/// a preset concern. The whole registry routes through it: the fragment shader,
/// the vertex shader, the implicit `zone.vert` fallback, every buffer-shader
/// entry, every image parameter's default, and every image-typed preset value.
/// It lived in `ShaderPresetParse.h` for a while, which put all pack path
/// security behind a preset-shaped include where nobody would look for it, and
/// put a future change to preset parsing in the same header as the check that
/// keeps arbitrary files off the GPU. Its own header, so both are findable as
/// what they are.
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

} // namespace PhosphorShaders
