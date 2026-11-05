// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1Char>
#include <QLatin1String>
#include <QString>

namespace PhosphorShaders {

/// Lower / upper bounds on a multipass `bufferScale` (FBO downscale
/// factor). 1/128 means a 1/128 downscale on each axis, and 1.0 means
/// full-resolution FBOs. Canonical home for every clamp site, so the bounds
/// cannot drift per-runtime: all four families' metadata parsers, the
/// `kMin/MaxBufferScale` forwarders that surface, animation and pointer each
/// re-export from here, the four rendering setters (`ShaderEffect`'s scalar
/// and per-pass scale setters and their two `ShaderNodeRhi` counterparts),
/// the four pack-validator range lints, and the compositor's own clamps for
/// the surface backdrop and the pointer pass.
///
/// WHY THE FLOOR SITS FOUR STEPS BELOW THE DEEPEST LEVEL ANY PACK DECLARES.
/// The deepest level of the bundled Kawase pyramids is 1/32, which is as deep
/// as a pyramid starting at quarter resolution can go and still have texels to
/// average. But the compositor multiplies every declared scale by the user's
/// decoration blur-scale multiplier BEFORE clamping
/// (PlasmaZonesEffect::clampedBufferScale), and that multiplier bottoms out at
/// 1/4 (DecorationDefaults::BlurScaleMultiplierMin). A floor equal to the
/// deepest declared level therefore clamped the bottom of the pyramid straight
/// back up at any multiplier below 1. At the minimum, five of the seven levels
/// all landed on 1/32 and the pyramid stopped halving at all, so turning the
/// quality down stopped reducing work and started destroying the blur instead.
/// 1/32 * 1/4 = 1/128 gives the multiplier its full range to scale the whole
/// pyramid without collapsing it. Nothing degenerates at the floor: both hosts
/// size targets with qMax(1, qRound(extent * scale)).
inline constexpr double kMinBufferScale = 0.0078125;
inline constexpr double kMaxBufferScale = 1.0;

/// Maximum number of multipass buffer passes a pack may declare. Canonical
/// home shared by the overlay parser, the pack validator and the daemon's
/// ShaderNodeRhi binding budget (the surface tree forwards
/// SurfaceShaderEffect::kMaxBufferPasses to it, as does the animation tree's
/// AnimationShaderContract::kMaxBufferPasses). The figure comes from the
/// SURFACE family: a dual Kawase pyramid needs seven passes, four down and
/// three up, to reach a 256 px blur from a quarter-resolution base, and the
/// eighth is headroom so the deepest bundled chain is not sitting exactly on
/// the cap. Every buffer pass costs a texture at its declared scale, so the
/// cap bounds GPU memory.
///
/// THE OTHER THREE FAMILIES INHERIT THAT NUMBER RATHER THAN NEEDING IT, and
/// that is deliberate rather than an oversight. The overlay and animation
/// parsers take this constant directly, so an installable pack in either
/// family may now declare eight full-canvas buffer draws where four was the
/// documented ceiling, on a path that is GPU-bound. The grant is accepted
/// because both are opt-in per pack and both already pay per pass at the
/// declared scale, so the cap bounds the worst case rather than describing a
/// typical one. The POINTER family is the exception and keeps its own cap of
/// two, stated in its contract, because a pointer chain runs on every output
/// frame while the pointer is live. A family that acquires that shape should
/// take its own cap the same way instead of inheriting this one.
///
/// The shared GLSL headers do NOT all declare this many iChannel samplers.
/// Overlay (multipass.glsl) and surface (surface_multipass.glsl) declare
/// iChannel0..7 and match; pointer declares iChannel0..3, above its own cap of
/// two; and the animation family declares none at all, which is why a
/// multipass animation pack has no declaration to sample its buffers through.
/// See ShaderBindings.h for the binding numbers the first two use.
inline constexpr int kMaxBufferPasses = 8;

/// The accepted texture / buffer `wrap` vocabulary, shared by every
/// validation site across the shader registries (overlay image-param
/// parse, surface metadata parse, per-slot texture parse, and runtime
/// override translation). Returns true only for the three canonical
/// tokens `clamp` / `repeat` / `mirror`. An empty string is NOT a member —
/// callers treat empty as "use the runtime default" and handle it
/// explicitly before consulting this predicate. Lives here (the lowest
/// shader library) so the four families' validation cannot drift apart. All
/// three of the other families forward to it, each from its own contract
/// header: `PhosphorSurfaceShaders::SurfaceShaderContract::isValidWrapToken`,
/// `PhosphorAnimationShaders::AnimationShaderContract::isValidWrapToken` and
/// `PhosphorPointerShaders::PointerShaderContract::isValidWrapToken`. The
/// overlay family and the pack validator call this one directly. Vocabulary
/// matches the runtime normaliser (`ShaderNodeRhi::normalizeWrapMode`).
inline bool isValidWrapToken(const QString& wrap)
{
    return wrap == QLatin1String("clamp") || wrap == QLatin1String("repeat") || wrap == QLatin1String("mirror");
}

/// The accepted buffer `filter` vocabulary: `linear` / `nearest` /
/// `mipmap`. Same membership and empty-string contract as
/// `isValidWrapToken`, and the same forwarders, each on its own contract
/// header (`SurfaceShaderContract`, `AnimationShaderContract`,
/// `PointerShaderContract`).
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
/// EVERY family encodes through this format and BOTH runtimes decode through
/// it. That is the property worth stating; the count that used to stand here
/// said three in one sentence and four in the next, and both were out of date.
///
///   • Encoders. The overlay registry's `ParameterInfo::uniformName()` builds
///     the same strings from an internal lookup table. Animation, surface and
///     pointer each reach this function through their own contract header's
///     `paramKey` forwarder.
///   • Decoders. The daemon decodes in
///     `PhosphorRendering::ShaderEffect::setShaderParams`, for every family.
///     The compositor decodes in its own per-family pack code.
///
/// If the format ever changes, even just the leading `"customParams"` prefix
/// or the underscore separator, the change lands here and every site follows.
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
    // Same graceful-degradation contract as the flat slotKey(int) below: an
    // out-of-range vec or a component outside {x,y,z,w} returns an empty string
    // rather than fabricating a key that could collide with a valid one.
    if (vec < 0 || vec >= kVecCount || (comp != 'x' && comp != 'y' && comp != 'z' && comp != 'w')) {
        return {};
    }
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
/// Reached the same way `CustomParams::slotKey` is, and by the same set:
/// every family encodes through it, the daemon decodes through it once for
/// all families, and the compositor decodes through it per family.
///
///   • Encoders. The overlay registry's `ParameterInfo::uniformName()` builds
///     the same strings from an internal lookup table. Animation, surface and
///     pointer reach this function through their contract headers' `colorKey`
///     forwarders. Each encoder advances a `colorSlot` counter INDEPENDENTLY
///     of the float `customParams` allocator (see `AnimationShaderContract.h`
///     for why they are independent) and enforces the 16-slot `kColorCount`
///     budget, dropping overflow with a `qCWarning`.
///   • Decoders. `PhosphorRendering::ShaderEffect::setShaderParams` on the
///     daemon, and the compositor's own per-family pack code.
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
