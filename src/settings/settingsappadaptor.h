// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusAbstractAdaptor>
#include <QString>

namespace PlasmaZones {

class SettingsController;

/**
 * @brief D-Bus adaptor for the settings app's single-instance launch surface.
 *
 * Implements the `org.plasmazones.SettingsController` interface documented in
 * `dbus/org.plasmazones.SettingsApp.xml`. Forwards calls to the parent
 * `SettingsController` so the controller itself doesn't need
 * `Q_SCRIPTABLE` annotations for transport concerns.
 *
 * Lifetime: constructed as a child of the controller; automatically
 * destroyed with it. Qt D-Bus discovers the adaptor on
 * `QDBusConnection::registerObject` with the default `ExportAdaptors` flag.
 */
class SettingsAppAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.SettingsController")

public:
    explicit SettingsAppAdaptor(SettingsController* controller);
    ~SettingsAppAdaptor() override;

public Q_SLOTS:
    void setActivePage(const QString& page);

private:
    SettingsController* m_controller; ///< Non-owning; parent object.
};

} // namespace PlasmaZones
