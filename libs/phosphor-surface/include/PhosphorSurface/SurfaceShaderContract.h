// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/CustomParamsKey.h>

#include <QString>

namespace PhosphorSurfaceShaders {

/// Cross-runtime named-uniform contract for **surface shaders**.
///
/// Phosphor has four distinct shader registries:
///
///   1. **Animation/transition shaders** — `AnimationShaderRegistry`,
///      sourced from `data/animations/*/`. Short-lived transitions
///      driven by a 0..1 timeline (open, close, snap, drag, etc.).
///
///   2. **Overlay/zone-background shaders** — `PhosphorShaders::ShaderRegistry`,
///      sourced from `data/overlays/*/`. Long-lived ambient effects with
///      access to the rich `BaseUniforms` UBO (`iMouse`, `iDate`,
///      `customColors[16]`, audio-spectrum / wallpaper / multipass
///      textures, etc.). Daemon-only, because the overlay family has no
///      compositor consumer at all — not because multipass is daemon-only,
///      which it stopped being when the compositor grew the surface fold.
///
///   3. **Surface shaders** — `SurfaceShaderRegistry` (this contract),
///      sourced from `data/surface/*/`. Persistent per-window surface
///      layers composited OVER (or as part of) the live window content:
///      the decoration band / border, rounded corners, focus tint, and
///      similar window-chrome effects. Unlike an animation shader these
///      do not run on a 0..1 timeline — they re-render every frame for
///      as long as the window is mapped and are parameterised by the
///      window's frame geometry plus the pack's OWN declared parameters.
///      Border width, corner radius and the active / inactive colours are
///      pack parameters, not a host-defined decoration appearance: the
///      pack mixes its two colours on the contract's uSurfaceFocused
///      itself. See the appearance section below.
///
///   4. **Pointer shaders** — `PhosphorPointer::PointerShaderRegistry`,
///      sourced from `data/pointer/*/`. Cursor decoration packs,
///      compositor-only, and the one family with its OWN buffer-pass cap of
///      two rather than the shared eight, because its chain runs on every
///      frame the pointer is live. `CustomParamsKey.h` is written against
///      all four.
///
/// This header documents the contract for the **third** category. Like
/// the animation contract it is a **dual-runtime** contract — the same
/// `effect.frag` source runs identically across both runtime execution
/// sites:
///
///   • **Compositor (window-content) execution** — `kwin-effect` running
///     inside the KWin compositor process. Uses classic OpenGL via
///     `KWin::GLShader`. Composites the surface layer onto the live
///     window content every paint for as long as the window is mapped.
///
///   • **Daemon (overlay-surface) execution** — the Phosphor daemon's
///     surface-layer pipeline. Uses Qt RHI via
///     `PhosphorRendering::ShaderEffect`. Composites the same surface
///     layer onto daemon-owned surfaces.
///
/// Both runtimes drive the same surface shaders with the same uniform
/// values. Authors write one `effect.frag`; it runs identically wherever
/// it's invoked.
///
/// @par Per-effect declared parameters
/// Every parameter declared in `metadata.json` lands in either a
/// `customParams[N].xyz` slot (float / int / bool) or a
/// `customColors[N]` slot (color), in declaration order — identical to
/// the animation contract. The two allocators advance independently — a
/// color parameter does NOT consume a `customParams` sub-slot, so a
/// `[color, float]` declaration produces `customColors[0]` +
/// `customParams[0].x`, not `customColors[0]` + `customParams[0].y`.
/// Float / int / bool parameters fill `customParams[0].x` …
/// `customParams[7].w` (32 slots); color parameters fill
/// `customColors[0]` … `customColors[15]` (16 slots).
///
/// `SurfaceShaderRegistry::translateSurfaceParams(effect, friendlyMap)`
/// converts a friendly parameter map (e.g. `{"glow": 0.4, "tint":
/// "#ff8800"}`) into the slot-keyed map (e.g. `{"customParams1_x": 0.4,
/// "customColor1": QColor(0xff, 0x88, 0x00)}`) both runtimes consume.
/// Color values are coerced to QColor at that boundary — strings
/// parseable by the QColor constructor are accepted alongside QColor
/// instances; everything else falls back to the declared default, then
/// transparent.
///
/// @par Spatial / chrome contract
/// The surface uniforms describe the captured surface texture and the
/// window's chrome geometry within it. They are populated on BOTH
/// runtimes — KWin via classic-GL `setUniform`, the daemon via its
/// surface-layer uniform plumbing — so none is guarded by an
/// `#ifdef PLASMAZONES_KWIN`.
///
/// Field dynamics: the following are re-pushed every paint because they
/// can change frame to frame. `uTexture0` (the live captured surface),
/// `uSurfaceSize` (texture size in device px), `uSurfaceFrameTopLeft` /
/// `uSurfaceFrameSize` (the content/frame rect within the texture, which
/// shifts as the window moves/resizes), `uSurfaceScale` (the logical-to-
/// device pixel scale, which changes when the window moves to a
/// differently-scaled output), and `uSurfaceFocused` (focus toggles
/// independently of any redraw).
///
/// Decoration APPEARANCE — border width, corner radius, colours, glow,
/// etc. — is NOT host state. It is each pack's own declared PARAMETERS
/// (`customParams` / `customColors`), so the host pushes only the surface
/// geometry, the logical-to-device scale, and the focus flag; the pack's
/// `effect.frag` reads its appearance from its parameter slots (e.g. the
/// border pack mixes `p_inactiveColor`/`p_activeColor` on `uSurfaceFocused`
/// and scales `p_borderWidth`/`p_cornerRadius` by `uSurfaceScale`).
namespace SurfaceShaderContract {

/// `sampler2D uTexture0` — the live captured surface, and on the compositor
/// it is NOT the same texture for every pack in a chain. The fold makes its
/// own capture over the padded canvas and binds that to the FIRST pack; each
/// pack after it reads the RUNNING COMPOSITE, so a later pack sees the
/// earlier packs' output rather than the bare window. Daemon path: the live
/// FBO of the surface-layer anchor. Per-frame-dynamic: re-bound every paint
/// because the captured content changes continuously. The
/// `uSurfaceFrameTopLeft` / `uSurfaceFrameSize` pair locates the
/// content/frame rect within this texture.
inline constexpr const char* kUTexture0 = "uTexture0";

/// `vec2 uSurfaceSize` — the size of `uTexture0` in device pixels.
/// Per-frame-dynamic: tracks the captured texture's dimensions, which
/// change on every window resize. Authors divide pixel-space quantities
/// (frame top-left / size, radius, border width) by this to obtain the
/// texture's [0, 1] UV space.
inline constexpr const char* kUSurfaceSize = "uSurfaceSize";

/// `vec2 uSurfaceFrameTopLeft` — the content/frame rect's top-left
/// within `uTexture0`, in device pixels, measured top-down (origin at
/// the texture's top-left, +Y down). Because both runtimes capture more
/// than the bare frame (KWin's `OffscreenEffect` includes the
/// decoration + shadow margin; the daemon may pad the anchor), the
/// visible window frame sits at a non-zero offset inside the texture.
/// Per-frame-dynamic: shifts as the window moves / resizes within the
/// captured region. Combine with `uSurfaceFrameSize` and `uSurfaceSize`
/// to fold a frame-local sample into texture UV space.
inline constexpr const char* kUSurfaceFrameTopLeft = "uSurfaceFrameTopLeft";

/// `vec2 uSurfaceFrameSize` — the content/frame rect's size within
/// `uTexture0`, in device pixels. The visible window frame's pixel
/// extent (excluding the captured decoration / shadow margin).
/// Per-frame-dynamic: tracks the window's frame size across resizes.
/// Border / corner math is anchored to this rect, not to the full
/// `uSurfaceSize` texture extent.
inline constexpr const char* kUSurfaceFrameSize = "uSurfaceFrameSize";

/// `float uSurfaceScale` — the logical-to-device pixel scale of the
/// output the surface is on. A pack declares its appearance lengths
/// (border width, corner radius, …) in LOGICAL pixels and multiplies
/// them by this to reach the device-pixel space the geometry uniforms
/// (`uSurfaceSize` / `uSurfaceFrameTopLeft` / `uSurfaceFrameSize`) are
/// expressed in. Per-frame-dynamic: pushed every paint so a window that
/// moves to a differently-scaled output picks up the new scale without
/// any per-window state-change bookkeeping.
inline constexpr const char* kUSurfaceScale = "uSurfaceScale";

/// `float uSurfaceFocused` — how focused the window owning this surface
/// is, from `0.0` to `1.0`. A pack with active/inactive appearance (e.g.
/// the border's `p_activeColor` / `p_inactiveColor`) mixes its own
/// parameters on this rather than the host pre-resolving a single
/// focus-applied value. Per-frame-dynamic: focus toggles independently
/// of window content, so this is re-pushed every paint.
///
/// THE TWO RUNTIMES DISAGREE ON THE VALUES BETWEEN THE ENDS, and a pack
/// cannot tell which one it is running under. The compositor RAMPS it
/// toward the 0-or-1 target over the focus-fade duration setting, so it
/// takes every intermediate value on a real window. Daemon hosts (the
/// settings preview pane, OSD and popup decorations) push a hard `0.0`
/// or `1.0`, because the property behind it is a bool and there is no
/// clock on that side to drive a ramp. So a pack that writes
/// `mix(inactive, active, uSurfaceFocused)`, which the whole border
/// family does, cross-fades on a window and snaps in the preview.
///
/// Authoring rule: treat any value `>= 0.5` as focused rather than
/// testing `== 1.0`. That is a live rule, not future-proofing.
inline constexpr const char* kUSurfaceFocused = "uSurfaceFocused";

/// `float iTime` — continuously-increasing seconds for ANIMATED surface
/// packs (pulsing glow, shimmer, …), the same role iTime plays in the
/// overlay / animation categories. The host captures an epoch at first use
/// so the value begins near 0 (preserving float precision over a long
/// session). Per-frame-dynamic: re-pushed every paint. The linker drops it
/// for a static pack (e.g. the border), and the compositor only drives a
/// window to repaint when one of its packs actually references iTime, so a
/// static decoration costs nothing.
inline constexpr const char* kITime = "iTime";

/// `float uSurfaceOpacity` — a constant 1.0 on BOTH runtimes. Retained only
/// so the UBO layout and the pack-facing name survive; there is nothing to
/// read from it. It no longer carries the window's rule-resolved SetOpacity:
/// that was the retired `handlesOpacity` contract, SetOpacity is layer-backed
/// now (the plain opacity-tint layer folds it into its own pack param) and
/// in-pack content dimming is an ordinary pack parameter (frost/glass
/// `contentOpacity`). A host that fades a whole decoration does it through
/// `qt_Opacity`, not this, so a pack has no reason to sample it.
inline constexpr const char* kUSurfaceOpacity = "uSurfaceOpacity";

/// `sampler2D uBackdrop` — BOTH RUNTIMES, with different content. The scene BEHIND the window,
/// captured over the same (padded) canvas as `uTexture0` each frame for
/// packs that declare `"needsBackdrop": true` (frost / glass). Texel-aligned
/// with the composite canvas, so a pack samples both with the same uv (via
/// the `backdropTexel()` helper). The two runtimes declare it differently:
/// the compositor branch is a loose uniform with no binding, while the daemon
/// branch is `layout(binding = 15)`, sharing that slot with the overlay
/// category's wallpaper sampler. On the daemon a host may bind the desktop
/// wallpaper into it as a stand-in. Packs MUST still sample through
/// `backdropTexel()`, which returns transparent when nothing was bound.
inline constexpr const char* kUBackdrop = "uBackdrop";

/// `vec4 uBackdropRect` — a sub-rect of the bound backdrop in TOP-DOWN
/// normalized coords (xy = min, zw = size). BOTH runtimes have one, and they
/// answer DIFFERENT questions.
///
/// The compositor's answers "which texels of this window's own capture are
/// valid": canvas parts hanging off the output are never blitted, so
/// `backdropTexel()` clamps into this rect and an edge window does not smear
/// the cleared margin into its frost. It is a loose uniform, and this constant
/// is its name.
///
/// The daemon's answers "which slice of the shared image lies behind THIS
/// surface", because a daemon or preview host binds one desktop-sized wallpaper
/// to every surface it decorates. It is a UBO member rather than a loose
/// uniform (see SurfaceShaderUniforms), so this constant does not name it.
inline constexpr const char* kUBackdropRect = "uBackdropRect";

/// `float uHasBackdrop` — 1.0 when the host bound a backdrop this frame,
/// else 0.0. On the compositor that is the captured scene; on a daemon or
/// preview host it is the desktop wallpaper stand-in, when one is bound.
/// A needsBackdrop pack styles an explicit fallback on this
/// gate (e.g. a plain translucent tint) instead of assuming the sampler.
inline constexpr const char* kUHasBackdrop = "uHasBackdrop";

/// `vec4 iMouse` — cursor position for hover-reactive packs. `.xy` is the
/// cursor in the SAME top-down device-px space as the geometry uniforms
/// (origin at the padded canvas's top-left); negative when the cursor is
/// off the canvas — exactly `(-1, -1)` on the compositor, and the seeded
/// `(-1, -1)` sentinel scaled by the dpr on a daemon host that wires no
/// hover source (SurfaceShaderItem seeds it). `.zw` is `.xy` normalized
/// by `uSurfaceSize`, negative alongside the sentinel, so `iMouse.x < 0.0`
/// is the canonical off-surface test on both runtimes; do not test
/// `iMouse.x == -1.0` exactly.
///
/// A pack that reads iMouse does NOT need `"animated": true`, and is better
/// off without it. There IS a per-cursor-move damage path, and the compositor
/// drives it from the INTROSPECTED iMouse uniform rather than from any
/// metadata flag, comparing the cursor its fold keyed on so the repaints stop
/// as soon as the pointer does. Declaring `animated` asks instead for a
/// repaint every vsync for as long as the window is up. No daemon host wires
/// a hover source, so iMouse holds the off-surface sentinel there.
inline constexpr const char* kIMouse = "iMouse";

/// `sampler2D uTexture1..3` — user-declared image textures (metadata
/// `textures`: logo, mask, pattern). Slot N of the metadata list feeds
/// `uTexture<N+1>` (bindings 12-14 on the daemon; dedicated units on the
/// compositor). `iTextureResolution[i].xy` is the pixel size of `uTexture<i>`,
/// so metadata slot N's size is at `iTextureResolution[N+1]`, and index 0
/// belongs to `uTexture0`, the surface itself. Mind the two different
/// indices: the sampler names are one-based over the metadata list and
/// iTextureResolution is zero-based over the texture slots. Otherwise the
/// same slot layout as the animation contract, so the settings UI reuses the
/// same editor components. A slot with no loadable file reads transparent
/// black.
inline constexpr const char* kUTexture1 = "uTexture1";
inline constexpr const char* kUTexture2 = "uTexture2";
inline constexpr const char* kUTexture3 = "uTexture3";

/// `int iAudioSpectrumSize` — number of CAVA spectrum bars, or 0 when audio
/// is off. Declared in surface_uniforms.glsl (always present) and read by the
/// surface_audio.glsl helpers to gate every audio read: a pack that never
/// includes surface_audio.glsl leaves this unreferenced and the linker drops
/// it. Populated on both runtimes: the daemon writes the UBO member, and the
/// compositor pushes it as a loose int uniform from its own
/// CavaSpectrumProvider.
inline constexpr const char* kIAudioSpectrumSize = "iAudioSpectrumSize";

/// `sampler2D uAudioSpectrum` — the CAVA spectrum as a `bars×1` texture (R =
/// bar value in 0..1). Lives in surface_audio.glsl, not the UBO: `binding = 10`
/// on the daemon's RHI pipeline, a loose named sampler on the compositor's
/// classic-GL pipeline (the `#ifdef PLASMAZONES_KWIN` branch). Only sampled
/// while `iAudioSpectrumSize > 0`, so an unbound sampler is never read.
inline constexpr const char* kUAudioSpectrum = "uAudioSpectrum";

/// `vec4 customParams[N]` — per-effect declared parameter slots.
/// Cross-runtime element-name lookup constant: mirrored by (not consumed from) the kwin-effect's
/// `glGetUniformLocation("customParams[N]")` calls and as a
/// documentation anchor for shader authors. Symmetric with
/// `kCustomColorsArray` below.
inline constexpr const char* kCustomParamsArray = "customParams";

/// `vec4 customColors[N]` — per-effect declared color parameter slots.
/// Cross-runtime element-name lookup constant, symmetric with
/// `kCustomParamsArray` above: mirrored by (not consumed from) the kwin-effect's
/// `glGetUniformLocation("customColors[N]")` calls and as a
/// documentation anchor for shader authors.
///
/// Carries straight (non-premultiplied) RGBA: the encoder writes
/// `QColor::redF/greenF/blueF/alphaF` verbatim, so a 50%-alpha red
/// arrives at the shader as `(1.0, 0.0, 0.0, 0.5)` not
/// `(0.5, 0.0, 0.0, 0.5)`. Authors should premultiply manually if
/// their composite math expects it.
///
/// Naming asymmetry: the GLSL array is plural (`customColors[N]`) but
/// the slot-key the encoder/decoder pass through `QVariantMap` is
/// singular and 1-based (`customColor1` … `customColor16`), matching the
/// `customParams[N]` ↔ `customParamsN_<x|y|z|w>` pattern.
inline constexpr const char* kCustomColorsArray = "customColors";

/// Number of `vec4` slots in the `customParams` array (8). Forwards to
/// the canonical constant in `<PhosphorShaders/CustomParamsKey.h>` so a
/// single source of truth governs every shader category.
///
/// Note: this is the **vec4 slot count**, NOT the per-effect parameter
/// budget. The per-parameter budget is `kMaxParameterSlots` (32 sub-slots
/// across the 8 vec4s); a shader can declare up to 32 float/int/bool
/// parameters before `translateSurfaceParams` starts dropping overflow.
inline constexpr int kMaxCustomParams = PhosphorShaders::CustomParams::kVecCount;

/// Number of `customColors` vec4 slots (16) — the per-color-param
/// budget. Forwards to the canonical constant in
/// `<PhosphorShaders/CustomParamsKey.h>`.
inline constexpr int kMaxCustomColors = PhosphorShaders::CustomColors::kColorCount;

/// Number of float sub-slots (4 per vec4 × 8 vec4s = 32). Caps the count
/// of declared parameters a surface shader can carry. Forwards to the
/// canonical constant in `<PhosphorShaders/CustomParamsKey.h>`.
inline constexpr int kMaxParameterSlots = PhosphorShaders::CustomParams::kFlatSlotCount;

/// Maximum number of user-declared textures per surface effect.
///
/// Each declared texture binds to one of the canonical user-texture
/// samplers; the captured surface itself (`uTexture0`) is not counted
/// here — that's a separate runtime-managed slot. Pinned to 3 to match
/// the animation contract's `kMaxUserTextureSlots`, so surface and
/// animation packs share the same per-effect texture budget and the
/// settings UI can reuse the same editor components.
inline constexpr int kMaxUserTextureSlots = 3;

/// The accepted texture / buffer `wrap` vocabulary — thin forwarder onto
/// `PhosphorShaders::isValidWrapToken`, the cross-library canonical
/// predicate (see `<PhosphorShaders/CustomParamsKey.h>` for membership
/// and the empty-string contract). Kept here so surface call sites refer
/// to a name inside this contract namespace, matching the `slotKey`
/// forwarders below.
inline bool isValidWrapToken(const QString& wrap)
{
    return PhosphorShaders::isValidWrapToken(wrap);
}

/// The accepted buffer `filter` vocabulary — thin forwarder onto
/// `PhosphorShaders::isValidFilterToken` (see the canonical header for
/// the hoist rationale: hand-inlined validation sites drifted, and the
/// animation tree had none at all, so `"bufferFilter": "linaer"` was
/// accepted, silently coerced by the runtime, and re-persisted to disk).
inline bool isValidFilterToken(const QString& filter)
{
    return PhosphorShaders::isValidFilterToken(filter);
}

/// Format a `customParams` slot key — thin forwarder onto
/// `PhosphorShaders::CustomParams::slotKey`, the cross-library canonical
/// helper. Kept here so surface-shader call sites can refer to a name
/// inside this contract namespace and consumers don't need to import the
/// phosphor-shaders header directly. See
/// `<PhosphorShaders/CustomParamsKey.h>` for the format, the rationale,
/// and the full list of consumers. Both forms are live in-tree: the
/// compositor's resolveSurfaceParamValues calls this (vec, comp) form, while
/// translateSurfaceParams and the tests use the flat-slot form below.
inline QString slotKey(int vec, char comp)
{
    return PhosphorShaders::CustomParams::slotKey(vec, comp);
}

/// Flat-slot overload: `slot` is 0..31 across the 8 `vec4` slots.
inline QString slotKey(int slot)
{
    return PhosphorShaders::CustomParams::slotKey(slot);
}

/// Format a `customColor` slot key — thin forwarder onto
/// `PhosphorShaders::CustomColors::colorKey`. Sibling of `slotKey(int)`
/// for the customParams region. Kept here for the same reason: surface
/// call sites stay inside this contract namespace instead of leaking the
/// underlying phosphor-shaders header. See
/// `<PhosphorShaders/CustomParamsKey.h>` for the format and the
/// out-of-range graceful-degradation contract.
inline QString colorKey(int slot)
{
    return PhosphorShaders::CustomColors::colorKey(slot);
}

} // namespace SurfaceShaderContract

} // namespace PhosphorSurfaceShaders
