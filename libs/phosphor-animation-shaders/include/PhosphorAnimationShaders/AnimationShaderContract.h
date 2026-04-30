// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace PhosphorAnimationShaders {

/// Cross-runtime named-uniform contract for **animation transition shaders**.
///
/// Animation shaders (`data/animations/*/effect.frag`) are short-lived
/// transitions — open/close/snap/drag/etc. — that ride a 0..1 timeline. By
/// design, they expose a deliberately small uniform surface so the same
/// `.frag` source compiles and renders identically across both PlasmaZones
/// runtimes:
///
///   1. **kwin-effect (compositor process)** — `KWin::ShaderManager`
///      `OffscreenEffect::redirect`, raw GL `setUniform(loc, val)`.
///      Animates window contents during lifecycle events and zone snaps.
///
///   2. **daemon (overlay surfaces, animation transitions)** —
///      `PhosphorRendering::ShaderEffect` (a `QQuickItem` driven by
///      `SurfaceAnimator::runLeg`), Qt RHI. Animates overlay-window
///      surfaces (snap-assist popup, OSD notification, layout-picker,
///      zone-selector show/hide).
///
/// Both runtimes drive `iTime` from `[0.0, 1.0]` (transition progress, not
/// wallclock seconds), and feed `iResolution` from the per-target frame
/// size. Per-effect declared parameters (`AnimationShaderEffect::parameters`)
/// resolve as named default-block uniforms with the parameter id as the
/// uniform name; values come from `ShaderProfile::effectiveParameters()`
/// and fall back to the metadata-declared default.
///
/// @par What's intentionally NOT in this contract
/// Overlay shaders (`data/shaders/*/effect.frag`) declare a richer
/// `layout(std140, binding = 0) uniform ZoneUniforms { ... }` block via
/// `data/shaders/common.glsl` and read `iMouse`, `iDate`, `iTimeDelta`,
/// `iFrame`, `iTimeHi`, `customParams[8]`, `customColors[16]`,
/// `iChannelResolution[4]`, `iTextureResolution[4]`, `iAudioSpectrumSize`,
/// `iFlipBufferY`, plus buffer-pass / audio / wallpaper / user-texture
/// samplers. **None of those are part of the animation contract.** They
/// are populated only by the daemon's RHI overlay path
/// (`ShaderNodeRhi` → `BaseUniforms` UBO upload). An animation shader
/// that declares e.g. `uniform vec4 iMouse;` will:
///   - On the kwin-effect path: get an unset uniform (location -1, value 0).
///   - On the daemon animation path: get an unset uniform too, since
///     bare `ShaderEffect` does not expose those values either when
///     driven by `SurfaceAnimator::runLeg`.
///
/// In other words: a shader using overlay-only uniforms is not a portable
/// animation shader and should live under `data/shaders/` (overlay) rather
/// than `data/animations/` (transition).
namespace AnimationShaderContract {

/// `float iTime` — transition progress in [0.0, 1.0]. Both runtimes drive
/// this from a 0..1 timeline (kwin: lifecycle elapsed/duration or animator
/// state value; daemon: `SurfaceAnimator::runLeg`'s
/// `shaderTime->start(0.0, 1.0, ...)`). Authors should `clamp(iTime, 0.0,
/// 1.0)` defensively in case a future timeline overshoots.
inline constexpr const char* kITime = "iTime";

/// `vec2 iResolution` — viewport size in logical pixels. Window frame size
/// on the kwin path; overlay surface size on the daemon path.
inline constexpr const char* kIResolution = "iResolution";

} // namespace AnimationShaderContract

} // namespace PhosphorAnimationShaders
