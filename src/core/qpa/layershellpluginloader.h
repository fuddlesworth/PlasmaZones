// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>

namespace PlasmaZones {

/// Call before QGuiApplication to register the pz-layer-shell QPA plugin.
/// Lightweight header — does not pull in Qt Wayland private headers.
/// Respects any existing QT_WAYLAND_SHELL_INTEGRATION value (e.g. for debugging).
/// Only sets the env var when WAYLAND_DISPLAY is set (proves a compositor is running).
inline void registerLayerShellPlugin()
{
    if (qEnvironmentVariableIsEmpty("QT_WAYLAND_SHELL_INTEGRATION")) {
        // WAYLAND_DISPLAY proves a compositor is actually running. We do NOT fall
        // back to XDG_SESSION_TYPE because it can be "wayland" on systemd machines
        // even in SSH sessions or before the compositor starts, and XDG_RUNTIME_DIR
        // is always set on systemd — so that combination is too permissive.
        if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
            qputenv("QT_WAYLAND_SHELL_INTEGRATION", "pz-layer-shell");
        }
    }
}

} // namespace PlasmaZones
