// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/phosphorshaders_export.h>

#include <QList>
#include <QString>

namespace PhosphorShaders {

/// One declared shader parameter, in metadata declaration order, as fed to
/// `buildParamPreamble`. The generator turns a list of these into the
/// `#define p_<id> <glsl-accessor>` block both shader runtimes inject so an
/// author reads a parameter by name (`p_speed`) instead of hand-decoding a
/// `customParams[N].xyzw` lane.
/// Image-pool budget: overlay/zone image params bind to `uTexture0..3`.
/// Animation packs never use the Image pool (their textures are a separate
/// top-level list bound to `uTexture1..3`), so this cap only governs the
/// zone path. Public so the metadata parser can warn on over-budget slots
/// with the same figure the preamble generator skips at.
inline constexpr int kMaxImageSlots = 4;

struct PreambleParam
{
    /// Which UBO region the parameter occupies. The three pools number
    /// independently (a color does not consume a scalar sub-slot).
    ///   • Scalar — float / int / bool → `customParams[N].<xyzw>`
    ///   • Color  — color              → `customColors[N]`
    ///   • Image  — image/texture      → `uTexture<N>` (overlay/zone only;
    ///              animation packs declare textures separately and never
    ///              use this pool)
    enum class Pool {
        Scalar,
        Color,
        Image,
    };

    QString id;
    Pool pool = Pool::Scalar;

    /// Explicit slot from metadata, or -1 to auto-assign by declaration order
    /// within the parameter's pool. Overlay (zone) packs currently carry
    /// explicit slots; animation packs always auto-assign. The resolved slot
    /// MUST match the slot the runtime uploads the value to, or `p_<id>`
    /// reads the wrong lane — callers pass whichever the runtime uses.
    int explicitSlot = -1;
};

/// Build the generated `#define p_<id> <glsl-accessor>` preamble for a
/// shader's declared parameters.
///
/// Auto-numbers each pool independently in declaration order for params whose
/// `explicitSlot < 0`. Returns an empty string for an empty list. A param
/// whose id isn't a valid GLSL identifier body, or whose resolved slot is out
/// of range, is emitted as a `// p: skipped ...` comment rather than a broken
/// `#define`, so the block always compiles.
///
/// The returned block is newline-terminated and contains no `#version` or
/// `#line` directive — the caller splices it after the shader's `#version`
/// line and is responsible for any `#line` fixup that restores author line
/// numbers (see the include resolver's `#line` contract).
PHOSPHORSHADERS_EXPORT QString buildParamPreamble(const QList<PreambleParam>& params);

/// True if @p id is a valid GLSL identifier *body* (`[A-Za-z0-9_]`, non-empty) —
/// the `p_` prefix supplies the leading character, so a leading digit is fine.
/// Shared so the metadata parser's auto-slot assignment skips exactly the same
/// params `buildParamPreamble` skips, keeping the two lane-numberings identical.
PHOSPHORSHADERS_EXPORT bool isValidParamId(const QString& id);

/// Splice @p block into @p source immediately after its `#version` line, then
/// emit a `#line <n> 0` directive so the author's subsequent lines keep their
/// original numbers (source string 0) despite the inserted block.
///
/// @p source is expected to be **already include-expanded** — the resolver's
/// own `#line` bracketing uses the raw line positions, so injecting before
/// expansion would corrupt its math; injecting here, after expansion, composes
/// cleanly (the fixup re-anchors source string 0; the resolver's later `#line`
/// directives are untouched). @p block must be newline-terminated (as
/// `buildParamPreamble` returns). An empty block returns @p source unchanged.
/// If @p source has no `#version` line the block is prepended best-effort (such
/// source is not valid GLSL anyway).
///
/// The `#version` line is located by a line-anchored regex, which does NOT skip
/// comments: a commented-out line whose non-whitespace prefix is `#version`,
/// appearing before the real directive, would be matched. Such source is not
/// valid GLSL either (`#version` must be the first non-comment token), so no
/// well-formed pack can reach it. The compositor's
/// `ShaderInternal::injectKwinDefineAfterVersion` runs a comment-aware walk
/// instead, because the block it splices is the ABI switch itself and a pack
/// that lost it would compile as the wrong dialect rather than fail.
PHOSPHORSHADERS_EXPORT QString spliceAfterVersion(const QString& source, const QString& block);

/// The preprocessor block that selects the COMPOSITOR branch of a pack's
/// source, newline-terminated with @p eol and ready for `spliceAfterVersion`.
///
/// `#define PLASMAZONES_KWIN` is the ABI switch itself: the shared uniform
/// headers key their default-block-uniform branch off it. The two ARB
/// directives ride along because KWin 6.7's `generateCustomShader` recompiles
/// custom effect shaders at GLSL `#version 140`, where the `layout(location =
/// N)` qualifiers our vertex stages declare are illegal without them (NVIDIA
/// error C7548). They are a harmless no-op in the fragment stage.
///
/// Shared because THREE places must agree on it exactly and they are in
/// different link units: the compositor's own
/// `ShaderInternal::injectKwinDefineAfterVersion` (which wraps this with the
/// `#version`-finding walk), the offline validator's compositor bake, and the
/// GPU bake test. A private copy in any of them turns the two checkers into
/// gates that pass source the compositor would reject, or reject source it
/// accepts — the exact failure a reproduce-the-runtime gate exists to rule out.
PHOSPHORSHADERS_EXPORT QString kwinDefineBlock(const QString& eol = QStringLiteral("\n"));

} // namespace PhosphorShaders
