// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Compatibility wrapper — implementation moved to PhosphorShell library.

#pragma once

#include <PhosphorShell/IWallpaperProvider.h>

namespace PlasmaZones {
using IWallpaperProvider = PhosphorShell::IWallpaperProvider;
using PhosphorShell::createWallpaperProvider;
} // namespace PlasmaZones
