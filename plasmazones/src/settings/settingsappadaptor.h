// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusAbstractAdaptor>
#include <QString>

namespace PlasmaZones {

class SettingsLaunchController;

/**
 * @brief D-Bus adaptor for the settings app's single-instance launch surface.
 *
 * Implements the `org.plasmazones.SettingsController` interface documented in
 * `dbus/org.plasmazones.SettingsApp.xml`. Forwards calls to the parent
 * `SettingsLaunchController` so the `SettingsController` domain object stays
 * free of transport/Q_SCRIPTABLE concerns.
 *
 * Lifetime: constructed as a child of the launch controller; automatically
 * destroyed with it. Qt D-Bus discovers the adaptor on
 * `QDBusConnection::registerObject` with the default `ExportAdaptors` flag.
 */
class SettingsAppAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.SettingsController")

public:
    explicit SettingsAppAdaptor(SettingsLaunchController* launcher);
    ~SettingsAppAdaptor() override;

public Q_SLOTS:
    void setActivePage(const QString& page);

private:
    SettingsLaunchController* m_launcher; ///< Non-owning; parent object, guaranteed non-null.
};

} // namespace PlasmaZones
