// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorSurface/SurfaceChainCompose.h"

#include "PhosphorSurface/SurfaceShaderEffect.h"
#include "PhosphorSurface/SurfaceShaderRegistry.h"

#include <QLatin1String>
#include <QUrl>
#include <QVariant>

#include <cmath>

namespace PhosphorSurfaceShaders {

/// A padding request is usable only if it converts to a number AND is finite.
///
/// Both inputs cross a trust boundary: the declared default comes from an
/// installed pack's metadata.json, and the override comes from a stored
/// per-surface profile. QVariant::toDouble() answers 0.0 for anything it
/// cannot convert, so testing convertibility separately is what keeps a
/// wrong-typed override from silently reading as "no padding requested" and
/// suppressing the pack's declared default. Rejecting non-finite values here
/// keeps NaN and infinity out of the callers' clamps, where the compositor's
/// narrowing to int would otherwise be undefined.
static bool usablePadding(const QVariant& value, double* out)
{
    bool ok = false;
    const double v = value.toDouble(&ok);
    if (!ok || !std::isfinite(v)) {
        return false;
    }
    *out = v;
    return true;
}

double paddingRequest(const SurfaceShaderEffect& effect, const QVariantMap& friendlyParams)
{
    if (effect.paddingParam.isEmpty()) {
        return 0.0;
    }
    double value = 0.0;
    // Per-surface override wins over the declared default, but only when it is
    // actually a number: an unusable override falls through to the default
    // rather than collapsing the margin to zero.
    const auto override = friendlyParams.constFind(effect.paddingParam);
    if (override != friendlyParams.constEnd() && usablePadding(*override, &value)) {
        return value;
    }
    for (const auto& param : effect.parameters) {
        if (param.id == effect.paddingParam) {
            return usablePadding(param.defaultValue, &value) ? value : 0.0;
        }
    }
    // paddingParam names a parameter the pack does not declare: no room asked
    // for. Callers clamp anyway, so a bad name degrades to the margin-less
    // 1:1 geometry rather than to an unbounded canvas.
    return 0.0;
}

QVariantMap composeStageMap(const SurfaceShaderEffect& effect, const QVariantMap& resolvedParams)
{
    QVariantMap stageMap;
    // An unusable pack composes to nothing rather than to a half-formed stage.
    // translateSurfaceParams already returns an empty map for one, so without
    // this the result carries a preamble and an `animated` flag alongside an
    // empty `source` url and no params — a shape a host would still add to its
    // chain. Callers treat an empty map as "skip this stage".
    if (!effect.isValid()) {
        return stageMap;
    }
    stageMap.insert(QLatin1String("source"), QUrl::fromLocalFile(effect.fragmentShaderPath));
    stageMap.insert(QLatin1String("vertexSource"),
                    effect.vertexShaderPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(effect.vertexShaderPath));
    stageMap.insert(QLatin1String("preamble"), SurfaceShaderRegistry::paramPreamble(effect));
    stageMap.insert(QLatin1String("params"), SurfaceShaderRegistry::translateSurfaceParams(effect, resolvedParams));
    stageMap.insert(QLatin1String("animated"), effect.animated);

    // See the header: the emptiness half of this gate is what keeps a pack
    // whose builtin: buffer failed to resolve on the single-pass path.
    const bool stageMultipass = effect.isMultipass && !effect.bufferShaderPaths.isEmpty();
    stageMap.insert(QLatin1String("multipass"), stageMultipass);
    if (stageMultipass) {
        stageMap.insert(QLatin1String("bufferShaderPaths"), QVariant::fromValue(effect.bufferShaderPaths));
        stageMap.insert(QLatin1String("bufferFeedback"), effect.bufferFeedback);
        stageMap.insert(QLatin1String("bufferScale"), effect.bufferScale);
        stageMap.insert(QLatin1String("bufferWrap"), effect.bufferWrap);
        stageMap.insert(QLatin1String("bufferWraps"), QVariant::fromValue(effect.bufferWraps));
        stageMap.insert(QLatin1String("bufferFilter"), effect.bufferFilter);
        stageMap.insert(QLatin1String("bufferFilters"), QVariant::fromValue(effect.bufferFilters));
        stageMap.insert(QLatin1String("useDepthBuffer"), effect.useDepthBuffer);
    }
    return stageMap;
}

} // namespace PhosphorSurfaceShaders
