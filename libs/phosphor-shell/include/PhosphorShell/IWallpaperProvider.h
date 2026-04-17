// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorshell_export.h>

#include <QString>
#include <memory>

namespace PhosphorShell {

/// Abstract wallpaper provider interface.
/// Implementations detect the current desktop environment and return the
/// filesystem path to the active wallpaper image.
class PHOSPHORSHELL_EXPORT IWallpaperProvider
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
PHOSPHORSHELL_EXPORT std::unique_ptr<IWallpaperProvider> createWallpaperProvider();

} // namespace PhosphorShell
