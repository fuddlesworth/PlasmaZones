// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Canonical UBO declaration for animation/transition shaders. Both
// PlasmaZones runtimes that load animation shaders honor this layout:
//
//   • Daemon overlay-surface execution (PhosphorRendering::ShaderEffect →
//     ShaderNodeRhi → BaseUniforms UBO at binding=0). The UBO layout
//     below is std140-aligned with `PhosphorShaders::BaseUniforms` and
//     covers its full 672-byte footprint (the C struct's explicit pad
//     fields are absorbed here as implicit std140 alignment); the
//     daemon's binding=0 upload feeds the right values into every field.
//
//   • Compositor window-content execution (kwin-effect KWin::GLShader,
//     classic GL). KWin's shader pipeline does not bind UBOs, so the
//     kwin-effect runs a small source rewriter
//     (`AnimationShaderRegistry::rewriteCanonicalUboToDefaultBlock` in
//     libs/phosphor-animation-shaders) that strips this
//     `layout(std140, binding=0) uniform AnimationUniforms` block and
//     re-emits each field as a default-block uniform before handing the
//     source to KWin::ShaderManager::generateCustomShader. `qt_Matrix`
//     and `qt_Opacity` are dropped on the kwin path (KWin manages its
//     own scene-graph transform / opacity); `_appField0` / `_appField1`
//     are dropped (consumer escape-hatch ints — populated from
//     `BaseUniforms.appField0/1` on the daemon path, no equivalent
//     plumbing on the kwin path); `iFlipBufferY` is dropped (daemon-only
//     Y-flip signal, no equivalent on the kwin path).
//
// Author guidance: include this file from each animation shader's
// effect.frag with `#include <animation_uniforms.glsl>`. Do not declare
// these fields anywhere else in your shader. Per-effect declared
// parameters from metadata.json land in `customParams[N].xyz` slots in
// declaration order — see `AnimationShaderRegistry::translateAnimationParams`
// for the exact slot mapping (float/int/bool fill 32 sub-slots in 8
// vec4s, color fills `customColors[N]` vec4 slots).
//
// Layout-drift guard: the offsets below MUST stay aligned with
// `PhosphorShaders::BaseUniforms`. The C++ side enforces that via
// `static_assert(offsetof(...))` in `<PhosphorShaders/BaseUniforms.h>`
// for every field declared below — if any assert fails after a
// `BaseUniforms` change, this header has to move in lockstep. The bake
// test in `tests/unit/ui/test_animation_shader_bake.cpp` surfaces
// GLSL-side drift by running `qsb` over every built-in shader.

#ifndef PLASMAZONES_ANIMATION_UNIFORMS_GLSL
#define PLASMAZONES_ANIMATION_UNIFORMS_GLSL

// Std140 array stride note: `int[N]` and `float[N]` arrays have a per-
// element stride of 16 bytes in std140 (rule 4: rounded up to vec4
// alignment). That makes `int _pad0[2]` 32 bytes — NOT 8 — which would
// shove the next field 24 bytes past where the C `BaseUniforms` upload
// places it. Sibling daemon `data/shaders/common.glsl` solves this by
// declaring no explicit padding and relying on std140's natural
// vec4-alignment of the next array to bridge the gap. Match that
// pattern here: after `int iFlipBufferY` (ending at 584), the next
// `vec4 iTextureResolution[4]` is auto-aligned to offset 592 by std140
// (rule 4 → 16-byte boundary), implicitly filling the same 8 bytes the
// C struct's `_pad_after_audioSpectrum[2]` covers. Likewise the struct
// itself is auto-aligned to a 16-byte multiple at the end, picking up
// the trailing 12 bytes that C's `_pad_after_iTimeHi[3]` owns and
// landing total size at 672 bytes — matching `BaseUniforms`.
layout(std140, binding = 0) uniform AnimationUniforms {
    mat4 qt_Matrix;              // offset 0   (64 bytes) — Qt scene-graph transform; ignored by kwin path
    float qt_Opacity;            // offset 64  (4 bytes)  — Qt scene-graph opacity; ignored by kwin path
    float iTime;                 // offset 68  — animation progress in [0, 1]
    float iTimeDelta;            // offset 72  — real-time seconds between SurfaceAnimator
                                 //              ticks (daemon path); 0 on kwin path
    int iFrame;                  // offset 76  — per-leg frame counter starting at 0
                                 //              (daemon path); 0 on kwin path
    vec2 iResolution;            // offset 80  — surface size in logical pixels
    int _appField0;              // offset 88  — consumer escape-hatch int
                                 //              (BaseUniforms.appField0 on daemon path,
                                 //              dropped by kwin rewriter)
    int _appField1;              // offset 92  — consumer escape-hatch int
                                 //              (BaseUniforms.appField1 on daemon path,
                                 //              dropped by kwin rewriter)
    vec4 iMouse;                 // offset 96  — cursor position in shader-local pixels
                                 //              (daemon: QQuickHoverHandler-driven,
                                 //              .xy = position, (-1,-1) when off-region;
                                 //              kwin: emitted as default-block uniform
                                 //              but never bound by paintWindow → reads
                                 //              GLSL-default zero)
    vec4 iDate;                  // offset 112 — year, month, day, seconds-since-midnight
                                 //              (daemon: ShaderNodeRhi sync at 1 Hz;
                                 //              kwin: emitted but never bound → zero)
    vec4 customParams[8];        // offset 128 (128 bytes) — per-effect float/int/bool parameter slots
    vec4 customColors[16];       // offset 256 (256 bytes) — per-effect color parameter slots
    vec4 iChannelResolution[4];  // offset 512 (64 bytes)  — buffer texture sizes (multipass)
    int iAudioSpectrumSize;      // offset 576 — audio spectrum bin count
    int iFlipBufferY;            // offset 580 — always 1 (Y-flip); dropped by kwin rewriter
    // implicit 8-byte std140 padding here — the next vec4 array
    // auto-aligns to the 16-byte boundary at 592, matching C's
    // `_pad_after_audioSpectrum[2]` slot. Do NOT declare an explicit
    // `int _pad[2]` — std140 stride-16 arrays would mis-align this.
    vec4 iTextureResolution[4];  // offset 592 (64 bytes)  — user texture sizes (bindings 7-10)
    float iTimeHi;               // offset 656 — wrap-offset counterpart of iTime
    // implicit 12-byte trailing pad — std140 rounds the struct end up
    // to a 16-byte boundary, total 672 bytes, mirroring C's
    // `_pad_after_iTimeHi[3]`. Same caveat: do NOT add `float _pad[3]`.
};

#endif
