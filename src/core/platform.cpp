// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform.h"
#include <QGuiApplication>
#include <QString>

#ifdef HAVE_LAYER_SHELL
#include <LayerShellQt/Shell>
#endif

namespace PlasmaZones {

namespace Platform {

bool isWayland()
{
    // Check WAYLAND_DISPLAY environment variable
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        return true;
    }

    // Check XDG_SESSION_TYPE (used by some compositors)
    const QString sessionType = qEnvironmentVariable("XDG_SESSION_TYPE");
    if (sessionType.compare(QLatin1String("wayland"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    // Check QGuiApplication platform name (Qt 6.5+)
    if (const auto* app = qGuiApp) {
        const QString platformName = app->platformName();
        if (platformName.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

bool isX11()
{
    // Check DISPLAY environment variable
    if (qEnvironmentVariableIsEmpty("DISPLAY")) {
        return false;
    }

    // If not Wayland, assume X11
    return !isWayland();
}

QString displayServer()
{
    if (isWayland()) {
        return QStringLiteral("wayland");
    } else if (isX11()) {
        return QStringLiteral("x11");
    }
    return QStringLiteral("unknown");
}

bool hasLayerShell()
{
#ifdef HAVE_LAYER_SHELL
    // LayerShellQt is compiled in - runtime availability checked when creating windows
    // Attempting to get Shell instance would require a window, so just check compile-time availability
    // Runtime check happens in OverlayService::createOverlayWindow() via LayerShellQt::Window::get()
    return true;
#else
    return false;
#endif
}

bool hasOverlaySupport()
{
    // On Wayland, need LayerShellQt
    if (isWayland()) {
        return hasLayerShell();
    }

    // On X11, can use regular windows as overlay
    if (isX11()) {
        return true;
    }

    // Unknown platform - assume no support
    return false;
}

} // namespace Platform

} // namespace PlasmaZones
