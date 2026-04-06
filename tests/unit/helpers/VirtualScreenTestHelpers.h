// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/virtualscreen.h"

namespace PlasmaZones {
namespace TestHelpers {

/// Helper: create a VirtualScreenDef with the given parameters.
/// Shared across virtual screen test files to avoid duplication.
inline VirtualScreenDef makeDef(const QString& physId, int index, const QString& name, const QRectF& region)
{
    VirtualScreenDef def;
    def.id = VirtualScreenId::make(physId, index);
    def.physicalScreenId = physId;
    def.displayName = name;
    def.region = region;
    def.index = index;
    return def;
}

/// Helper: create a two-way 50/50 horizontal split config.
inline VirtualScreenConfig makeSplitConfig(const QString& physId)
{
    VirtualScreenConfig config;
    config.physicalScreenId = physId;
    config.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0, 0, 0.5, 1.0)));
    config.screens.append(makeDef(physId, 1, QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1.0)));
    return config;
}

/// Helper: create a three-way horizontal split config (33/34/33).
inline VirtualScreenConfig makeThreeWayConfig(const QString& physId)
{
    VirtualScreenConfig config;
    config.physicalScreenId = physId;
    config.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0, 0, 0.333, 1.0)));
    config.screens.append(makeDef(physId, 1, QStringLiteral("Center"), QRectF(0.333, 0, 0.334, 1.0)));
    config.screens.append(makeDef(physId, 2, QStringLiteral("Right"), QRectF(0.667, 0, 0.333, 1.0)));
    return config;
}

} // namespace TestHelpers
} // namespace PlasmaZones
