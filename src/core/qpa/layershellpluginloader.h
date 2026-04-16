// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Compatibility wrapper — implementation moved to PhosphorShell library.
// PlasmaZones callers still use registerLayerShellPlugin() which now
// registers the PhosphorShell QPA plugin.

#pragma once

#include <PhosphorShell/LayerShellPluginLoader.h>

namespace PlasmaZones {

/// Registers the PhosphorShell QPA plugin for Wayland layer-shell surfaces.
/// Call before QGuiApplication construction.
inline void registerLayerShellPlugin()
{
    PhosphorShell::registerLayerShellPlugin();
}

} // namespace PlasmaZones
