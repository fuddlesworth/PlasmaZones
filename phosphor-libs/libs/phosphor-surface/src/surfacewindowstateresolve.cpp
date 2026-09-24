// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorSurface/SurfaceWindowStateResolve.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <QLatin1String>

namespace PhosphorSurfaceShaders {
bool resolveWindowStateParams(const SurfaceShaderEffect& effect, QVariantMap& friendlyParams, bool maximized)
{
    if (!maximized) {
        return true;
    }
    const auto enabled = [&](QLatin1String id) {
        for (const auto& parameter : effect.parameters) {
            if (parameter.id == id) {
                return friendlyParams.value(QString(id), parameter.defaultValue).toBool();
            }
        }
        return false;
    };
    if (enabled(QLatin1String("hideWhenMaximized"))) {
        return false;
    }
    if (enabled(QLatin1String("squareWhenMaximized"))) {
        for (const auto& parameter : effect.parameters) {
            if (parameter.id == QLatin1String("cornerRadius")) {
                friendlyParams.insert(QStringLiteral("cornerRadius"), 0);
                break;
            }
        }
    }
    return true;
}
}
