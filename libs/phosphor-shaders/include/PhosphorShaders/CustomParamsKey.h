// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1Char>
#include <QLatin1String>
#include <QString>

namespace PhosphorShaders {

/// Lower / upper bounds on a multipass `bufferScale` (FBO downscale
/// factor). 0.125 means a 1/8 downscale on each axis (1/64 area — the
/// lowest cost-floor that still gives Shadertoy-style buffer effects
/// something to work with); 1.0 means full-resolution FBOs. Canonical
/// home for every clamp site (overlay metadata parse, surface
/// `SurfaceShaderEffect::kMin/MaxBufferScale` forwarders, and the two
/// rendering setters), so the bounds cannot drift per-runtime.
inline constexpr double kMinBufferScale = 0.125;
inline constexpr double kMaxBufferScale = 1.0;

/// The accepted texture / buffer `wrap` vocabulary, shared by every
/// validation site across the shader registries (overlay image-param
/// parse, surface metadata parse, per-slot texture parse, and runtime
/// override translation). Returns true only for the three canonical
/// tokens `clamp` / `repeat` / `mirror`. An empty string is NOT a member —
/// callers treat empty as "use the runtime default" and handle it
/// explicitly before consulting this predicate. Lives here (the lowest
/// shader library) so overlay and surface validation cannot drift apart;
/// `PhosphorSurfaceShaders::isValidWrapToken` forwards to this. Vocabulary
/// matches the runtime normaliser (`ShaderNodeRhi::normalizeWrapMode`).
inline bool isValidWrapToken(const QString& wrap)
{
    return wrap == QLatin1String("clamp") || wrap == QLatin1String("repeat") || wrap == QLatin1String("mirror");
}

/// The accepted buffer `filter` vocabulary: `linear` / `nearest` /
/// `mipmap`. Same membership and empty-string contract as
/// `isValidWrapToken`; `PhosphorSurfaceShaders::isValidFilterToken`
/// forwards to this.
inline bool isValidFilterToken(const QString& filter)
{
    return filter == QLatin1String("linear") || filter == QLatin1String("nearest") || filter == QLatin1String("mipmap");
}

/// Canonical key format for the `customParams[N].<x|y|z|w>` sub-slots in
/// `BaseUniforms`. Used as the cross-runtime serialisation of per-effect
/// declared parameter values when those values travel through a
/// `QVariantMap` between encoder (a registry's `translate*` method) and
/// decoder (a render path that writes them into the UBO).
///
/// Produces strings of the form `"customParams<N>_<x|y|z|w>"` where `N` is
/// 1-based (matching what GLSL authors write in their
/// `#define direction customParams[0].x` macros, plus one for the
/// daemon's UBO key parser).
///
/// Three concrete consumers of this format today:
///
///   • `PhosphorShaders::ShaderRegistry::ParameterInfo::uniformName()` —
///     overlay-shader encoder (uses an internal lookup-table form;
///     identical output)
///   • `PhosphorAnimationShaders::AnimationShaderRegistry::translateAnimationParams`
///     — animation-shader encoder
///   • `PhosphorRendering::ShaderEffect::setShaderParams` — decoder for
///     both runtime paths (overlay and animation)
///
/// Plus the kwin-effect's per-transition pack at compositor-side. All
/// four call sites consume the format produced here. If the format ever
/// changes — even just the leading `"customParams"` prefix or the
/// underscore separator — the change has to land here so every site
/// stays in sync.
namespace CustomParams {

/// Number of `vec4` slots in `BaseUniforms::customParams[8]`.
inline constexpr int kVecCount = 8;

/// Number of float sub-slots across all vec4s (4 × kVecCount). Caps the
/// flat-index space used by `slotKey(int slot)` and the per-effect
/// parameter budget enforced by `AnimationShaderContract::kMaxParameterSlots`
/// (which forwards to this constant).
inline constexpr int kFlatSlotCount = 4 * kVecCount;

/// Format a customParams slot key from explicit `(vec, comp)` pair.
/// `vec` is the 0..7 index into `customParams[8]` (the array slot);
/// `comp` is `'x'`, `'y'`, `'z'`, or `'w'` (the float sub-slot inside
/// the vec4). The vec index is rendered 1-based in the output to match
/// the GLSL-author convention.
inline QString slotKey(int vec, char comp)
{
    return QStringLiteral("customParams") + QString::number(vec + 1) + QLatin1Char('_') + QLatin1Char(comp);
}

/// Format a customParams slot key from a flat sub-slot index in
/// [0, `kFlatSlotCount`). Slot 0 → `"customParams1_x"`, slot 4 →
/// `"customParams2_x"`, etc.
///
/// Out-of-range values return an empty `QString` rather than wrapping
/// around — wrap-around would silently collide with a valid in-range key
/// (e.g. `slot = -1` would otherwise produce the same string as `slot = 3`
/// in two's-complement modulo arithmetic, corrupting the decoder's UBO
/// upload). The empty-string behaviour is graceful-degradation: no
/// decoder ever matches the empty key, so the value drops cleanly rather
/// than overflowing into an adjacent slot.
inline QString slotKey(int slot)
{
    if (slot < 0 || slot >= kFlatSlotCount) {
        return {};
    }
    static constexpr char kComponents[4] = {'x', 'y', 'z', 'w'};
    return slotKey(slot / 4, kComponents[slot & 3]);
}

/// GLSL author-facing accessor for a flat scalar sub-slot — the form a shader
/// author reads in fragment source, as opposed to `slotKey()`'s `QVariantMap`
/// serialisation form. Slot 0 → `"customParams[0].x"`, slot 5 →
/// `"customParams[1].y"`. The vec index is rendered **0-based** here to match
/// GLSL array indexing (`slotKey()` is 1-based for the uniform-key wire
/// format). Out-of-range values return an empty `QString` — same
/// graceful-degradation contract as `slotKey(int)`.
inline QString glslAccessor(int slot)
{
    if (slot < 0 || slot >= kFlatSlotCount) {
        return {};
    }
    static constexpr char kComponents[4] = {'x', 'y', 'z', 'w'};
    return QStringLiteral("customParams[") + QString::number(slot / 4) + QStringLiteral("].")
        + QLatin1Char(kComponents[slot & 3]);
}

} // namespace CustomParams

/// Canonical key format for the `customColors[N]` slots in `BaseUniforms`.
/// Sibling to `CustomParams::slotKey` — the slot-keyed map decoder in
/// `PhosphorRendering::ShaderEffect::setShaderParams` consumes both formats.
///
/// Color params produce keys of the form `"customColor<N>"` where `N` is
/// 1-based. There is no sub-component split because each color occupies a
/// full vec4 (rgba) — so the single-arg overload is the only one needed.
///
/// Three current consumers:
///
///   • `PhosphorShaders::ShaderRegistry::ParameterInfo::uniformName()` —
///     overlay-shader encoder for color params (uses internal lookup
///     table; identical output)
///   • `PhosphorRendering::ShaderEffect::setShaderParams` — decoder for
///     both runtime paths
///   • `AnimationShaderRegistry::translateAnimationParams` — animation
///     shaders route color-typed params through this helper too. The
///     encoder advances a separate `colorSlot` counter independently of
///     the float `customParams` allocator (see
///     `AnimationShaderContract.h` for the independence rationale) and
///     enforces the 16-slot `kColorCount` budget; overflow is dropped
///     with a `qCWarning`. The `AnimationShaderContract::colorKey`
///     helper is a thin forwarder onto this function.
///
/// Lifted alongside `CustomParams::slotKey` so a future format drift
/// (renaming the prefix, switching to 0-based indexing, etc.) only has to
/// change here and every consumer stays in sync.
namespace CustomColors {

/// Number of color slots in `BaseUniforms::customColors[16]`.
inline constexpr int kColorCount = 16;

/// Format a customColor key from a 0-based slot index. Slot 0 →
/// `"customColor1"`, slot 15 → `"customColor16"`. Out-of-range values
/// return an empty `QString` rather than wrapping around — same
/// graceful-degradation contract as `CustomParams::slotKey(int)`.
inline QString colorKey(int slot)
{
    if (slot < 0 || slot >= kColorCount) {
        return {};
    }
    return QStringLiteral("customColor") + QString::number(slot + 1);
}

/// GLSL author-facing accessor for a color slot — the form a shader author
/// reads in fragment source, as opposed to `colorKey()`'s `QVariantMap`
/// serialisation form. Slot 0 → `"customColors[0]"`. **0-based** to match GLSL
/// array indexing (`colorKey()` is 1-based for the uniform-key wire format).
/// Out-of-range values return an empty `QString`.
inline QString glslAccessor(int slot)
{
    if (slot < 0 || slot >= kColorCount) {
        return {};
    }
    return QStringLiteral("customColors[") + QString::number(slot) + QLatin1Char(']');
}

} // namespace CustomColors

} // namespace PhosphorShaders
