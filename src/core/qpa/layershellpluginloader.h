// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>

namespace PlasmaZones {

/// Call before QGuiApplication to register the pz-layer-shell QPA plugin.
/// Lightweight header — does not pull in Qt Wayland private headers.
/// Respects any existing QT_WAYLAND_SHELL_INTEGRATION value (e.g. for debugging).
/// Only sets the env var on Wayland sessions (XDG_SESSION_TYPE=wayland or
/// WAYLAND_DISPLAY set) to avoid confusing log output on X11.
inline void registerLayerShellPlugin()
{
    if (qEnvironmentVariableIsEmpty("QT_WAYLAND_SHELL_INTEGRATION")) {
        const bool isWayland = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
            || qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland");
        if (isWayland) {
            qputenv("QT_WAYLAND_SHELL_INTEGRATION", "pz-layer-shell");
        }
    }
}

} // namespace PlasmaZones
