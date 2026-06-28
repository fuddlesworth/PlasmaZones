// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/CustomParamsKey.h>

#include <QString>

namespace PhosphorAnimationShaders {

/// Cross-runtime named-uniform contract for **animation/transition shaders**.
///
/// Phosphor has two distinct shader registries:
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
///     events (`window.open`, `window.move`, `window.snapIn`, …).
///
///   • **Daemon (overlay-surface) execution** — `SurfaceAnimator::runLeg`
///     in the Phosphor daemon. Uses Qt RHI via
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
/// The canonical header (`data/animations/shared/animation_uniforms.glsl`)
/// has two `#ifdef`-selected branches:
///
///   • Default branch (daemon path): `layout(std140, binding = 0) uniform
///     AnimationUniforms { ... };` — std140-aligned with
///     `PhosphorShaders::BaseUniforms` covering its full footprint
///     (currently 672 bytes; pinned by BaseUniforms.h's static_asserts),
///     populated by Qt-RHI's binding=0 upload.
///
///   • `#ifdef PLASMAZONES_KWIN` branch (compositor path): plain
///     default-block `uniform float iTime;`-style declarations. The
///     kwin-effect prepends `#define PLASMAZONES_KWIN` after the
///     shader's `#version` line before passing the source to
///     `KWin::ShaderManager::generateCustomShader`, which selects this
///     branch. KWin's `KWin::GLShader::setUniform(loc, val)` API
///     addresses default-block uniforms only and never binds UBOs.
///
/// Shader authors do NOT need their own `#ifdef PLASMAZONES_KWIN` blocks
/// — both branches expose the same identifiers (`iTime`, `iResolution`,
/// `customParams[N]`, `iChannel0`, etc.). The macro switch describes the
/// uniform-binding ABI, not the runtime feature set.
///
/// @par Per-effect declared parameters
/// Every parameter declared in `metadata.json` lands in either a
/// `customParams[N].xyz` slot (float / int / bool) or a
/// `customColors[N]` slot (color), in declaration order. The two
/// allocators advance independently — a color parameter does NOT
/// consume a `customParams` sub-slot, so a `[color, float]` declaration
/// produces `customColors[0]` + `customParams[0].x`, not
/// `customColors[0]` + `customParams[0].y`. Float / int / bool
/// parameters fill `customParams[0].x` … `customParams[7].w` (32
/// slots); color parameters fill `customColors[0]` … `customColors[15]`
/// (16 slots).
///
/// `AnimationShaderRegistry::translateAnimationParams(effectId, friendlyMap)`
/// converts a friendly parameter map (e.g. `{"direction": 1, "parallax":
/// 0.2, "tint": "#ff8800"}`) into the slot-keyed map (e.g.
/// `{"customParams1_x": 1, "customParams1_y": 0.2,
/// "customColor1": QColor(0xff, 0x88, 0x00)}`) both runtimes consume.
/// Color values are coerced to QColor at that boundary — strings
/// parseable by the QColor constructor are accepted alongside QColor
/// instances; everything else falls back to the declared default, then
/// transparent.
///
/// @par Core animation contract
/// `iTime`, `iResolution`, `customParams[8]`, and `customColors[16]`
/// are the active contract fields populated by both runtimes.
///
/// @par Cross-runtime field coverage
/// Both runtimes populate the same core animation contract:
///
///   • `iTime` — per-leg progress in [0,1]
///   • `iResolution` — surface size
///   • `iTimeDelta` — wall-clock seconds since the previous paint tick
///     for this leg (0 on the first tick). Daemon: `SurfaceAnimator`'s
///     driver delta. Kwin: `paintWindow`'s steady-clock delta.
///   • `iFrame` — per-leg frame counter starting at 0 on each fresh
///     attach. Daemon: `SurfaceAnimator`'s per-leg counter. Kwin:
///     `ShaderTransition::frameCount`.
///   • `iDate` — local-time `(year, month, day, seconds-since-midnight)`.
///     Daemon: `ShaderNodeRhi` scene-data sync (throttled to 1 Hz).
///     Kwin: `QDateTime::currentDateTime()` per paint.
///   • `iMouse` — cursor position in shader-local logical pixels;
///     `(-1, -1)` when off-surface. Daemon: `QQuickHoverHandler` on
///     the attached shader item. Kwin: `effects->cursorPos()` minus
///     the window's frame-geometry origin, gated on `frameGeometry()`
///     containment.
///   • `customParams[N]` / `customColors[N]` — per-effect declared
///     parameters resolved at transition begin time.
///   • `iChannel0` — redirected surface texture. Daemon: live FBO
///     from the layer-enabled shader anchor. Kwin: KWin's
///     `OffscreenEffect`-managed window snapshot.
///
/// @par Daemon-only extensions
/// These fields receive zero values on the compositor path; shaders
/// that need them must run on the daemon overlay path until the
/// compositor wires the matching producers:
///
///   • `iAudioSpectrumSize` and the audio spectrum binding — fed by
///     `SurfaceAnimator::setAudioSpectrum` (the daemon's
///     `OverlayService` wires CAVA's `IAudioSpectrumProvider::spectrumUpdated`
///     here)
///   • `iChannelResolution[]` — auto-populated when multipass buffer
///     shaders are bound
///   • `iTextureResolution[]` — auto-populated when user textures are
///     bound (bindings 7-10)
///   • `iTimeHi` — auto-computed wrap counterpart of `iTime`
///
/// @par Spatial uniforms — per-runtime capture geometry
/// These four fields describe where the captured card sits inside the
/// texture / FBO the shader renders into. Both runtimes populate all
/// four — KWin via classic-GL `setUniform`, the daemon via
/// `AnimationUniformExtension` — so none are guarded by
/// `#ifdef PLASMAZONES_KWIN`. They matter because neither runtime
/// captures the bare card: KWin redirects the EXPANDED window geometry
/// (frame + decoration + shadow); the daemon captures the shader
/// anchor, which a PopupFrame enlarges past the card by a glow margin
/// so the glow animates with the card.
///
///   • `iAnchorSize` — the visible card's pixel size, logical pixels.
///   • `iAnchorPosInFbo` — the card's top-left within the FBO the
///     shader rasterises, logical pixels. `anchorRemap` consumes it to
///     fold surface-extent `vTexCoord` into card space; (0, 0) for
///     anchor-extent, where the vertex stage already delivers card-
///     space `vTexCoord`.
///   • `iAnchorRectInTexture` — the card's UV sub-rect within
///     `uTexture0`. `surfaceColor()` folds it in so a card-space [0, 1]
///     sample addresses the card's region of the padded texture. A
///     bare card-sized anchor carries the (0, 0, 1, 1) identity.
///   • `iSurfaceScreenPos` — the surface's global screen origin (.xy)
///     plus host screen size (.zw). Edge-distance math (fly-in's
///     nearest-edge pick) reads this.
///
/// `iFlipBufferY`, `qt_Matrix`, `qt_Opacity`, `_appField0` /
/// `_appField1` are daemon-only and absent from the canonical header's
/// `#ifdef PLASMAZONES_KWIN` branch entirely.
namespace AnimationShaderContract {

/// `float iTime` — transition progress in [0.0, 1.0]. Both runtimes
/// drive this from a 0..1 timeline (kwin: lifecycle elapsed/duration or
/// animator state value; daemon: `SurfaceAnimator::runLeg`'s
/// `shaderTime->start(0.0, 1.0, ...)`). Authors should `clamp(iTime,
/// 0.0, 1.0)` defensively in case a future timeline overshoots.
///
/// **Curve shape is NOT guaranteed to be linear.** Both runtimes feed
/// `iTime` through the resolved `Profile`'s easing curve:
///
///   • Compositor (kwin-effect): `paintWindow` reads progress from either
///     the time-based (`(now - startTimeMs) / durationMs`, linear) or
///     `m_windowAnimator->animationFor(w)` (curved by the geometry
///     animation's profile). Lifecycle events (`window.open`,
///     `window.close`, focus etc.) ride the linear branch;
///     `window.snap*` / `window.layoutSwitch` events ride the curved
///     one (their geometry tween drives progress).
///   • Daemon (SurfaceAnimator): the `shaderTime` AnimatedValue runs
///     under the resolved `showShaderProfile` / `hideShaderProfile`
///     curve, falling back to the opacity profile's curve when the
///     shader profile is empty. So an OutCubic opacity profile makes
///     `iTime` arrive on an OutCubic ramp, not a linear ramp.
///
/// Authors writing `mix(a, b, iTime)` should be aware: if the resolved
/// curve is non-linear, the visual blend follows that curve. If a shader
/// requires linear progress, register a Linear-curve profile and bind it
/// via `showShaderProfile`. Most transitions look better with a curved
/// `iTime` (the curve smooths the visual progression), so this is the
/// documented default rather than a forced linear override.
inline constexpr const char* kITime = "iTime";

/// `vec2 iResolution` — surface size in logical pixels. Window frame
/// size on the compositor execution site; overlay-surface size on the
/// daemon execution site.
inline constexpr const char* kIResolution = "iResolution";

/// `float iTimeDelta` — wall-clock seconds between consecutive paint
/// ticks that fed this transition. First tick of a fresh transition
/// reports `0.0`; subsequent ticks report `(now - lastPaint) / 1000`.
/// Daemon equivalent: `SurfaceAnimator`'s driver-tick delta, fed by
/// the same monotonic clock semantics.
inline constexpr const char* kITimeDelta = "iTimeDelta";

/// `int iFrame` — per-leg frame counter. Reset to 0 on every fresh
/// `beginShaderTransition` install (or supersession), increments by 1
/// on every paintWindow tick that feeds the transition. Daemon
/// equivalent: `SurfaceAnimator`'s per-leg frame counter — same 0-based
/// reset-on-attach semantic so shaders that key staggered effects off
/// `iFrame` see identical sequences on both runtimes.
inline constexpr const char* kIFrame = "iFrame";

/// `vec4 iDate` — local-time `(year, month, day, seconds-since-midnight)`.
/// Year and month are integers stored as floats (Year=2026.0, January=1.0);
/// day-of-month likewise; the .w component carries fractional seconds for
/// shaders that key on time-of-day. Read once per paint on the kwin path;
/// daemon throttles its sync to 1 Hz.
inline constexpr const char* kIDate = "iDate";

/// `int iIsReversed` — direction signal for asymmetric leg rendering.
/// Value is exactly `1` on reverse legs (window.close / going-to-
/// minimized / unmaximize on the kwin path; hide leg on the daemon
/// path) and exactly `0` on forward legs. The runtime also flips
/// iTime for reverse legs so symmetric shaders auto-mirror —
/// asymmetric shaders branch on this when the iTime flip alone can't
/// express the open-vs-close difference.
///
/// Authoring rule: branch with `iIsReversed == 1` (NOT `iIsReversed != 0`
/// or implicit-truthy). This pins the behaviour against any future
/// runtime that elects to extend the encoding (e.g. negative for
/// "unknown direction"). The matrix shader follows this convention.
inline constexpr const char* kIIsReversed = "iIsReversed";

/// `vec4 iSurfaceScreenPos` — the shader surface's position in screen
/// coords plus the host screen dimensions, both in logical pixels.
///   .xy = (surfaceX, surfaceY) — top-left of the shader surface
///         relative to the screen origin
///   .zw = (screenWidth, screenHeight) of the host screen
///
/// Daemon: written to the appended `AnimationUniformExtension` (UBO
/// offset 672 = sizeof(BaseUniforms)). The extension is installed by
/// `SurfaceAnimator::attachShaderToAnchor` so this field is animation-
/// only — it never lands in zone-shader UBOs (those use
/// `ZoneUniformExtension` instead).
///
/// kwin-effect: pushed via classic-GL `setUniform` keyed on this exact
/// name. Independent of the UBO mechanism — the UBO contract isolation
/// only matters on the daemon path.
///
/// Vertex / fragment shaders that need to know where the surface sits
/// on its host screen (fly-in from closest edge, screen-relative noise)
/// read this. Both runtimes populate it once per leg attach + on every
/// anchor or window geometry signal.
inline constexpr const char* kISurfaceScreenPos = "iSurfaceScreenPos";

/// `vec2 iAnchorSize` — captured anchor (card) pixel size in logical
/// pixels. Decoupled from `iResolution` so vertex shaders can rely on
/// it under any fboExtentKind; `iResolution` is auto-reset by Qt to the
/// shader item's bounds on every geometry event and would otherwise
/// clobber any anchor-size override on a `fboExtentKind: Surface` item.
/// Daemon-side written via `AnimationUniformExtension`; kwin-effect
/// uses classic-GL `setUniform`.
inline constexpr const char* kIAnchorSize = "iAnchorSize";

/// `vec2 iAnchorPosInFbo` is the anchor's top-left position inside the FBO,
/// in logical pixels. Combined with `iAnchorSize` and `iResolution`,
/// shaders compute the anchor's UV region for `vTexCoord` →
/// anchor-space remap (used by morph + broken-glass; previously did
/// this via `customParams[7].x` which is no longer reserved):
///   vec2 anchorTopLeftUv = iAnchorPosInFbo / iResolution;
///   vec2 anchorSizeUv    = iAnchorSize    / iResolution;
///   vec2 anchorUv        = (vTexCoord - anchorTopLeftUv) / anchorSizeUv;
/// Daemon-side written via `AnimationUniformExtension`; kwin-effect
/// uses classic-GL `setUniform`. On kwin the value is (0, 0) today
/// (the OffscreenEffect FBO covers the window's frameGeometry 1:1,
/// no actor expansion), which makes the anchor-space remap collapse
/// to identity. Equivalent to the pre-refactor `customParams[7].x = 0`
/// fallback that morph documented as the kwin path. A future actor-
/// expansion PR would push (padW, padH) instead.
inline constexpr const char* kIAnchorPosInFbo = "iAnchorPosInFbo";

/// `vec4 iAnchorRectInTexture` — the card's UV sub-rect within
/// `uTexture0`, as `(x, y, width, height)` in the texture's [0, 1]
/// space. Populated on BOTH runtimes: each captures more than the bare
/// card into `uTexture0`. KWin's `OffscreenEffect` redirects the whole
/// window item (decoration + shadow included), so `uTexture0` covers
/// the EXPANDED geometry. The daemon renders the shader anchor into
/// `uTexture0`, and a PopupFrame enlarges that anchor past the card by
/// a glow margin so the glow animates with the card. `surfaceColor()`
/// folds this rect in so a card-space [0, 1] sample addresses the
/// card's sub-region instead of stretching the whole texture across the
/// card (the "card animates smaller than it lands" artefact); on the
/// daemon `animation.vert` also divides `texCoord` by it so anchor-
/// extent shaders receive card-space `vTexCoord` in the first place. A
/// bare card-sized anchor with no capture margin carries the
/// `(0, 0, 1, 1)` identity. Daemon: written through
/// `AnimationUniformExtension` (UBO offset 704). kwin-effect: pushed
/// via classic-GL `setUniform`.
inline constexpr const char* kIAnchorRectInTexture = "iAnchorRectInTexture";

/// `vec4 iFromRect` / `vec4 iToRect` — geometry-morph endpoints in
/// logical screen pixels `(x, y, width, height)`. Used by the window
/// move/resize morph: the window jumps to its destination instantly via
/// `moveResize`, and the shader animates the visual transition by
/// interpolating the drawn quad from `iFromRect` (old frame) to `iToRect`
/// (new frame) by `iTime`, cross-fading the captured old content
/// (`uOldWindow`) into the live new content. Both default to `(0,0,0,0)`
/// for non-morph transitions (window.open/close/etc.), which a shader can
/// treat as "no morph". kwin-effect: pushed via classic-GL `setUniform`;
/// daemon: reserved in the UBO contract for source parity (the morph runs
/// compositor-side, but the shared header declares them on both branches).
inline constexpr const char* kIFromRect = "iFromRect";
inline constexpr const char* kIToRect = "iToRect";

/// `sampler2D uOldWindow` — snapshot of the window's content captured at
/// the old frame size just before the instant `moveResize`. The morph
/// shader cross-fades this (alpha `1 - iTime`) against the live new
/// content in `uTexture0` (alpha `iTime`), each mapped at native aspect,
/// so an aspect-ratio-changing resize doesn't stretch the content. Bound
/// to a dedicated texture unit on the kwin path; a transparent 1×1
/// fallback is bound when no snapshot was captured.
inline constexpr const char* kUOldWindow = "uOldWindow";

/// `float iWindowOpacity` — the window's effective rule-resolved opacity
/// in [0.0, 1.0], COMPOSITOR PATH ONLY. A `SetOpacity` rule must
/// dim the window for the whole duration of a transition, but the custom
/// transition shader is compiled `MapTexture`-only (no `Modulate` trait),
/// so KWin never applies `data.opacity()` to it — `surfaceColor()`
/// multiplies the premultiplied surface sample by this uniform instead.
/// The kwin-effect pushes it every frame via classic-GL `setUniform`,
/// defaulting to `1.0` for windows with no matching rule (a no-op dim).
///
/// Absent on the daemon path: overlay-surface animations have no
/// window-rule opacity (`SetOpacity` is a compositor window-rule feature),
/// so the canonical header declares this uniform — and folds it into
/// `surfaceColor()` — only inside the `#ifdef PLASMAZONES_KWIN` branch.
inline constexpr const char* kIWindowOpacity = "iWindowOpacity";

/// Maximum number of user-declared textures per animation effect.
///
/// Each declared texture binds to one of the canonical samplers
/// `iChannel1` / `iChannel2` / `iChannel3`. The redirected surface
/// itself (`iChannel0`, binding 7 on the daemon, TEXTURE0 on KWin) is
/// not counted here — that's a separate runtime-managed slot. The
/// daemon's `PhosphorRendering::kMaxUserTextures = 4` includes slot 0,
/// hence the off-by-one in the budget. Pinned to the daemon constant
/// by the `static_assert` in `libs/phosphor-animation/src/contract_pins.cpp`.
///
/// The compile-time pin against PhosphorRendering::kMaxUserTextures
/// lives in src/contract_pins.cpp — see the comment there explaining
/// why the assert can't sit in this header (epoxy/Qt typedef collision
/// in the kwin-effect TU).
inline constexpr int kMaxUserTextureSlots = 3;

/// `vec4 iMouse` — cursor position in shader-local pixels.
/// `.xy = (cursorX, cursorY)` relative to the shader surface's origin
/// (window frame origin on the kwin path; overlay surface origin on
/// the daemon path); `(-1, -1)` when the cursor is outside the shader's
/// surface. `.zw` are reserved for click state in the daemon overlay
/// contract; the kwin path leaves them at 0.
inline constexpr const char* kIMouse = "iMouse";

/// `vec4 customParams[N]` — per-effect declared parameter slots.
/// Cross-runtime element-name lookup constant: used by the kwin-effect's
/// `glGetUniformLocation("customParams[N]")` calls (the canonical header's
/// `#ifdef PLASMAZONES_KWIN` branch declares them as default-block uniforms
/// `customParams[0]..customParams[7]`) and as a documentation anchor for
/// shader authors. Symmetric with `kCustomColorsArray` below.
inline constexpr const char* kCustomParamsArray = "customParams";

/// Number of `vec4` slots in the `customParams` array (8). Forwards to
/// the canonical constant in `<PhosphorShaders/CustomParamsKey.h>` so a
/// single source of truth governs both libraries.
///
/// Note: this is the **vec4 slot count**, NOT the per-effect parameter
/// budget. The per-parameter budget is `kMaxParameterSlots` (32 sub-slots
/// across the 8 vec4s); a shader can declare up to 32 float/int/bool
/// parameters before `translateAnimationParams` starts dropping overflow.
/// The `kMaxCustomColors` constant below IS the per-color-param budget
/// (16) — this naming asymmetry exists because customColors slots are
/// whole-vec4 (one colour per slot) while customParams slots are
/// sub-vec4 (one float per `.xyzw` component).
inline constexpr int kMaxCustomParams = PhosphorShaders::CustomParams::kVecCount;

/// Number of float sub-slots (4 per vec4 × 8 vec4s = 32). Caps the count
/// of declared parameters an animation shader can carry without spilling
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

/// `vec4 customColors[N]` — per-effect declared color parameter slots.
/// Cross-runtime element-name lookup constant, symmetric with
/// `kCustomParamsArray` above: used by the kwin-effect's
/// `glGetUniformLocation("customColors[N]")` calls (the canonical
/// header's `#ifdef PLASMAZONES_KWIN` branch declares them as
/// default-block uniforms `customColors[0]..customColors[15]`) and as
/// a documentation anchor for shader authors.
///
/// Carries straight (non-premultiplied) RGBA: the encoder writes
/// `QColor::redF/greenF/blueF/alphaF` verbatim, so a 50%-alpha red
/// arrives at the shader as `(1.0, 0.0, 0.0, 0.5)` not
/// `(0.5, 0.0, 0.0, 0.5)`. Authors should premultiply manually if
/// their composite math expects it.
///
/// Naming asymmetry: the GLSL array is plural (`customColors[N]`) but
/// the slot-key the encoder/decoder pass through `QVariantMap` is
/// singular and 1-based (`customColor1` … `customColor16`). The
/// encoder produces the singular form directly via
/// `PhosphorShaders::CustomColors::colorKey(slot)` (a `"customColor"
/// + (slot+1)` join — no string-manipulation step). This matches the
/// `customParams[N]` ↔ `customParamsN_<x|y|z|w>` pattern — both choose
/// 1-based singular keys for QML/JSON friendliness while keeping the
/// GLSL declaration in 0-based plural form per std140 convention.
inline constexpr const char* kCustomColorsArray = "customColors";
inline constexpr int kMaxCustomColors = PhosphorShaders::CustomColors::kColorCount;

/// Format a `customColor` slot key — thin forwarder onto
/// `PhosphorShaders::CustomColors::colorKey`. Sibling of `slotKey(int)`
/// for the customParams region. Kept here for the same reason: animation
/// call sites stay inside this contract namespace instead of leaking the
/// underlying phosphor-shaders header. See
/// `<PhosphorShaders/CustomParamsKey.h>` for the format and the
/// out-of-range graceful-degradation contract.
inline QString colorKey(int slot)
{
    return PhosphorShaders::CustomColors::colorKey(slot);
}

/// @par Multipass limitation (compositor path)
/// Animation shaders may declare multipass buffer shaders, wallpaper,
/// and depth in their metadata. The daemon's SurfaceAnimator wires
/// these through to PhosphorRendering::ShaderEffect which has full
/// multipass support. However, the kwin-effect compositor path uses
/// KWin::GLShader via OffscreenEffect, which is single-pass with no
/// auxiliary FBOs. Multipass animation shaders degrade to single-pass
/// on the compositor with a diagnostic log.

/// @par Std140 offset contract
/// The canonical `data/animations/shared/animation_uniforms.glsl` UBO
/// declares its fields at the same byte offsets as
/// `PhosphorShaders::BaseUniforms` (the daemon's `binding=0` upload
/// struct). That alignment is what lets a single `effect.frag` source
/// run on both runtimes without per-runtime overrides.
///
/// The C++ side of the contract is pinned by `static_assert(offsetof(...))`
/// statements in `<PhosphorShaders/BaseUniforms.h>` for every field
/// declared in the GLSL UBO. If anyone reorders `BaseUniforms`, those
/// asserts fail at compile time and the canonical GLSL header has to
/// be updated to match. The GLSL side is exercised at build time by
/// `tests/unit/ui/test_animation_shader_bake.cpp`, which runs every
/// built-in animation shader through `qsb` (which in turn computes
/// std140 offsets) — a layout drift would surface there as a bake
/// failure.

} // namespace AnimationShaderContract

} // namespace PhosphorAnimationShaders

// The compile-time link between AnimationShaderContract::kMaxUserTextureSlots
// and PhosphorRendering::kMaxUserTextures lives in
// libs/phosphor-animation/src/contract_pins.cpp. It cannot live in this header
// because translation units that only need contract definitions (notably the
// kwin-effect, which already pulls in epoxy/gl.h via KWin) must not be forced
// to also pull in <QtQuick/QSGTextureProvider> via ShaderNodeRhi.h — epoxy and
// Qt's qopenglext.h declare overlapping GL function-pointer typedefs that
// conflict when both end up in the same TU.
