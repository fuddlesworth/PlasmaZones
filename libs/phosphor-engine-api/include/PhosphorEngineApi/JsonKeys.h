// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorGeometry/JsonKeys.h>

#include <QLatin1String>

namespace PhosphorEngineApi {
namespace JsonKeys {

using PhosphorGeometry::JsonKeys::Height;
using PhosphorGeometry::JsonKeys::Width;
using PhosphorGeometry::JsonKeys::X;
using PhosphorGeometry::JsonKeys::Y;

inline constexpr QLatin1String WindowId{"windowId"};
inline constexpr QLatin1String SourceZoneId{"sourceZoneId"};
inline constexpr QLatin1String TargetZoneId{"targetZoneId"};
inline constexpr QLatin1String TargetZoneIds{"targetZoneIds"};

} // namespace JsonKeys
} // namespace PhosphorEngineApi
