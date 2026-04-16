// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Compatibility wrapper — implementation moved to PhosphorShell library.
// PlasmaZones code that previously included this header only needs the
// LayerShellIntegration type for instance() and layerShell() calls.
// The actual class definition is compiled within PhosphorShell's target
// where the generated protocol headers are on the include path.

#pragma once

// Forward-declare enough for PlasmaZones code that calls instance()/layerShell().
// The full definition is only needed inside PhosphorShell's QPA plugin sources.
namespace PhosphorShell {
class LayerShellIntegration;
} // namespace PhosphorShell

namespace PlasmaZones {
using LayerShellIntegration = PhosphorShell::LayerShellIntegration;
} // namespace PlasmaZones
