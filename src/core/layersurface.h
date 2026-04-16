// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Compatibility wrapper — PlasmaZones::LayerSurface is now PhosphorShell::LayerSurface.
// This header provides namespace aliases so existing PlasmaZones code compiles
// without changing every #include and qualified name.

#pragma once

#include <PhosphorShell/LayerSurface.h>

namespace PlasmaZones {

// Pull PhosphorShell types into PlasmaZones namespace for backwards compatibility.
using LayerSurface = PhosphorShell::LayerSurface;
namespace LayerSurfaceProps = PhosphorShell::LayerSurfaceProps;

} // namespace PlasmaZones

// Metatype already registered for PhosphorShell::LayerSurface* — alias it
// so qRegisterMetaType<PlasmaZones::LayerSurface*>() still resolves.
// (Both names point to the same type via the using declaration.)
