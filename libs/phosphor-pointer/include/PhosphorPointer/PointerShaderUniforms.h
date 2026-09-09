// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/BaseUniforms.h>

#include <cstddef>

namespace PhosphorPointerShaders {

/// Byte offset of the pointer tail inside the UBO: it is appended directly
/// after `PhosphorShaders::BaseUniforms`.
constexpr size_t kPointerTailOffset = 672;

static_assert(sizeof(PhosphorShaders::BaseUniforms) == kPointerTailOffset,
              "BaseUniforms must remain 672 bytes: the pointer tail is pinned at UBO offset 672");

/// C++ mirror of the POINTER contract's UBO tail. The preview runtime's UBO
/// is `PhosphorShaders::BaseUniforms` (672 bytes, binding 0) followed
/// byte-for-byte by this struct, matching the `#else` (non-PLASMAZONES_KWIN)
/// branch of `data/pointer/shared/pointer_uniforms.glsl`. The compositor
/// pushes the same fields as loose uniforms and never touches this layout.
///
/// Every field is a `vec4` (or a `vec4` array), so std140 introduces no
/// padding and the offsets below are simply cumulative. The static_asserts
/// pin them: reorder or resize a field here and the corresponding assert
/// fails at compile time, at which point the GLSL block's documented offsets
/// MUST be updated to match.
struct alignas(16) PointerUniformsTail
{
    /// .xy device px/s, .z speed (length), .w unused.
    float uPointerVelocity[4]; // vec4: 16 bytes at tail offset 0, UBO offset 672

    /// .xy canvas px of the last press, .z seconds since (1e6 when none),
    /// .w button (1 left, 2 right, 3 middle, 0 none).
    float uPointerPress[4]; // vec4: 16 bytes at tail offset 16, UBO offset 688

    /// Same shape for the last release.
    float uPointerRelease[4]; // vec4: 16 bytes at tail offset 32, UBO offset 704

    /// .x pressed-button bitmask (1 left, 2 right, 4 middle), .y seconds since
    /// the last motion, .z logical-to-device scale, .w trail count (0..32).
    float uPointerState[4]; // vec4: 16 bytes at tail offset 48, UBO offset 720

    /// Cursor sprite rect in canvas px (x, y, w, h), hotspot applied.
    float uCursorRect[4]; // vec4: 16 bytes at tail offset 64, UBO offset 736

    /// .x = 1 when uCursorSprite is bound, .yzw reserved 0.
    float uPointerFlags[4]; // vec4: 16 bytes at tail offset 80, UBO offset 752

    /// Newest first: .xy canvas px, .z age seconds, .w speed at the sample.
    float uPointerTrail[32][4]; // vec4[32]: 512 bytes at tail offset 96, UBO offset 768
}; // total 608 bytes, UBO total 1280 bytes

static_assert(sizeof(PointerUniformsTail) == 608, "PointerUniformsTail must be exactly 608 bytes (pointer UBO tail)");
static_assert(sizeof(PhosphorShaders::BaseUniforms) + sizeof(PointerUniformsTail) == 1280,
              "BaseUniforms + PointerUniformsTail must total 1280 bytes (pointer UBO contract)");

static_assert(offsetof(PointerUniformsTail, uPointerVelocity) == 0,
              "PointerUniformsTail::uPointerVelocity must remain at tail offset 0 (UBO 672)");
static_assert(offsetof(PointerUniformsTail, uPointerPress) == 16,
              "PointerUniformsTail::uPointerPress must remain at tail offset 16 (UBO 688)");
static_assert(offsetof(PointerUniformsTail, uPointerRelease) == 32,
              "PointerUniformsTail::uPointerRelease must remain at tail offset 32 (UBO 704)");
static_assert(offsetof(PointerUniformsTail, uPointerState) == 48,
              "PointerUniformsTail::uPointerState must remain at tail offset 48 (UBO 720)");
static_assert(offsetof(PointerUniformsTail, uCursorRect) == 64,
              "PointerUniformsTail::uCursorRect must remain at tail offset 64 (UBO 736)");
static_assert(offsetof(PointerUniformsTail, uPointerFlags) == 80,
              "PointerUniformsTail::uPointerFlags must remain at tail offset 80 (UBO 752)");
static_assert(offsetof(PointerUniformsTail, uPointerTrail) == 96,
              "PointerUniformsTail::uPointerTrail must remain at tail offset 96 (UBO 768)");

} // namespace PhosphorPointerShaders
