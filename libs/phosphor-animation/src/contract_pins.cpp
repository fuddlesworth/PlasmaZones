// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorRendering/ShaderNodeRhi.h>

// Compile-time pin: the contract's user-declarable texture budget plus the
// reserved surface slot (uTexture0) must equal the daemon's RHI texture-array
// capacity. If anyone resizes one without the other, the kwin/daemon contract
// silently drifts (declared textures past slot kMaxUserTextureSlots would land
// outside the SRB binding range on the daemon, or the daemon would reserve
// SRB slots no shader can address). Fail the build instead.
//
// Lives in its own TU rather than the contract header because including
// <PhosphorRendering/ShaderNodeRhi.h> there transitively pulls
// <QtQuick/QSGTextureProvider>, which conflicts with epoxy/gl.h (the kwin-
// effect path) on overlapping GL function-pointer typedefs.
static_assert(PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots + 1
                  == PhosphorRendering::kMaxUserTextures,
              "AnimationShaderContract::kMaxUserTextureSlots must match "
              "PhosphorRendering::kMaxUserTextures - 1 (slot 0 reserved for surface)");
