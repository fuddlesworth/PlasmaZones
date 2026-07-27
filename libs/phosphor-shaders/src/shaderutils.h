// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QLatin1String>
#include <QString>

namespace PhosphorShaders {

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

} // namespace PhosphorShaders
