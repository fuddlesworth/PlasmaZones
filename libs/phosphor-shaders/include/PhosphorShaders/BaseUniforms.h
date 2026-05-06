// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>

namespace PhosphorShaders {

/// Time wrap period for float32 precision preservation.
/// See docs/phosphorshaders-api-design.md for details.
constexpr double kShaderTimeWrap = 1024.0;

/// GPU uniform buffer layout following std140 rules (base region).
///
/// This is the generic Shadertoy-compatible portion of the UBO. Consumers
/// append application-specific data after this block via IUniformExtension.
///
/// The render node allocates sizeof(BaseUniforms) + extension->extensionSize()
/// bytes for the UBO, writes the base region itself, and calls
/// extension->write() for the remainder.
///
/// @par appField0 / appField1 escape hatch
/// Two consumer-defined int fields at offsets 88–95. They exist regardless
/// of use because they fill the std140 alignment slot between iResolution
/// (vec2) and iMouse (vec4) — removing them would break the layout.
///
/// Use them when you have a small (≤2 ints), frequently-updated piece of
/// state that needs to live INSIDE BaseUniforms (rather than the extension
/// region) — typically because the fragment shader reads them on every
/// pixel and you want them on the same cache line as iResolution. For
/// larger or differently-shaped state, implement IUniformExtension.
///
/// The library writes them via the K_APP_FIELDS UBO region (8 bytes) so
/// frequent updates don't trigger a full scene-header re-upload. Consumers
/// that don't use them should leave them at 0.
struct alignas(16) BaseUniforms
{
    // Transform and opacity from Qt scene graph (offset 0)
    float qt_Matrix[16]; // mat4: 64 bytes at offset 0
    float qt_Opacity; // float: 4 bytes at offset 64

    // Shader timing (Shadertoy-compatible)
    float iTime; // float: 4 bytes at offset 68 (wrapped)
    float iTimeDelta; // float: 4 bytes at offset 72
    int iFrame; // int: 4 bytes at offset 76

    // Resolution
    float iResolution[2]; // vec2: 8 bytes at offset 80

    // Consumer-defined int slots — see class doc above for the escape-hatch
    // design rationale. Written via ShaderNodeRhi::setAppField0/1.
    int appField0; // offset 88
    int appField1; // offset 92

    // Mouse position
    float iMouse[4]; // vec4: 16 bytes at offset 96

    // Date/time: year, month (1-12), day (1-31), seconds since midnight
    float iDate[4]; // vec4: 16 bytes at offset 112

    // Custom shader parameters (32 float slots in 8 vec4s)
    float customParams[8][4]; // vec4[8]: 128 bytes at offset 128

    // Custom colors (16 color slots)
    float customColors[16][4]; // vec4[16]: 256 bytes at offset 256

    // Multi-pass: iChannelResolution[i] = buffer texture size
    float iChannelResolution[4][4]; // vec4[4]: 64 bytes at offset 512

    // Audio spectrum
    int iAudioSpectrumSize; // offset 576
    int iFlipBufferY; // offset 580 — always 1 for Y-flip
    // The two pad ints at offsets 584 and 588 are explicitly written as zero
    // by the C-side upload path (see shadernoderhiuniforms.cpp's syncBaseUniforms)
    // — readers should not assume the bytes are skipped on the wire. On the
    // GLSL side they are absorbed by std140's vec4 alignment of the following
    // `iTextureResolution` (vec4[4]) member, which forces the next field onto
    // a 16-byte boundary at offset 592. Removing the pad would shift the
    // GLSL view of `iTextureResolution` and break the
    // `data/animations/shared/animation_uniforms.glsl` byte-for-byte
    // contract pinned by the static_asserts below.
    int _pad_after_audioSpectrum[2]; // offset 584

    // User texture resolutions (bindings 7-10)
    float iTextureResolution[4][4]; // vec4[4]: 64 bytes at offset 592

    // Wrap-offset counterpart of iTime
    float iTimeHi; // offset 656

    // Direction signal for asymmetric leg rendering. 1 when the runtime
    // is driving this leg in the "reverse" direction (window.close /
    // going-to-minimized / unmaximize on the kwin path; hide leg on
    // the daemon path), 0 otherwise. Symmetric shaders ignore this and
    // rely on the runtime's iTime flip to auto-mirror; asymmetric
    // shaders (matrix's directional rain, rain windowAlpha trajectory,
    // anything where open and close differ in more than time direction)
    // branch on it.
    int iIsReversed; // offset 660

    // Surface-in-screen rect for spatial vertex / fragment effects.
    //   .xy = surface origin in logical-screen pixels (top-left of the
    //         shader surface relative to the screen the surface lives on)
    //   .zw = (screenWidth, screenHeight) in logical-screen pixels
    // Vertex shaders use this to compute "closest edge" / fly-in offsets;
    // fragment shaders that want screen-relative noise or edge fades read
    // it for the same reason. Lands at offset 664 (right after iIsReversed
    // at 660); std140 vec4 alignment auto-pads from 664 to a 16-byte
    // boundary at offset 672, which the C struct mirrors with
    // `_pad_before_iSurfaceScreenPos[2]`. Both runtimes populate it once
    // per leg attach + on every anchor/window geometry signal — see
    // SurfaceAnimator (daemon) and paint_pipeline.cpp (kwin-effect).
    int _pad_before_iSurfaceScreenPos[2]; // offset 664 — std140 vec4 alignment for the next field
    float iSurfaceScreenPos[4]; // offset 672 (16 bytes) — (surfaceX, surfaceY, screenW, screenH)

    // Anchor (card) pixel size in logical pixels. Decoupled from
    // `iResolution` because Qt's QQuickItem geometryChange handler
    // auto-resets `iResolution` to the shader item's bounds whenever
    // the item's geometry changes — for `boundsExtent: parent` shaders
    // the item is parent-sized and that auto-reset sweeps any explicit
    // setIResolution(anchor.size) override back to (parent.w, parent.h).
    // Vertex shaders that need to know the rendered card's size in pixels
    // (fly-in mapping the captured texture onto a small region within a
    // parent-sized FBO) read this directly. Daemon writes it on every
    // anchor geometry signal; kwin-effect writes it from frameGeometry.
    // .xy = (anchorWidth, anchorHeight); .zw reserved (zero today).
    // std140: vec2 alignment is 8 bytes — lands at offset 688 directly
    // after iSurfaceScreenPos with no implicit pad. Trailing 8 bytes
    // make up the std140 16-byte struct alignment; total size 696.
    float iAnchorSize[2]; // offset 688 (8 bytes)
    int _pad_after_iAnchorSize[2]; // offset 696 — std140 16-byte struct alignment, total 704
};

static_assert(sizeof(BaseUniforms) == 704, "BaseUniforms must be exactly 704 bytes");

// Per-field std140 offset asserts. These pin the layout the
// `data/animations/shared/animation_uniforms.glsl` canonical UBO branch
// depends on — that GLSL declaration is a byte-for-byte std140 prefix of
// `BaseUniforms`, which is what lets a single animation `effect.frag`
// source produce identical visuals on both runtimes (compositor classic
// GL via the canonical header's `#ifdef PLASMAZONES_KWIN` default-block
// branch, daemon Qt RHI via the `binding=0` UBO upload). If anyone
// reorders or inserts a field above any of these, the corresponding
// assert fails at compile time and the canonical GLSL header MUST be
// updated to match (and all in-tree `effect.frag` files re-baked, since
// their `customParams[N]` `#define` macros encode the slot positions).
//
// Every field declared in `animation_uniforms.glsl` is pinned here. A
// previous revision asserted only iTime / iResolution / customParams /
// customColors, which left the door open to a reorder that swapped
// {iTimeDelta, iFrame, _appField0, _appField1, iMouse, iDate} amongst
// themselves while preserving the four asserted offsets — silent
// miscompile for any future animation shader that reads the in-between
// fields. The full coverage below catches that.
static_assert(offsetof(BaseUniforms, qt_Matrix) == 0,
              "BaseUniforms::qt_Matrix must remain at std140 offset 0 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, qt_Opacity) == 64,
              "BaseUniforms::qt_Opacity must remain at std140 offset 64 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iTime) == 68,
              "BaseUniforms::iTime must remain at std140 offset 68 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iTimeDelta) == 72,
              "BaseUniforms::iTimeDelta must remain at std140 offset 72 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iFrame) == 76,
              "BaseUniforms::iFrame must remain at std140 offset 76 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iResolution) == 80,
              "BaseUniforms::iResolution must remain at std140 offset 80 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, appField0) == 88,
              "BaseUniforms::appField0 must remain at std140 offset 88 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, appField1) == 92,
              "BaseUniforms::appField1 must remain at std140 offset 92 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iMouse) == 96,
              "BaseUniforms::iMouse must remain at std140 offset 96 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iDate) == 112,
              "BaseUniforms::iDate must remain at std140 offset 112 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, customParams) == 128,
              "BaseUniforms::customParams must remain at std140 offset 128 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, customColors) == 256,
              "BaseUniforms::customColors must remain at std140 offset 256 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iChannelResolution) == 512,
              "BaseUniforms::iChannelResolution must remain at std140 offset 512 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iAudioSpectrumSize) == 576,
              "BaseUniforms::iAudioSpectrumSize must remain at std140 offset 576 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iFlipBufferY) == 580,
              "BaseUniforms::iFlipBufferY must remain at std140 offset 580 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iTextureResolution) == 592,
              "BaseUniforms::iTextureResolution must remain at std140 offset 592 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iTimeHi) == 656,
              "BaseUniforms::iTimeHi must remain at std140 offset 656 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iIsReversed) == 660,
              "BaseUniforms::iIsReversed must remain at std140 offset 660 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iSurfaceScreenPos) == 672,
              "BaseUniforms::iSurfaceScreenPos must remain at std140 offset 672 (animation UBO contract)");
static_assert(offsetof(BaseUniforms, iAnchorSize) == 688,
              "BaseUniforms::iAnchorSize must remain at std140 offset 688 (animation UBO contract)");

/// UBO region offsets and sizes for partial updates (reduces GPU bandwidth).
namespace UboRegions {

// Transform and opacity from Qt scene graph (mat4 + float)
constexpr size_t K_MATRIX_OPACITY_OFFSET = 0;
constexpr size_t K_MATRIX_OPACITY_SIZE = offsetof(BaseUniforms, iTime); // 68 bytes

// Animation time block (iTime, iTimeDelta, iFrame)
constexpr size_t K_TIME_BLOCK_OFFSET = offsetof(BaseUniforms, iTime);
constexpr size_t K_TIME_BLOCK_SIZE = sizeof(float) + sizeof(float) + sizeof(int); // 12 bytes

// App-fields block (appField0, appField1) — 8 bytes at offset 88.
// Uploaded as a tiny standalone region when ONLY the consumer's escape-hatch
// fields changed. Without this granular region, every appField update would
// trigger a full scene-header re-upload (~512 bytes).
constexpr size_t K_APP_FIELDS_OFFSET = offsetof(BaseUniforms, appField0);
constexpr size_t K_APP_FIELDS_SIZE = sizeof(int) * 2;

// Scene header: iResolution through end of struct.
// Covers iResolution, appFields, iMouse, iDate, customParams, customColors,
// iChannelResolution, iAudioSpectrumSize, iFlipBufferY, iTextureResolution,
// iTimeHi, iIsReversed, iSurfaceScreenPos, and any trailing std140 pad.
// Sized to the remainder of BaseUniforms so any field added at or after
// offset 80 (iResolution) is automatically covered by m_sceneDataDirty's
// upload path without needing a per-field dirty flag.
//
// History: an earlier revision capped K_SCENE_HEADER_SIZE at offsetof(iTimeHi),
// leaving the gap from offsetof(iTimeHi) + sizeof(iTimeHi) (660) to
// sizeof(BaseUniforms) (672) un-uploaded by partial-update paths. The
// new iIsReversed field at offset 660 landed exactly in that gap and
// silently failed to propagate to the GPU after the first full upload —
// asymmetric direction-aware shaders read stale values forever.
//
// WARNING: a new field added BEFORE iResolution (offset < 80) must
// extend K_MATRIX_OPACITY or land in its own region; this scene-header
// range only covers offsets [80, sizeof(BaseUniforms)).
constexpr size_t K_SCENE_HEADER_OFFSET = offsetof(BaseUniforms, iResolution);
constexpr size_t K_SCENE_HEADER_SIZE = sizeof(BaseUniforms) - K_SCENE_HEADER_OFFSET;

// iTimeHi block: a granular 4-byte upload region for the time-wrap-only
// path (m_timeHiDirty fires every ~30 s when iTime crosses the wrap
// boundary). Subsumed by K_SCENE_HEADER, so when m_sceneDataDirty also
// fires for the same frame the upload site below skips this granular
// write to avoid the duplicate 4-byte transfer.
constexpr size_t K_TIME_HI_OFFSET = offsetof(BaseUniforms, iTimeHi);
constexpr size_t K_TIME_HI_SIZE = sizeof(float);

// Verify K_SCENE_HEADER reaches end of BaseUniforms — a new field added
// at or after offset 80 must land inside this range. Without this assert,
// adding a field beyond the previous K_SCENE_HEADER end (the iTimeHi-
// based cap) would silently regress the gap-bug fixed by extending the
// region. Pin via the actual LAST field's offset+size so a developer
// who manually narrows K_SCENE_HEADER_SIZE (e.g. `= offsetof(iTimeHi) -
// K_SCENE_HEADER_OFFSET`, which is exactly the original regression)
// fails to build. The earlier formulation
// `K_SCENE_HEADER_OFFSET + K_SCENE_HEADER_SIZE == sizeof(BaseUniforms)`
// was tautological because K_SCENE_HEADER_SIZE is itself defined as
// `sizeof(BaseUniforms) - K_SCENE_HEADER_OFFSET` — the assert reduced
// to `sizeof == sizeof` and could not catch the regression it claimed
// to defend.
static_assert(offsetof(BaseUniforms, iAnchorSize) + sizeof(BaseUniforms::iAnchorSize)
                  <= K_SCENE_HEADER_OFFSET + K_SCENE_HEADER_SIZE,
              "K_SCENE_HEADER must cover iAnchorSize — "
              "narrowing K_SCENE_HEADER_SIZE leaves a trailing-field gap unmapped");
static_assert(K_SCENE_HEADER_OFFSET + K_SCENE_HEADER_SIZE == sizeof(BaseUniforms),
              "K_SCENE_HEADER must reach end-of-BaseUniforms — defensive companion to "
              "the trailing-field assert above");
// Verify K_TIME_HI is fully nested inside K_SCENE_HEADER so the upload
// site can skip the granular write when both dirty flags fire.
static_assert(K_TIME_HI_OFFSET >= K_SCENE_HEADER_OFFSET
                  && K_TIME_HI_OFFSET + K_TIME_HI_SIZE <= K_SCENE_HEADER_OFFSET + K_SCENE_HEADER_SIZE,
              "K_TIME_HI must be subsumed by K_SCENE_HEADER so a scene-data upload "
              "covers iTimeHi too without needing the m_timeHiDirty granular path");
// Verify K_APP_FIELDS is fully nested inside K_SCENE_HEADER for the
// same reason: the upload site uses an `else if` to skip the granular
// app-fields write when scene-data is also dirty (the broader upload
// already covers it). Pinning the nesting at compile time makes a
// future field-shuffle that breaks containment a build failure rather
// than a silent missed-upload regression.
static_assert(K_APP_FIELDS_OFFSET >= K_SCENE_HEADER_OFFSET
                  && K_APP_FIELDS_OFFSET + K_APP_FIELDS_SIZE <= K_SCENE_HEADER_OFFSET + K_SCENE_HEADER_SIZE,
              "K_APP_FIELDS must be subsumed by K_SCENE_HEADER so the scene-data upload "
              "covers appField0/appField1 too without needing the m_appFieldsDirty granular path");

// Total base size (for extension offset calculation)
constexpr size_t K_BASE_SIZE = sizeof(BaseUniforms);

} // namespace UboRegions

} // namespace PhosphorShaders
