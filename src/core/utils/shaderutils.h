// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Compatibility wrapper — forwards to PhosphorShaders implementations.

#pragma once

#include <PhosphorShaders/ShaderRegistry.h> // pulls in phosphorshaders_export.h

// PhosphorShaders has the canonical implementations in its internal shaderutils.h.
// Re-declare identical inline functions here for PlasmaZones code that hasn't
// been ported to use PhosphorShaders directly.

#include <QLatin1String>
#include <QString>

namespace PlasmaZones {

inline QString normalizeWrapMode(const QString& wrap)
{
    if (wrap == QLatin1String("repeat"))
        return QStringLiteral("repeat");
    if (wrap == QLatin1String("mirror"))
        return QStringLiteral("mirror");
    return QStringLiteral("clamp");
}

inline QString normalizeFilterMode(const QString& filter)
{
    if (filter == QLatin1String("nearest"))
        return QStringLiteral("nearest");
    if (filter == QLatin1String("mipmap"))
        return QStringLiteral("mipmap");
    return QStringLiteral("linear");
}

} // namespace PlasmaZones
