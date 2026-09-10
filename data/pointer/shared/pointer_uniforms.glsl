// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Canonical uniform contract for POINTER shaders — the fourth shader-pack
// family, alongside overlay (data/overlays), animation (data/animations) and
// surface (data/surface). A pointer pack decorates the mouse pointer: trails,
// halos, click ripples, sparks. It is a CONTINUOUS decoration: the host ticks
// it every frame while the pointer has recent motion or a recent button event
// (its metadata `trailSeconds` window), then goes quiet. Event reactions (a
// click ring) are expressed through the event uniforms below inside the same
// continuous contract, so there is one family and one entry point.
//
// DUAL-RUNTIME, like the surface contract. The SAME pack source compiles for:
//
//   • The compositor (kwin-effect): a screen-space pass over the composited
//     scene of the output under the pointer. The effect prepends `#define
//     PLASMAZONES_KWIN` after `#version`, so the KWin branch below is taken;
//     uniforms are classic default-block uniforms set with GLShader::setUniform.
//
//   • The settings preview (PhosphorRendering Qt-RHI ShaderEffect): Qt-RHI's
//     SPIR-V pipeline mandates UBO-bound uniforms, so the #else branch binds a
//     std140 UBO at binding=0. Its first 672 bytes are byte-for-byte
//     PhosphorShaders::BaseUniforms (the shared base every RHI family uses);
//     the pointer tail follows at offset 672 and is mirrored by
//     PointerShaderUniforms.h (`struct PointerUniformsTail`) and written by
//     PointerUniformExtension.
//
// CANVAS: the whole output in DEVICE px, top-down, origin at the output's
// top-left. Every position uniform below is in that space and iResolution.xy
// is the output size in device px. Lengths a pack declares as parameters are
// LOGICAL px; multiply by pointerScale() (uPointerState.z) to reach device px.
//
// OUTPUT: packs return PREMULTIPLIED rgba composited source-over the scene
// (`glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)` on both runtimes). Return
// transparent black wherever nothing is painted, and fade to exactly zero
// within the pack's `trailSeconds`, because the host stops repainting then.

#ifndef PLASMAZONES_POINTER_UNIFORMS_GLSL
#define PLASMAZONES_POINTER_UNIFORMS_GLSL

#ifdef PLASMAZONES_KWIN

// ── Compositor branch — classic default-block uniforms ──────────────────────

// Seconds since this burst of pointer activity began: restarts at 0 for each
// burst and never wraps on the compositor. (The preview wraps it at 1024 s
// like every family; the base iTimeHi counterpart is not used by pointer
// packs on either runtime.)
uniform float iTime;

// The rest of the preview branch's BaseUniforms members, declared here too so
// that BOTH dialects accept the same source: a pack (or a shared helper) that
// names any base-contract identifier compiles on the compositor exactly as it
// does in the preview, instead of failing only on the path that ships. The
// compositor never sets them, so on this branch they read as zero. The two
// Qt scene-graph members, qt_Matrix and qt_Opacity, are deliberately absent:
// they are the preview's alone (its vertex stage reads qt_Matrix) and a pack
// that names them is preview-only by construction.
uniform float iTimeDelta;
uniform int iFrame;
uniform int _appField0;
uniform int _appField1;
uniform vec4 iDate;
uniform int iAudioSpectrumSize;
uniform int iFlipBufferY;
uniform float iTimeHi;
uniform int iIsReversed;

// Canvas size in device px.
uniform vec2 iResolution;

// Pointer position: .xy in canvas px, .zw = .xy / iResolution.
uniform vec4 iMouse;

// Pack-specific tweakable parameters (declared in metadata.json, addressed by
// `#define p_<id> customParamsN_x` / `customColorN` preambles the registry
// generates — identical to the other families).
uniform vec4 customParams[8];
uniform vec4 customColors[16];

// Multipass buffer-pass output SIZES: iChannelResolution[N].xy is the pixel
// size of iChannelN. The samplers themselves live in the opt-in
// pointer_multipass.glsl module.
uniform vec4 iChannelResolution[4];

// User-declared image textures (metadata `textures`): slot N feeds
// uTexture<N+1>, iTextureResolution[N].xy carries its pixel size.
uniform vec4 iTextureResolution[4];

// ── Pointer tail ────────────────────────────────────────────────────────────

// .xy pointer velocity in device px/s, .z speed (length of .xy), .w the
// filtered speed over the current stroke (read it through
// pointerFilteredSpeed()).
uniform vec4 uPointerVelocity;

// Last button PRESS: .xy canvas px, .z seconds since it (1e6 when there has
// been none this session), .w button (1 left, 2 right, 3 middle, 0 none).
uniform vec4 uPointerPress;

// Last button RELEASE, same shape as uPointerPress.
uniform vec4 uPointerRelease;

// .x pressed-button bitmask as a float (1 left, 2 right, 4 middle),
// .y seconds since the last motion (1e6 before any motion this session),
// .z logical-to-device scale, .w trail point count actually filled (0..32).
uniform vec4 uPointerState;

// Cursor sprite rect in canvas px (x, y, w, h), hotspot already applied so it
// is where the sprite is drawn. (0,0,0,0) when unknown.
uniform vec4 uCursorRect;

// .x = 1.0 when uCursorSprite is bound (metadata `needsCursor` honoured),
// else 0.0.
// .y = this pack's resolved reach in DEVICE px — the radius the host inflates
//      the damage rect by around every live sample. Read it through
//      pointerReach() and clamp your own extents to it: nothing painted
//      further out reaches the screen. .z .w reserved, always 0.
uniform vec4 uPointerFlags;

// Trail history, NEWEST FIRST. .xy canvas px, .z age in seconds (0 = this
// frame), .w speed at that sample (device px/s). Entries at or past
// uPointerState.w are zero.
uniform vec4 uPointerTrail[32];

// The cursor sprite, bound only when the pack declares `needsCursor`.
uniform sampler2D uCursorSprite;

// User-declared image textures (metadata `textures`), bound at draw time.
uniform sampler2D uTexture1;
uniform sampler2D uTexture2;
uniform sampler2D uTexture3;

#else

// ── Preview branch — std140 UBO at binding 0 ────────────────────────────────
// The leading 672 bytes are PhosphorShaders::BaseUniforms verbatim (the same
// prefix animation_uniforms.glsl declares), so BaseUniformProfile fills them
// unchanged. The pointer tail starts at offset 672. Field order is std140:
// every member below the base is a vec4 or a vec4 array, so there is no
// padding anywhere in the tail.
layout(std140, binding = 0) uniform PointerUniforms {
    mat4 qt_Matrix;              // offset 0   (64)  — Qt scene-graph transform; preview-only
    float qt_Opacity;            // offset 64  (4)   — Qt scene-graph opacity; preview-only
    float iTime;                 // offset 68  (4)   — seconds (wrapped)
    float iTimeDelta;            // offset 72  (4)   — seconds since the previous frame
    int iFrame;                  // offset 76  (4)   — frame counter
    vec2 iResolution;            // offset 80  (8)   — canvas size, device px
    int _appField0;              // offset 88  (4)   — base escape-hatch int, unused here
    int _appField1;              // offset 92  (4)   — base escape-hatch int, unused here
    vec4 iMouse;                 // offset 96  (16)  — pointer .xy canvas px, .zw normalized
    vec4 iDate;                  // offset 112 (16)  — year, month, day, seconds-since-midnight
    vec4 customParams[8];        // offset 128 (128) — scalar parameter slots
    vec4 customColors[16];       // offset 256 (256) — colour parameter slots
    vec4 iChannelResolution[4];  // offset 512 (64)  — multipass buffer sizes (.xy)
    int iAudioSpectrumSize;      // offset 576 (4)   — base member, always 0 here
    int iFlipBufferY;            // offset 580 (4)   — base member, always 1
    // implicit 8-byte std140 pad here — vec4[] below is 16-aligned.
    vec4 iTextureResolution[4];  // offset 592 (64)  — user texture sizes (.xy)
    float iTimeHi;               // offset 656 (4)   — wrap counterpart of iTime, unused
    int iIsReversed;             // offset 660 (4)   — base member, always 0 here
    // implicit 8-byte std140 pad here — base region ends at 672.

    // ── pointer tail (PointerUniformsTail, 608 bytes) ──
    vec4 uPointerVelocity;       // offset 672 (16)  — .xy device px/s, .z speed, .w filtered speed
    vec4 uPointerPress;          // offset 688 (16)  — .xy canvas px, .z seconds since (1e6 = none), .w button
    vec4 uPointerRelease;        // offset 704 (16)  — same shape as uPointerPress
    vec4 uPointerState;          // offset 720 (16)  — .x buttons mask, .y idle s, .z scale, .w trail count
    vec4 uCursorRect;            // offset 736 (16)  — cursor sprite rect, canvas px (x, y, w, h)
    vec4 uPointerFlags;          // offset 752 (16)  — .x has cursor sprite; .y reach in device px; .zw reserved 0
    vec4 uPointerTrail[32];      // offset 768 (512) — newest first: .xy px, .z age s, .w speed
};                               // total 1280 bytes, no trailing pad

// The cursor sprite (metadata `needsCursor`), binding 7 — the slot the surface
// family gives uTexture0, which pointer packs do not have.
layout(binding = 7) uniform sampler2D uCursorSprite;
// User-declared image textures (metadata `textures`), bindings 8-10 — the
// same sampler-name and binding-point dialect the other families use,
// provided by the base ShaderEffect's user-texture plumbing.
layout(binding = 8) uniform sampler2D uTexture1;
layout(binding = 9) uniform sampler2D uTexture2;
layout(binding = 10) uniform sampler2D uTexture3;

// The multipass iChannel sampler bindings (2-5) live in pointer_multipass.glsl,
// which a multipass pack includes.

#endif // PLASMAZONES_KWIN

// ─── Final-colour hook (HDR colour management) ─────────────────────────
// The generated entry main() routes its fragColor write through
// PZ_FINALIZE_COLOR(...). The guarded default below is identity, which is
// right for the preview and for every buffer stage (an intermediate target
// stays in the pack's own colour space).
//
// The compositor's MAIN pass overrides it before this header is included:
// pointerdecorationshader.cpp splices KWin's colormanagement.glsl plus
// `#define PZ_FINALIZE_COLOR(c) pzFinalizeColor(c)` after `#version`. A
// pointer pack authors new sRGB content and composites it straight into
// KWin's blending space, which on an HDR or wide-gamut output is not sRGB —
// without the conversion every pack reads dim and desaturated there. Same
// mechanism as the animation family's; the strip and desktop-switch passes
// deliberately do NOT convert because their inputs are captures that already
// live in the blending space.
#ifndef PZ_FINALIZE_COLOR
#define PZ_FINALIZE_COLOR(c) (c)
#endif

#endif // PLASMAZONES_POINTER_UNIFORMS_GLSL
