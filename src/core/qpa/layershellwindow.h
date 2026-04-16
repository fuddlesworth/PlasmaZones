// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Compatibility wrapper — implementation moved to PhosphorShell library.

#pragma once

namespace PhosphorShell {
class LayerShellWindow;
} // namespace PhosphorShell

namespace PlasmaZones {
using LayerShellWindow = PhosphorShell::LayerShellWindow;
} // namespace PlasmaZones
