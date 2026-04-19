// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorScreens/DBusScreenAdaptor.h>

namespace Phosphor::Screens {
class ScreenManager;
}

namespace PlasmaZones {

/**
 * @brief PlasmaZones-specific D-Bus adaptor.
 *
 * All the logic — screen queries, virtual-screen mutation, caches,
 * signal plumbing — lives in @ref Phosphor::Screens::DBusScreenAdaptor.
 * This subclass exists only to add
 * `Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Screen")` so
 * registrations go to the right interface name.
 *
 * Wiring: the daemon constructs this with an explicit ScreenManager
 * pointer and separately calls the base's `setConfigStore` with its
 * SettingsConfigStore. Constructor-injection removes what was a hidden
 * read from the `PlasmaZones::screenManager()` service-locator at ctor
 * time — that made the adaptor implicitly depend on Daemon's own
 * initialisation order. One IConfigStore instance per process → single
 * change-signal channel.
 *
 * Interface name must match `dbus/org.plasmazones.Screen.xml` and
 * `DBus::Interface::Screen` for KCM signal connections.
 */
class PLASMAZONES_EXPORT ScreenAdaptor : public Phosphor::Screens::DBusScreenAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Screen")

public:
    explicit ScreenAdaptor(Phosphor::Screens::ScreenManager* manager, QObject* parent = nullptr);
    ~ScreenAdaptor() override;
};

} // namespace PlasmaZones
