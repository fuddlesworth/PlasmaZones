// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Forwarding header — LayoutFactory now lives in phosphor-zones.
// Daemon code that included this header continues to work unchanged
// via the namespace alias below.

#include <PhosphorZones/LayoutFactory.h>

namespace PlasmaZones {
using LayoutFactory = PhosphorZones::LayoutFactory;
} // namespace PlasmaZones
