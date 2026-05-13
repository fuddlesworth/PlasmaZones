// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/UPowerHost.h>
#include <PhosphorServices/UPowerDevice.h>

#include "upower_interface.h"

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcUPowerHost, "phosphorservices.upower.host")

static constexpr auto kUPowerService = "org.freedesktop.UPower";
static constexpr auto kUPowerPath = "/org/freedesktop/UPower";

namespace PhosphorServices {

class UPowerHost::Private
{
public:
    UPowerHost* owner = nullptr;
    std::unique_ptr<OrgFreedesktopUPowerInterface> proxy;
    QList<UPowerDevice*> devices;
    UPowerDevice* displayDevice = nullptr;
    bool onBattery = false;

    void addDevice(const QString& path)
    {
        for (auto* dev : std::as_const(devices)) {
            if (dev->dbusPath() == path)
                return;
        }
        auto* device = new UPowerDevice(path, owner);
        devices.append(device);
        qCDebug(lcUPowerHost) << "Device added:" << path;
        Q_EMIT owner->deviceAdded(device);
        Q_EMIT owner->deviceCountChanged();
    }

    void removeDevice(const QString& path)
    {
        for (int i = 0; i < devices.size(); ++i) {
            if (devices.at(i)->dbusPath() == path) {
                auto* device = devices.takeAt(i);
                qCDebug(lcUPowerHost) << "Device removed:" << path;
                Q_EMIT owner->deviceRemoved(device);
                Q_EMIT owner->deviceCountChanged();
                device->deleteLater();
                return;
            }
        }
    }
};

UPowerHost::UPowerHost(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;

    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        qCInfo(lcUPowerHost) << "System bus unavailable — UPower not accessible";
        return;
    }

    d->proxy = std::make_unique<OrgFreedesktopUPowerInterface>(QLatin1String(kUPowerService),
                                                               QLatin1String(kUPowerPath), bus, this);

    bus.connect(QLatin1String(kUPowerService), QLatin1String(kUPowerPath),
                QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));

    connect(d->proxy.get(), &OrgFreedesktopUPowerInterface::DeviceAdded, this, [this](const QDBusObjectPath& path) {
        d->addDevice(path.path());
    });
    connect(d->proxy.get(), &OrgFreedesktopUPowerInterface::DeviceRemoved, this, [this](const QDBusObjectPath& path) {
        d->removeDevice(path.path());
    });

    d->onBattery = d->proxy->onBattery();

    auto displayReply = d->proxy->GetDisplayDevice();
    displayReply.waitForFinished();
    if (displayReply.isValid() && !displayReply.value().path().isEmpty()) {
        d->displayDevice = new UPowerDevice(displayReply.value().path(), this);
        Q_EMIT displayDeviceChanged();
    }

    auto enumReply = d->proxy->EnumerateDevices();
    enumReply.waitForFinished();
    if (enumReply.isValid()) {
        const auto paths = enumReply.value();
        for (const QDBusObjectPath& path : paths) {
            d->addDevice(path.path());
        }
    }
}

UPowerHost::~UPowerHost() = default;

bool UPowerHost::onBattery() const
{
    return d->onBattery;
}
UPowerDevice* UPowerHost::displayDevice() const
{
    return d->displayDevice;
}
int UPowerHost::deviceCount() const
{
    return d->devices.size();
}
QList<UPowerDevice*> UPowerHost::devices() const
{
    return d->devices;
}

UPowerDevice* UPowerHost::deviceAt(int index) const
{
    if (index < 0 || index >= d->devices.size())
        return nullptr;
    return d->devices.at(index);
}

void UPowerHost::_q_onPropertiesChanged(const QString& iface, const QVariantMap& changed,
                                        const QStringList& /*invalidated*/)
{
    if (iface != QLatin1String("org.freedesktop.UPower"))
        return;
    if (changed.contains(QStringLiteral("OnBattery"))) {
        bool val = changed.value(QStringLiteral("OnBattery")).toBool();
        if (d->onBattery != val) {
            d->onBattery = val;
            Q_EMIT onBatteryChanged();
        }
    }
}

} // namespace PhosphorServices
