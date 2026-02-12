// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform.h"
#include <QGuiApplication>
#include <QString>

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

bool isSupported()
{
    // PlasmaZones requires Wayland - X11 is not supported
    return isWayland();
}

} // namespace Platform

} // namespace PlasmaZones
