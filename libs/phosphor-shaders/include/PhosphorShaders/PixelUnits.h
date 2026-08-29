// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/phosphorshaders_export.h>

#include <QLatin1String>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PhosphorShaders {

/// The `unit` a parameter declares when its value is logical pixels of the
/// real surface the shader draws on, which is the screen for a zone/overlay
/// pack. Only zone/overlay pack metadata declares this: the surface schema has
/// no `unit` field and nothing under `libs/phosphor-surface` reads one.
///
/// Every other parameter is unitless as far as the runtime is concerned: a
/// strength, an angle, a count, a colour. Only px values have to be re-read
/// when the surface is not its real size.
inline QLatin1String pixelUnit()
{
    return QLatin1String("px");
}

/// Scale every px-denominated parameter for a host that renders the shader on
/// a surface smaller than the real thing.
///
/// A shader's px parameters are absolute against the surface it draws on: a
/// 24px blur radius over a 1000px window is a light frost, and the SAME 24px
/// over a 240px preview card covers a tenth of it and swallows everything
/// underneath. So a preview that shrinks the surface has to shrink the pixel
/// values with it, or it shows a different effect than the one that will run —
/// and, when the same shader is previewed at two sizes, two different effects.
///
/// `factor` is the linear reduction (preview extent / real extent). 1.0, and
/// anything non-finite or non-positive, leaves @p values untouched.
///
/// ONE factor, taken from a single axis (width, at every current call site),
/// is applied to every px parameter. That is exact only while the preview
/// preserves the screen's aspect ratio, which the current preview hosts do NOT
/// — they scale their rects anisotropically, so a px parameter denominating a
/// VERTICAL length is still scaled by the horizontal ratio and comes out
/// slightly off. A second, per-axis factor is not the fix: nothing in the pack
/// metadata says which axis a parameter denominates, so there is no way to
/// pick the right one. The limitation is documented rather than papered over.
///
/// A px parameter the caller left out of @p values is INSERTED at its scaled
/// default rather than skipped: an absent parameter falls back to the pack's
/// declared default further down the pipeline, which is the unscaled value
/// this function exists to avoid. Parameters with any other unit are passed
/// through, values that are not numbers are passed through, and the map keeps
/// every entry the caller put in it. `image` parameters are skipped outright,
/// even if one declares px: their values are paths, and inserting a default
/// for one would manufacture the "the user set this" provenance that
/// `ShaderRegistry::translateParamsToUniforms` derives from key presence to
/// pick its absolute-path Trust-vs-Reject policy.
PHOSPHORSHADERS_EXPORT void scalePixelParams(const QVariantList& parameterInfos, QVariantMap& values, double factor);

} // namespace PhosphorShaders
