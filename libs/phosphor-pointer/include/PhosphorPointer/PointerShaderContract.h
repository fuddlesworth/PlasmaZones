// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/CustomParamsKey.h>

#include <QString>

namespace PhosphorPointerShaders {

/// Cross-runtime named-uniform contract for **pointer shaders**.
///
/// Pointer packs (`data/pointer/<id>/`) decorate the mouse pointer: trails,
/// halos, click ripples, sparks. They are the fourth shader family beside the
/// overlay, animation and surface categories and are modelled on the surface
/// contract: one `effect.frag` runs on both runtimes.
///
///   • **Compositor execution** — the kwin-effect paints the chain as a
///     screen-space pass over the composited scene of the output under the
///     pointer. Classic GL, loose uniforms (the `#ifdef PLASMAZONES_KWIN`
///     branch of `data/pointer/shared/pointer_uniforms.glsl`).
///
///   • **Preview execution** — the settings app drives the same source on
///     the Qt-RHI `ShaderEffect` node. std140 UBO at binding 0 =
///     `PhosphorShaders::BaseUniforms` followed by the pointer tail
///     (`PointerUniformsTail`, see PointerShaderUniforms.h).
///
/// A pointer pack is a CONTINUOUS decoration: it ticks every frame while the
/// pointer has recent motion or a recent button event, then the host stops
/// repainting until the next event (`PointerShaderEffect::trailSeconds`).
/// Click reactions are expressed through the press / release event uniforms
/// inside that same continuous contract, so there is one family, not a
/// decoration arm plus an animation arm.
///
/// @par Canvas
/// The canvas is the whole output in device px, top-down, origin at the
/// output's top-left. Every position uniform is in that space and
/// `iResolution.xy` is the output size in device px.
///
/// @par Per-effect declared parameters
/// Identical mechanism to the surface contract: float / int / bool parameters
/// fill `customParams[0].x` … `customParams[7].w` (32 slots), colour
/// parameters fill `customColors[0]` … `customColors[15]` (16 slots), both in
/// metadata declaration order with independent allocators.
/// `PointerShaderRegistry::translatePointerParams` converts a friendly map
/// into the slot-keyed map both runtimes consume and
/// `PointerShaderRegistry::paramPreamble` generates the `#define p_<id>`
/// accessors packs read.
namespace PointerShaderContract {

// ── Base (both runtimes; UBO: BaseUniforms members) ──────────────────────

/// `float iTime` — continuously increasing seconds, wrapped like every
/// family (`iTimeHi` is unused on this contract).
inline constexpr const char* kITime = "iTime";

/// `vec2 iResolution` — canvas size in device px.
inline constexpr const char* kIResolution = "iResolution";

/// `vec4 iMouse` — `.xy` pointer position in canvas px, `.zw` = `.xy /
/// iResolution`.
inline constexpr const char* kIMouse = "iMouse";

/// `vec4 customParams[8]` / `vec4 customColors[16]` — declared parameter
/// slots. Cross-runtime element-name lookup constants, mirrored by the
/// kwin-effect's `glGetUniformLocation("customParams[N]")` calls.
inline constexpr const char* kCustomParamsArray = "customParams";
inline constexpr const char* kCustomColorsArray = "customColors";

/// `vec4 iChannelResolution[4]` — multipass buffer sizes (`.xy`).
inline constexpr const char* kIChannelResolution = "iChannelResolution";

/// `vec4 iTextureResolution[4]` — user texture sizes (`.xy`).
inline constexpr const char* kITextureResolution = "iTextureResolution";

// ── Pointer tail (UBO offsets 672..1279, see PointerShaderUniforms.h) ────

/// `vec4 uPointerVelocity` — `.xy` device px/s, `.z` speed (length), `.w`
/// unused. UBO offset 672.
inline constexpr const char* kUPointerVelocity = "uPointerVelocity";

/// `vec4 uPointerPress` — `.xy` canvas px of the last button press, `.z`
/// seconds since it (1e6 when none this session), `.w` button (1 left, 2
/// right, 3 middle, 0 none). UBO offset 688.
inline constexpr const char* kUPointerPress = "uPointerPress";

/// `vec4 uPointerRelease` — same shape as `uPointerPress` for the last
/// release. UBO offset 704.
inline constexpr const char* kUPointerRelease = "uPointerRelease";

/// `vec4 uPointerState` — `.x` pressed-button bitmask as float (1 left, 2
/// right, 4 middle), `.y` seconds since the last motion, `.z`
/// logical-to-device scale, `.w` trail point count actually filled (0..32).
/// UBO offset 720.
inline constexpr const char* kUPointerState = "uPointerState";

/// `vec4 uCursorRect` — cursor sprite rect in canvas px (x, y, w, h) with
/// the hotspot already applied, so it is where the sprite is drawn.
/// (0, 0, 0, 0) when unknown. UBO offset 736.
inline constexpr const char* kUCursorRect = "uCursorRect";

/// `vec4 uPointerFlags` — `.x` = 1.0 when `uCursorSprite` is bound
/// (`needsCursor` honoured), else 0. `.y .z .w` reserved 0. UBO offset 752.
inline constexpr const char* kUPointerFlags = "uPointerFlags";

/// `vec4 uPointerTrail[32]` — newest first. `.xy` canvas px, `.z` age in
/// seconds (0 = this frame), `.w` speed at that sample (device px/s).
/// Entries past `uPointerState.w` are zero. UBO offset 768, 512 bytes.
inline constexpr const char* kUPointerTrail = "uPointerTrail";

// ── Samplers ─────────────────────────────────────────────────────────────

/// `sampler2D uCursorSprite` — the cursor image, bound only for packs that
/// declare `needsCursor`. Loose sampler on KWin, binding 7 on the UBO
/// runtime. Gate every read on `uPointerFlags.x`.
inline constexpr const char* kUCursorSprite = "uCursorSprite";

/// `sampler2D uTexture1..3` — user-declared image textures (metadata
/// `textures`). Slot N of the metadata list feeds `uTexture<N+1>` (bindings
/// 8-10 on the UBO runtime) and `iTextureResolution[N].xy` carries its size.
inline constexpr const char* kUTexture1 = "uTexture1";
inline constexpr const char* kUTexture2 = "uTexture2";
inline constexpr const char* kUTexture3 = "uTexture3";

/// `sampler2D iChannel0..3` — multipass buffers, declared by the opt-in
/// `pointer_multipass.glsl` include (bindings 2-5 on the UBO runtime).
inline constexpr const char* kIChannel0 = "iChannel0";
inline constexpr const char* kIChannel1 = "iChannel1";
inline constexpr const char* kIChannel2 = "iChannel2";
inline constexpr const char* kIChannel3 = "iChannel3";

// ── Budgets ──────────────────────────────────────────────────────────────

/// Ring capacity of `uPointerTrail`.
inline constexpr int kMaxTrailPoints = 32;

/// Number of `vec4` slots in `customParams` (8). Forwards to the canonical
/// constant so a single source of truth governs every shader family.
inline constexpr int kMaxCustomParams = PhosphorShaders::CustomParams::kVecCount;

/// Number of `customColors` slots (16).
inline constexpr int kMaxCustomColors = PhosphorShaders::CustomColors::kColorCount;

/// Number of float sub-slots (4 × 8 = 32), the per-effect scalar budget.
inline constexpr int kMaxParameterSlots = PhosphorShaders::CustomParams::kFlatSlotCount;

/// Maximum number of user-declared textures per pointer effect.
inline constexpr int kMaxUserTextureSlots = 3;

/// Maximum number of buffer passes a pointer pack may declare. Two, not the
/// surface family's four: a pointer chain runs on every output frame while
/// live, and each pass costs a canvas-sized draw.
inline constexpr int kMaxBufferPasses = 2;

/// Maximum number of declared parameters a pack may carry across both pools.
inline constexpr int kMaxDeclaredParameters = 48;

/// Peak speed, in px per second, that the settings preview's simulated pointer
/// ever reaches. The preview stage is PreviewCanvas.size (420x236) and its
/// pointer traces a Lissajous figure eight over a 4 second lap, so the two
/// axes have amplitude 158.1 px at 1.571 rad/s and 66.1 px at 3.142 rad/s.
/// Both terms peak together at the start of the lap, giving
/// hypot(158.1 * 1.571, 66.1 * 3.142) which is a little under 324.
///
/// It lives here because it is a fact about how every pointer pack is judged,
/// and the validator lints a pack's speed-gate default against it: a default
/// above this means the gate never opens on the lap, so the pack previews as a
/// blank stage however well it behaves on a real desktop. That is how the
/// windtrail pack shipped invisible.
///
/// PointerPreviewCanvas.qml owns the lap this is derived from. Changing the
/// stage size, the lap duration or the inset changes this number too.
inline constexpr double kPreviewPeakSpeedPxPerSecond = 324.0;

/// The accepted texture `wrap` vocabulary (forwarder onto the canonical
/// predicate in `<PhosphorShaders/CustomParamsKey.h>`).
inline bool isValidWrapToken(const QString& wrap)
{
    return PhosphorShaders::isValidWrapToken(wrap);
}

/// Format a `customParams` slot key (`customParams<N>_<x|y|z|w>`).
inline QString paramKey(int vec, char comp)
{
    return PhosphorShaders::CustomParams::slotKey(vec, comp);
}

/// Flat-slot overload: `slot` is 0..31 across the 8 `vec4` slots.
inline QString paramKey(int slot)
{
    return PhosphorShaders::CustomParams::slotKey(slot);
}

/// Format a `customColor<N>` slot key (1-based on the wire).
inline QString colorKey(int slot)
{
    return PhosphorShaders::CustomColors::colorKey(slot);
}

} // namespace PointerShaderContract

} // namespace PhosphorPointerShaders
