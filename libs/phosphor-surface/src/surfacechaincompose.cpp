// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorSurface/SurfaceChainCompose.h"

#include "PhosphorSurface/SurfaceShaderEffect.h"
#include "PhosphorSurface/SurfaceShaderRegistry.h"

#include <QLatin1String>
#include <QUrl>
#include <QVariant>

namespace PhosphorSurfaceShaders {

double paddingRequest(const SurfaceShaderEffect& effect, const QVariantMap& friendlyParams)
{
    if (effect.paddingParam.isEmpty()) {
        return 0.0;
    }
    // Per-surface override wins over the declared default.
    if (friendlyParams.contains(effect.paddingParam)) {
        return friendlyParams.value(effect.paddingParam).toDouble();
    }
    for (const auto& param : effect.parameters) {
        if (param.id == effect.paddingParam) {
            return param.defaultValue.toDouble();
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
