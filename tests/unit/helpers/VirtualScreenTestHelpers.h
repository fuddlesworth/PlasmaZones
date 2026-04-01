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

} // namespace TestHelpers
} // namespace PlasmaZones
