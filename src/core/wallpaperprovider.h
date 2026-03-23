// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Abstract wallpaper provider interface.
//
// Returns the current desktop wallpaper path for use as a shader input
// texture (iChannel-style).  Implementations can read from KDE Plasma
// config, Hyprland/hyprpaper, sway/swww, GNOME gsettings, a user-
// configured path, or any other source.

#pragma once

#include <QString>
#include "plasmazones_export.h"

namespace PlasmaZones {

class PLASMAZONES_EXPORT IWallpaperProvider
{
public:
    virtual ~IWallpaperProvider() = default;

    /// Return the filesystem path to the current desktop wallpaper image.
    /// Returns empty string if unavailable or not applicable.
    virtual QString wallpaperPath() = 0;
};

/// Create the default wallpaper provider for the current desktop environment.
/// Detects KDE, Hyprland, Sway, GNOME and returns the appropriate implementation.
/// Returns a fallback provider (empty path) if no supported DE is detected.
PLASMAZONES_EXPORT std::unique_ptr<IWallpaperProvider> createWallpaperProvider();

} // namespace PlasmaZones
