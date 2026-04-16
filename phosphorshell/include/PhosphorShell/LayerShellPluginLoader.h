// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>

namespace PhosphorShell {

/// Call before QGuiApplication to register the phosphorshell QPA plugin.
/// Lightweight header — does not pull in Qt Wayland private headers.
/// Respects any existing QT_WAYLAND_SHELL_INTEGRATION value (e.g. for debugging).
/// Only sets the env var when WAYLAND_DISPLAY is set (proves a compositor is running).
///
/// Note: if the application is started before the compositor sets WAYLAND_DISPLAY
/// (e.g. early systemd unit ordering), this will not register the plugin and
/// windows will fall back to xdg_toplevel. Ensure the application's systemd unit
/// has After=graphical-session.target or equivalent to guarantee the compositor
/// is running before the application starts.
inline void registerLayerShellPlugin()
{
    if (qEnvironmentVariableIsEmpty("QT_WAYLAND_SHELL_INTEGRATION")) {
        // WAYLAND_DISPLAY proves a compositor is actually running. We do NOT fall
        // back to XDG_SESSION_TYPE because it can be "wayland" on systemd machines
        // even in SSH sessions or before the compositor starts, and XDG_RUNTIME_DIR
        // is always set on systemd — so that combination is too permissive.
        //
        // Also check XDG_RUNTIME_DIR/wayland-0 as a fallback: some compositors
        // (e.g. COSMIC) or systemd socket-activation setups may not set
        // WAYLAND_DISPLAY but the socket exists at the default path.
        if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
            qputenv("QT_WAYLAND_SHELL_INTEGRATION", "phosphorshell");
        } else {
            // Fallback: check for common Wayland sockets. Compositors may use any
            // socket name (wayland-0, wayland-1 for nested sessions, or custom names).
            // We check the two most common defaults. COSMIC and some socket-activation
            // setups may not set WAYLAND_DISPLAY but the socket still exists.
            const QByteArray runtimeDir = qgetenv("XDG_RUNTIME_DIR");
            if (!runtimeDir.isEmpty()) {
                const QString runtimePath = QString::fromUtf8(runtimeDir);
                if (QFile::exists(runtimePath + QStringLiteral("/wayland-0"))
                    || QFile::exists(runtimePath + QStringLiteral("/wayland-1"))) {
                    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "phosphorshell");
                }
            }
        }
    }
}

} // namespace PhosphorShell
