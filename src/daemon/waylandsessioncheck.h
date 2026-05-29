// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>

namespace PlasmaZones {

// Resolve the filesystem path of the Wayland display socket that Qt's wayland
// QPA plugin would attempt to connect to, given the WAYLAND_DISPLAY and
// XDG_RUNTIME_DIR environment values.
//
//   - An absolute WAYLAND_DISPLAY (starts with '/') is used verbatim;
//     XDG_RUNTIME_DIR is irrelevant.
//   - A relative WAYLAND_DISPLAY resolves against XDG_RUNTIME_DIR.
//   - An empty/unset WAYLAND_DISPLAY falls back to Qt's default "wayland-0"
//     display name (also resolved against XDG_RUNTIME_DIR). This is the case
//     that previously slipped past the startup guard in main(): with no
//     WAYLAND_DISPLAY the guard did nothing, Qt's wayland QPA failed to create
//     a wl_display, the xcb fallback also failed, and QGuiApplication's
//     constructor called qFatal() → SIGABRT → core dump (see the crash analysis
//     and queryPlasmaWorkspaceState() in daemon.cpp).
//
// Returns an empty QString when no path can be formed — a relative or empty
// display name with no XDG_RUNTIME_DIR — which means there is no Wayland
// session for this process to connect to.
inline QString resolveWaylandSocketPath(const QByteArray& waylandDisplay, const QByteArray& runtimeDir)
{
    if (waylandDisplay.startsWith('/')) {
        return QString::fromLocal8Bit(waylandDisplay);
    }

    const QByteArray displayName = waylandDisplay.isEmpty() ? QByteArrayLiteral("wayland-0") : waylandDisplay;
    if (runtimeDir.isEmpty()) {
        return QString();
    }
    return QString::fromLocal8Bit(runtimeDir + '/' + displayName);
}

} // namespace PlasmaZones
