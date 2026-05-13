// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServices/phosphorservices_export.h>

#include <PhosphorServices/UPowerDevice.h>

#include <QDBusObjectPath>
#include <QList>
#include <QObject>

namespace PhosphorServices {

class PHOSPHORSERVICES_EXPORT UPowerHost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool onBattery READ onBattery NOTIFY onBatteryChanged)
    Q_PROPERTY(PhosphorServices::UPowerDevice* displayDevice READ displayDevice NOTIFY displayDeviceChanged)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY deviceCountChanged)

public:
    explicit UPowerHost(QObject* parent = nullptr);
    ~UPowerHost() override;

    [[nodiscard]] bool onBattery() const;
    [[nodiscard]] UPowerDevice* displayDevice() const;
    [[nodiscard]] int deviceCount() const;
    [[nodiscard]] QList<UPowerDevice*> devices() const;
    [[nodiscard]] UPowerDevice* deviceAt(int index) const;

Q_SIGNALS:
    void onBatteryChanged();
    void displayDeviceChanged();
    void deviceAdded(PhosphorServices::UPowerDevice* device);
    void deviceRemoved(PhosphorServices::UPowerDevice* device);
    void deviceCountChanged();

private Q_SLOTS:
    void _q_onPropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& invalidated);
    void _q_onDeviceAdded(const QDBusObjectPath& path);
    void _q_onDeviceRemoved(const QDBusObjectPath& path);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServices
