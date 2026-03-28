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
        // Require WAYLAND_DISPLAY as the primary check — it proves a compositor is
        // actually running. XDG_SESSION_TYPE alone can be "wayland" even when the
        // process is running under XWayland or the compositor hasn't started yet.
        const bool hasWaylandDisplay = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
        const bool sessionIsWayland = qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland");
        if (hasWaylandDisplay || (sessionIsWayland && !qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR"))) {
            qputenv("QT_WAYLAND_SHELL_INTEGRATION", "pz-layer-shell");
        }
    }
}

} // namespace PlasmaZones
