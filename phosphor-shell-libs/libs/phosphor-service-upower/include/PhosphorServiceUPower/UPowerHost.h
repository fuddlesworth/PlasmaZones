// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceUPower/phosphorserviceupower_export.h>

#include <PhosphorServiceUPower/UPowerDevice.h>

#include <QDBusObjectPath>
#include <QList>
#include <QObject>

namespace PhosphorServiceUPower {

class PHOSPHORSERVICEUPOWER_EXPORT UPowerHost : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(UPowerHost)
    Q_PROPERTY(bool onBattery READ onBattery NOTIFY onBatteryChanged)
    Q_PROPERTY(PhosphorServiceUPower::UPowerDevice* displayDevice READ displayDevice NOTIFY displayDeviceChanged)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY deviceCountChanged)

public:
    explicit UPowerHost(QObject* parent = nullptr);
    ~UPowerHost() override;

    [[nodiscard]] bool onBattery() const;
    [[nodiscard]] UPowerDevice* displayDevice() const;
    [[nodiscard]] int deviceCount() const;
    [[nodiscard]] QList<UPowerDevice*> devices() const;
    [[nodiscard]] Q_INVOKABLE PhosphorServiceUPower::UPowerDevice* deviceAt(int index) const;

Q_SIGNALS:
    void onBatteryChanged();
    void displayDeviceChanged();
    void deviceAdded(PhosphorServiceUPower::UPowerDevice* device);
    void deviceRemoved(PhosphorServiceUPower::UPowerDevice* device);
    void deviceCountChanged();

private Q_SLOTS:
    void _q_onPropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& invalidated);
    void _q_onDeviceAdded(const QDBusObjectPath& path);
    void _q_onDeviceRemoved(const QDBusObjectPath& path);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceUPower
