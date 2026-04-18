// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorScreens/DBusScreenAdaptor.h>

#include <memory>

namespace PlasmaZones {

class Settings;
class SettingsConfigStore;

/**
 * @brief PlasmaZones-specific D-Bus adaptor.
 *
 * All the logic — screen queries, virtual-screen mutation, caches,
 * signal plumbing — lives in @ref Phosphor::Screens::DBusScreenAdaptor.
 * This subclass adds exactly three things:
 *
 *  1. `Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Screen")` so
 *     registrations go to the right interface name.
 *  2. A `setSettings(Settings*)` convenience that builds a
 *     `SettingsConfigStore` and wires it through to the base's
 *     `setConfigStore`. Matches the existing daemon init call
 *     sequence (ScreenAdaptor constructed before Settings).
 *  3. Ownership of the SettingsConfigStore (declared here so its
 *     lifetime is tied to the adaptor; the base holds a non-owning
 *     pointer into it).
 *
 * Interface name must match `dbus/org.plasmazones.Screen.xml` and
 * `DBus::Interface::Screen` for KCM signal connections.
 */
class PLASMAZONES_EXPORT ScreenAdaptor : public Phosphor::Screens::DBusScreenAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Screen")

public:
    explicit ScreenAdaptor(QObject* parent = nullptr);
    ~ScreenAdaptor() override;

    /// Wire the authoritative Settings instance. Builds a backing
    /// `SettingsConfigStore` and hands it to the base adaptor's
    /// `setConfigStore`.
    void setSettings(Settings* settings);

private:
    std::unique_ptr<SettingsConfigStore> m_virtualScreenStore;
};

} // namespace PlasmaZones
