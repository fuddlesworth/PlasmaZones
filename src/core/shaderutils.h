// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QLatin1String>
#include <QString>

namespace PlasmaZones {

/// Normalize wrap mode: only "repeat" is recognized, everything else -> "clamp"
inline QString normalizeWrapMode(const QString& wrap)
{
    return (wrap == QLatin1String("repeat")) ? QStringLiteral("repeat") : QStringLiteral("clamp");
}

/// Normalize filter mode: "nearest" and "mipmap" are recognized, everything else -> "linear"
inline QString normalizeFilterMode(const QString& filter)
{
    if (filter == QLatin1String("nearest")) {
        return QStringLiteral("nearest");
    }
    if (filter == QLatin1String("mipmap")) {
        return QStringLiteral("mipmap");
    }
    return QStringLiteral("linear");
}

} // namespace PlasmaZones
