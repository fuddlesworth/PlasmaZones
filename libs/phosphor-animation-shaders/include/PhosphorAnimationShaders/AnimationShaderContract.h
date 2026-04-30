// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/CustomParamsKey.h>

#include <QString>

namespace PhosphorAnimationShaders {

/// Cross-runtime named-uniform contract for **animation/transition shaders**.
///
/// PlasmaZones has two distinct shader registries:
///
///   1. **Animation/transition shaders** — `AnimationShaderRegistry`,
///      sourced from `data/animations/*/`. Short-lived transitions
///      driven by a 0..1 timeline (open, close, snap, drag, etc.).
///
///   2. **Overlay/zone-background shaders** — `PhosphorShaders::ShaderRegistry`,
///      sourced from `data/shaders/*/`. Long-lived ambient effects with
///      access to the rich `BaseUniforms` UBO (`iMouse`, `iDate`,
///      `customColors[16]`, audio-spectrum / wallpaper / multipass
///      textures, etc.). Daemon-only (RHI/multipass infrastructure has
///      no compositor-side equivalent).
///
/// This header documents the contract for the **first** category. It
/// applies identically across both runtime execution sites:
///
///   • **Compositor (window-content) execution** — `kwin-effect` running
///     inside the KWin compositor process. Uses classic OpenGL via
///     `KWin::GLShader`. Animates window contents during lifecycle
///     events (`window.open`, `window.move`, `zone.snapIn`, …).
///
///   • **Daemon (overlay-surface) execution** — `SurfaceAnimator::runLeg`
///     in the PlasmaZones daemon. Uses Qt RHI via
///     `PhosphorRendering::ShaderEffect` → `ShaderNodeRhi`. Animates
///     daemon-owned overlay surfaces (snap-assist popup, OSD
///     notification, layout-picker, zone-selector show/hide).
///
/// Both runtimes drive the same animation shaders with the same uniform
/// values. Authors write one `effect.frag`; it runs identically wherever
/// it's invoked.
///
/// @par UBO source convention
/// Animation shaders declare their uniforms inside the canonical
/// `AnimationUniforms` UBO via:
///
/// @code
/// #version 450
/// #include "animation_uniforms.glsl"
/// // ...read from iTime, iResolution, customParams[N].xyz...
/// @endcode
///
/// The UBO layout (`data/animations/_shared/animation_uniforms.glsl`)
/// is a std140-aligned prefix of `PhosphorShaders::BaseUniforms` so the
/// daemon's binding=0 upload populates it directly. The kwin-effect
/// can't bind UBOs through `KWin::GLShader`, so it runs a small in-memory
/// source rewriter that converts the UBO declaration to default-block
/// uniforms before handing the source to
/// `KWin::ShaderManager::generateCustomShader`.
///
/// @par Per-effect declared parameters
/// Every parameter declared in `metadata.json` lands in a
/// `customParams[N].xyz` slot in declaration order. Float/int/bool
/// parameters fill `customParams[0].x`, `customParams[0].y`,
/// `customParams[0].z`, `customParams[0].w`, then `customParams[1].x`,
/// … through `customParams[7].w` (32 slots total). Color parameters
/// would fill `customColors[N]` in a separate region (animation effects
/// don't currently declare color parameters; if they do, the contract
/// extends accordingly).
///
/// `AnimationShaderRegistry::translateAnimationParams(effectId, friendlyMap)`
/// converts a friendly parameter map (e.g. `{"direction": 1, "parallax":
/// 0.2}`) into the slot-keyed map (e.g. `{"customParams1_x": 1,
/// "customParams1_y": 0.2}`) both runtimes consume.
///
/// @par What's intentionally NOT in this contract
/// Overlay shaders' rich UBO fields (`iMouse`, `iDate`, `iTimeDelta`,
/// `iFrame`, `iTimeHi`, `iChannelResolution[]`, `iTextureResolution[]`,
/// `iAudioSpectrumSize`, `iFlipBufferY`, audio / wallpaper / user
/// textures, multipass buffer samplers) — and the `customColors[16]`
/// array — are **not** part of the animation contract. They are
/// declared in `data/shaders/common.glsl` and populated only by the
/// daemon's overlay path. An animation shader that reads those fields
/// will get either zero or undefined values; if you need them, the
/// effect belongs in `data/shaders/` (overlay), not `data/animations/`
/// (transition).
namespace AnimationShaderContract {

/// `float iTime` — transition progress in [0.0, 1.0]. Both runtimes
/// drive this from a 0..1 timeline (kwin: lifecycle elapsed/duration or
/// animator state value; daemon: `SurfaceAnimator::runLeg`'s
/// `shaderTime->start(0.0, 1.0, ...)`). Authors should `clamp(iTime,
/// 0.0, 1.0)` defensively in case a future timeline overshoots.
inline constexpr const char* kITime = "iTime";

/// `vec2 iResolution` — surface size in logical pixels. Window frame
/// size on the compositor execution site; overlay-surface size on the
/// daemon execution site.
inline constexpr const char* kIResolution = "iResolution";

/// `vec4 customParams[N]` — per-effect declared parameter slots.
/// Element name for `glGetUniformLocation` on the compositor side
/// (after the kwin-effect's source rewrite turns the array into
/// default-block uniforms `customParams[0]..customParams[7]`).
inline constexpr const char* kCustomParamsArray = "customParams";

/// Number of `vec4` slots in the `customParams` array. Forwards to the
/// canonical constant in `<PhosphorShaders/CustomParamsKey.h>` so a
/// single source of truth governs both libraries.
inline constexpr int kMaxCustomParams = PhosphorShaders::CustomParams::kVecCount;

/// Number of float sub-slots (4 per vec4 × 8 vec4s). Caps the count of
/// declared parameters an animation shader can carry without spilling
/// into a region the daemon's overlay extension owns. Forwards to the
/// canonical constant in `<PhosphorShaders/CustomParamsKey.h>`.
inline constexpr int kMaxParameterSlots = PhosphorShaders::CustomParams::kFlatSlotCount;

/// Format a `customParams` slot key — thin forwarder onto
/// `PhosphorShaders::CustomParams::slotKey`, the cross-library canonical
/// helper. Kept here so animation-shader call sites can refer to a name
/// inside this contract namespace and consumers don't need to import the
/// phosphor-shaders header directly. See
/// `<PhosphorShaders/CustomParamsKey.h>` for the format, the rationale,
/// and the full list of consumers.
inline QString slotKey(int vec, char comp)
{
    return PhosphorShaders::CustomParams::slotKey(vec, comp);
}

/// Flat-slot overload: `slot` is 0..31 across the 8 `vec4` slots.
inline QString slotKey(int slot)
{
    return PhosphorShaders::CustomParams::slotKey(slot);
}

/// @par Std140 offset contract
/// The canonical `data/animations/_shared/animation_uniforms.glsl` UBO
/// declares its fields at the same byte offsets as the prefix of
/// `PhosphorShaders::BaseUniforms` (the daemon's `binding=0` upload
/// struct). That alignment is what lets a single `effect.frag` source
/// run on both runtimes without per-runtime overrides.
///
/// The C++ side of the contract is pinned by `static_assert(offsetof(...))`
/// statements in `<PhosphorShaders/BaseUniforms.h>` for `iTime`,
/// `iResolution`, `customParams`, and `customColors`. If anyone reorders
/// `BaseUniforms`, those asserts fail at compile time and the canonical
/// GLSL header has to be updated to match. The GLSL side is exercised
/// at build time by `tests/unit/ui/test_animation_shader_bake.cpp`,
/// which runs every built-in animation shader through `qsb` (which in
/// turn computes std140 offsets) — a layout drift would surface there
/// as a bake failure.

} // namespace AnimationShaderContract

} // namespace PhosphorAnimationShaders
