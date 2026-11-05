// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QStringList>

namespace PhosphorRendering {
class ShaderEffect;
}

namespace PhosphorAnimationShaders {
struct AnimationShaderEffect;
}

namespace PhosphorAnimationLayer {

/// Apply an animation pack's per-effect static configuration (fragment /
/// vertex source, param preamble, entry scaffold, include paths, multipass
/// buffers, wallpaper, depth) to a shader item.
///
/// This is the SAME code every production shader leg runs
/// (`SurfaceAnimator`'s attach and reuse paths both call it), exported so a
/// preview host can configure a shader item exactly the way a real leg
/// would. A preview that assembled its own static config would drift from
/// the thing it claims to predict. Idempotent — every `ShaderEffect` setter
/// no-ops on identity — and always-set: a repeat call after a metadata
/// hot-reload propagates disabled features (multipass, wallpaper, depth)
/// onto a reused item instead of leaving stale state.
///
/// Per-leg state — `iTime`, `isReversed`, translated parameters, geometry,
/// the uniform extension — is NOT this function's concern; the caller
/// threads those through separately, as the animator does.
PHOSPHORANIMATION_EXPORT void applyEffectStaticConfig(PhosphorRendering::ShaderEffect* shaderItem,
                                                      const PhosphorAnimationShaders::AnimationShaderEffect& effect,
                                                      const QStringList& shaderIncludePaths);

} // namespace PhosphorAnimationLayer
