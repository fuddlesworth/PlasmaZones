// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorScreens/VirtualScreen.h>

namespace PlasmaZones {
namespace TestHelpers {

/// Helper: create a Phosphor::Screens::VirtualScreenDef with the given parameters.
/// Shared across virtual screen test files to avoid duplication.
inline Phosphor::Screens::VirtualScreenDef makeDef(const QString& physId, int index, const QString& name,
                                                   const QRectF& region)
{
    Phosphor::Screens::VirtualScreenDef def;
    def.id = PhosphorIdentity::VirtualScreenId::make(physId, index);
    def.physicalScreenId = physId;
    def.displayName = name;
    def.region = region;
    def.index = index;
    return def;
}

/// Helper: create a two-way 50/50 horizontal split config.
inline Phosphor::Screens::VirtualScreenConfig makeSplitConfig(const QString& physId)
{
    Phosphor::Screens::VirtualScreenConfig config;
    config.physicalScreenId = physId;
    config.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0, 0, 0.5, 1.0)));
    config.screens.append(makeDef(physId, 1, QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1.0)));
    return config;
}

/// Helper: create a three-way horizontal split config (33/34/33).
inline Phosphor::Screens::VirtualScreenConfig makeThreeWayConfig(const QString& physId)
{
    Phosphor::Screens::VirtualScreenConfig config;
    config.physicalScreenId = physId;
    config.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0, 0, 0.333, 1.0)));
    config.screens.append(makeDef(physId, 1, QStringLiteral("Center"), QRectF(0.333, 0, 0.334, 1.0)));
    config.screens.append(makeDef(physId, 2, QStringLiteral("Right"), QRectF(0.667, 0, 0.333, 1.0)));
    return config;
}

} // namespace TestHelpers
} // namespace PlasmaZones
