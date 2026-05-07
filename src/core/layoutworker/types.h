// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Forwarding header — types now live in phosphor-zones.
#include <PhosphorZones/LayoutComputeTypes.h>

namespace PlasmaZones {
using ZoneSnapshot = PhosphorZones::ZoneSnapshot;
using LayoutSnapshot = PhosphorZones::LayoutSnapshot;
using ComputedZoneGeometry = PhosphorZones::ComputedZoneGeometry;
using LayoutComputeResult = PhosphorZones::LayoutComputeResult;
} // namespace PlasmaZones
