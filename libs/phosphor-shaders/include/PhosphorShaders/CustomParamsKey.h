// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1Char>
#include <QString>

namespace PhosphorShaders {

/// Canonical key format for the `customParams[N].<x|y|z|w>` sub-slots in
/// `BaseUniforms`. Used as the cross-runtime serialisation of per-effect
/// declared parameter values when those values travel through a
/// `QVariantMap` between encoder (a registry's `translate*` method) and
/// decoder (a render path that writes them into the UBO).
///
/// Produces strings of the form `"customParams<N>_<x|y|z|w>"` where `N` is
/// 1-based (matching what GLSL authors write in their
/// `#define direction customParams[0].x` macros, plus one for the
/// daemon's UBO key parser).
///
/// Three concrete consumers of this format today:
///
///   • `PhosphorShaders::ShaderRegistry::ParameterInfo::uniformName()` —
///     overlay-shader encoder (uses an internal lookup-table form;
///     identical output)
///   • `PhosphorAnimationShaders::AnimationShaderRegistry::translateAnimationParams`
///     — animation-shader encoder
///   • `PhosphorRendering::ShaderEffect::setShaderParams` — decoder for
///     both runtime paths (overlay and animation)
///
/// Plus the kwin-effect's per-transition pack at compositor-side. All
/// four call sites consume the format produced here. If the format ever
/// changes — even just the leading `"customParams"` prefix or the
/// underscore separator — the change has to land here so every site
/// stays in sync.
namespace CustomParams {

/// Format a customParams slot key from explicit `(vec, comp)` pair.
/// `vec` is the 0..7 index into `customParams[8]` (the array slot);
/// `comp` is `'x'`, `'y'`, `'z'`, or `'w'` (the float sub-slot inside
/// the vec4). The vec index is rendered 1-based in the output to match
/// the GLSL-author convention.
inline QString slotKey(int vec, char comp)
{
    return QStringLiteral("customParams") + QString::number(vec + 1) + QLatin1Char('_') + QLatin1Char(comp);
}

/// Format a customParams slot key from a flat sub-slot index in [0, 32).
/// Slot 0 → `"customParams1_x"`, slot 4 → `"customParams2_x"`, etc. Caller
/// is responsible for bounds-checking against
/// `AnimationShaderContract::kMaxParameterSlots` (or equivalent); out-of-
/// range values produce keys that no decoder will match, which is the
/// graceful-degradation behaviour we want — values silently drop rather
/// than overflow into adjacent UBO regions.
inline QString slotKey(int slot)
{
    static constexpr char kComponents[4] = {'x', 'y', 'z', 'w'};
    return slotKey(slot / 4, kComponents[slot & 3]);
}

} // namespace CustomParams

} // namespace PhosphorShaders
