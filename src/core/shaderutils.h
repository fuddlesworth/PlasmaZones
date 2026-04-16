// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Compatibility wrapper — implementation moved to PhosphorShell library.

#pragma once

#include <QLatin1String>
#include <QString>

// Forward the utility functions from PhosphorShell into PlasmaZones namespace.
// These are used by PlasmaZones code that hasn't been moved yet.

namespace PlasmaZones {

inline QString normalizeWrapMode(const QString& wrap)
{
    return (wrap == QLatin1String("repeat")) ? QStringLiteral("repeat") : QStringLiteral("clamp");
}

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
