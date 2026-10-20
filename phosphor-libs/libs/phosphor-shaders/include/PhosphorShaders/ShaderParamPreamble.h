// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/phosphorshaders_export.h>

#include <QList>
#include <QString>

namespace PhosphorShaders {

/// Image-pool budget: overlay/zone image params bind to `uTexture0..3`.
/// Animation packs never use the Image pool (their textures are a separate
/// top-level list bound to `uTexture1..3`), so this cap only governs the
/// zone path. Public so the metadata parser can warn on over-budget slots
/// with the same figure the preamble generator skips at.
inline constexpr int kMaxImageSlots = 4;

/// One declared shader parameter, in metadata declaration order, as fed to
/// `buildParamPreamble`. The generator turns a list of these into the
/// `#define p_<id> <glsl-accessor>` block both shader runtimes inject so an
/// author reads a parameter by name (`p_speed`) instead of hand-decoding a
/// `customParams[N].xyzw` lane.
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

/// True if `p_<id>` would COLLIDE with a `#define p_...` a shared family header
/// already owns.
///
/// isValidParamId checks only the character set, so an id like `reversed` is a
/// perfectly legal identifier that buildParamPreamble will happily turn into
/// `#define p_reversed <slot>` — while plasmazones/data/animations/shared/animation_uniforms.glsl
/// already carries `#define p_reversed (iIsReversed == 1)`. Redefining a macro with a
/// different replacement list is an error in GLSL, so such a pack fails to compile
/// with the diagnostic pointing at the SHARED HEADER rather than at the pack's own
/// metadata, which is the wrong place to send an author.
///
/// The reservation is per family, because a header only reserves a name for the
/// packs that include it. Only the animation family reserves anything today; the
/// list lives here, beside the generator that creates the clash, so a family that
/// adds a `#define p_...` has one place to declare it.
PHOSPHORSHADERS_EXPORT bool isReservedAnimationParamId(const QString& id);

/// Splice @p block into @p source immediately after its `#version` line, then
/// emit a `lineDirective` re-anchoring source string 0 so the author's
/// subsequent lines keep their original numbers despite the inserted block.
///
/// @p source is expected to be **already include-expanded**. A block may carry
/// an `#include` only the compositor can resolve (`kwinFinalizeColorBlock`
/// includes a KWin resource), which the phosphor resolver would fail on, so
/// splicing before expansion is not an option for every caller. After
/// expansion it composes cleanly: the fixup re-anchors source string 0 and
/// the resolver's `#line` directives are untouched. @p block must be newline-terminated (as
/// `buildParamPreamble` returns). An empty block returns @p source unchanged.
/// If @p source has no `#version` line the block is prepended best-effort (such
/// source is not valid GLSL anyway).
///
/// The `#version` line is located by `versionDirectiveEnd`, so a `#version`
/// inside a comment is never taken for the directive. A leading BOM is
/// stripped once whether or not @p block is empty, so the returned source
/// never carries it. The newline and directive this function emits follow the
/// source's own convention (CRLF when it contains any). @p block itself is
/// spliced verbatim, so a CRLF source given an LF block comes back mixed,
/// which every GLSL compiler accepts.
PHOSPHORSHADERS_EXPORT QString spliceAfterVersion(const QString& source, const QString& block);

/// Locate the first REAL `#version` directive of @p source: the first line
/// whose first non-whitespace token is `#version` outside any `//` or
/// `/* … */` comment, with a leading UTF-8 BOM (U+FEFF) skipped. Returns the
/// index of the newline that terminates that line, `source.size()` when the
/// directive is the last line and has no trailing newline, or -1 when there
/// is no directive at all.
///
/// The `#version` finder every SPLICE shares: `spliceAfterVersion` above (the
/// daemon's preview bake, the validator and the compositor's preamble
/// splices) and `kwinCompositorSource`, which the compositor's own
/// `ShaderInternal::injectKwinDefineAfterVersion` wraps. Two finders that
/// disagreed on a comment or a BOM put the preamble and the ABI define on
/// different sides of the directive, which is a compile failure on the
/// compositor only.
///
/// It is more permissive than KWin's own finder, which `kwinGlesDialect` and
/// `kwinDesktopDialect` reproduce on purpose: KWin drops only a line that
/// STARTS with `#version`, untrimmed and with no comment awareness. So an
/// indented `#version`, or one after a same-line comment, is found here and
/// survives KWin's rewrite, where it then fails as a second directive. The
/// validator's compositor arms compile through those dialects and report it.
///
/// Unsupported shapes, all rejected rather than guessed at:
///   • `# version` (whitespace after the `#`) is not recognised, and KWin
///     would not drop it either.
///   • A file whose lines end in a bare CR is one line here.
///   • A `#version` line that opens a block comment it does not close returns
///     -1. Splicing after it would land the block inside the comment, where
///     the ABI define and every `p_<id>` silently vanish, so the caller's
///     no-directive path runs instead and the compile fails loudly.
PHOSPHORSHADERS_EXPORT int versionDirectiveEnd(const QString& source);

/// The preprocessor block that selects the COMPOSITOR branch of a pack's
/// source, newline-terminated with @p eol and ready for `spliceAfterVersion`.
///
/// `#define PLASMAZONES_KWIN` is the ABI switch itself: the shared uniform
/// headers key their default-block-uniform branch off it.
///
/// KWin picks the dialect from the GL context, not from its own version. On a
/// desktop GL context KWin 6.7 recompiles custom shaders as desktop GLSL 140,
/// where the `layout(location = N)` qualifiers left on vertex inputs and
/// fragment outputs need GL_ARB_explicit_attrib_location (NVIDIA error C7548).
/// GL_ARB_separate_shader_objects stays enabled there as well. The qualifiers
/// on varyings are gone by then (see `stripVaryingLocations`), so it only
/// backs up a varying declaration the strip does not recognise, which 6.7
/// accepted before the strip existed. On a GLES 3 context, which is every
/// context on KWin 6.8, KWin compiles GLSL ES 300, where the remaining
/// qualifiers are built in and KWin's precision declarations come before this
/// block. The desktop extensions are guarded with `#ifndef GL_ES`, because an
/// `#extension` after those precision declarations is rejected by Mesa
/// ("#extension directive is not allowed in the middle of a shader") and by
/// NVIDIA (error C7621).
///
/// Shared because the compositor's injector (through `kwinCompositorSource`),
/// the offline validator's compositor bake and the GPU bake tests must agree
/// on it exactly, and they are in different link units. A private copy in any
/// of them turns the checkers into gates that pass source the compositor would
/// reject, or reject source it accepts — the exact failure a
/// reproduce-the-runtime gate exists to rule out.
PHOSPHORSHADERS_EXPORT QString kwinDefineBlock(const QString& eol = QStringLiteral("\n"));

/// The pipeline stage a source is compiled for, as `stripVaryingLocations`
/// needs to tell a varying from a vertex input or a fragment output.
enum class ShaderStage {
    Vertex,
    Fragment,
};

/// Remove the `layout(location = N)` qualifier from every varying of @p source:
/// the `out` declarations of a vertex stage and the `in` declarations of a
/// fragment stage. Vertex inputs and fragment outputs keep theirs.
///
/// The compositor branch of a pack must not carry them. On a GLES 3 context,
/// which is every context on KWin 6.8, KWin compiles custom shaders as GLSL ES
/// 300, which allows a location only on vertex inputs and fragment outputs. A
/// location on a varying needs ES 310 or GL_EXT_separate_shader_objects, and
/// that extension cannot be enabled because KWin's precision declarations
/// precede anything a shader splices in. Mesa's GLSL compiler, which every Mesa
/// driver shares, rejects the source outright ("shader output explicit location
/// requires GL_EXT_separate_shader_objects extension or GLSL ES 3.10"), while
/// the proprietary NVIDIA driver accepts it. The qualifier is redundant there
/// anyway: KWin links both stages into one program, which matches varyings by
/// name. That makes name matching the contract on every KWin version, so a
/// pack's vertex outputs and fragment inputs must share names and types.
///
/// The daemon branch keeps the qualifiers, because qsb's SPIR-V target requires
/// them, so the pack files declare them and this runs only on the compositor
/// path: `kwinCompositorSource` (the compositor's injector, the offline
/// validator's compositor bake and the GPU bake tests all go through it). Run
/// it on include-expanded source. Each qualifier is removed within its own
/// line, so line numbers are unchanged.
///
/// Recognised: `layout(location = <digits>)`, whitespace anywhere inside the
/// parentheses but on one line, followed by any mix of `flat`, `smooth`,
/// `noperspective`, `centroid`, `sample`, `invariant` and `precise` (possibly
/// across newlines) and then the stage's `in` or `out`. Left in place: a
/// layout list with a second member (`location = 0, component = 1`), a
/// location spelled as a macro or expression, a newline inside the
/// parentheses, and a comment or preprocessor line between the qualifier and
/// the storage keyword. Those fail the validator's GLES arm, as they fail live.
PHOSPHORSHADERS_EXPORT QString stripVaryingLocations(const QString& source, ShaderStage stage);

/// @p source as KWin 6.8 hands it to the driver: every line that starts with
/// `#version` blanked (its newline kept, so line numbers do not move), and
/// KWin's GLSL ES 300 prologue (the version line, the
/// GL_OES_EGL_image_external require and its default precision declarations)
/// prepended. A copy of what `KWin::GLShader::preprocess` does at the top level
/// on a GLES 3 context, for the gates that compile the compositor branch
/// offline: the validator's GLES arm and the GPU bake tests. The runtime never
/// calls it, since KWin applies the real rewrite itself.
PHOSPHORSHADERS_EXPORT QString kwinGlesDialect(const QString& source);

/// @p source as KWin 6.7 hands it to the driver on a desktop GL context: every
/// line that starts with `#version` blanked (newline kept) and `#version 140`
/// prepended, with no precision or extension lines. The desktop counterpart of
/// `kwinGlesDialect`, for the validator's desktop compositor arm. The runtime
/// never calls it.
PHOSPHORSHADERS_EXPORT QString kwinDesktopDialect(const QString& source);

/// The `#define` KWin 6.8's per-window animation path and the decoration
/// present shader splice beside `kwinDefineBlock`, telling a shader that the
/// offscreen texture KWin hands an effect is top-down. KWin 6.8 changed that
/// orientation, so the runtime adds it exactly when the effect is built
/// against 6.8. Shared so the effect, the validator's GLES arm (which models
/// 6.8) and the GPU bake tests spell it once. Newline-terminated with @p eol.
PHOSPHORSHADERS_EXPORT QString kwinTopDownTexcoordsDefine(const QString& eol = QStringLiteral("\n"));

/// The compositor branch of @p source exactly as the effect hands it to KWin:
/// `kwinDefineBlock` and @p extraDefines spliced after `#version`, then the
/// varying locations of @p stage stripped. A leading BOM is dropped. A source
/// with no `#version` gets `#version 450` synthesized in front of the block,
/// with the author's first line re-anchored to line 1, and
/// @p synthesizedVersion (when given) reports that so the caller can warn.
/// @p extraDefines is re-terminated to the source's line-ending convention.
///
/// ONE function for every place that builds this source: the compositor's
/// `ShaderInternal::injectKwinDefineAfterVersion`, the offline validator's
/// compositor bake and the GPU bake tests. Each of them used to splice and
/// strip by hand, which is how a gate ends up compiling something the
/// compositor never runs.
PHOSPHORSHADERS_EXPORT QString kwinCompositorSource(const QString& source, ShaderStage stage,
                                                    const QString& extraDefines = {},
                                                    bool* synthesizedVersion = nullptr);

/// A `#line` re-anchor that numbers the NEXT line @p line of source string
/// @p sourceString under every GLSL dialect a shader here compiles in,
/// terminated with @p eol.
///
/// GLSL changed the directive's meaning: before desktop GLSL 330, `#line N`
/// numbered the line after it N+1, and from 330 on it numbers it N. Every GLSL
/// ES version numbers it N, 1.00 included, which is how glslang and Mesa read
/// the ES specs. KWin 6.7 compiles the compositor branch as desktop GLSL 140,
/// so a plain `#line N` put every compile-log line there one past the real
/// fault. The directive therefore takes its number from a macro defined per
/// dialect under `__VERSION__`. A macro and not an expression, because Mesa's
/// preprocessor accepts only an integer after `#line` once macros are expanded.
/// The macro name carries both numbers, so two re-anchors to the same place
/// define it identically, which GLSL allows.
///
/// Used by `spliceAfterVersion`, by `assembleEntryPoint` after a family
/// prologue, and by the include resolver, which also honours a re-anchor of
/// this shape that its input already carries.
PHOSPHORSHADERS_EXPORT QString lineDirective(int line, int sourceString, const QString& eol = QStringLiteral("\n"));

} // namespace PhosphorShaders
