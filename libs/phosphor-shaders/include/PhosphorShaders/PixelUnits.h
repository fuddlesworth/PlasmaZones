// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/phosphorshaders_export.h>

#include <QLatin1String>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PhosphorShaders {

/// The `unit` a parameter declares when its value is logical pixels of the
/// real surface the shader draws on (a window for a surface pack, the screen
/// for a zone/overlay pack).
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
/// A px parameter the caller left out of @p values is INSERTED at its scaled
/// default rather than skipped: an absent parameter falls back to the pack's
/// declared default further down the pipeline, which is the unscaled value
/// this function exists to avoid. Parameters with any other unit are passed
/// through, values that are not numbers are passed through, and the map keeps
/// every entry the caller put in it.
PHOSPHORSHADERS_EXPORT void scalePixelParams(const QVariantList& parameterInfos, QVariantMap& values, double factor);

/// Struct-list overload, for a caller holding parsed metadata rather than the
/// QVariantMap form a registry hands to QML. `ParamInfo` needs `id` and `unit`
/// QStrings and a `defaultValue` QVariant, which both
/// `ShaderRegistry::ParameterInfo` and `SurfaceShaderEffect::ParameterInfo`
/// have.
template<typename ParamInfo>
void scalePixelParams(const QList<ParamInfo>& parameterInfos, QVariantMap& values, double factor)
{
    QVariantList asMaps;
    asMaps.reserve(parameterInfos.size());
    for (const ParamInfo& info : parameterInfos) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), info.id);
        m.insert(QStringLiteral("unit"), info.unit);
        m.insert(QStringLiteral("default"), info.defaultValue);
        asMaps.append(m);
    }
    scalePixelParams(asMaps, values, factor);
}

} // namespace PhosphorShaders
