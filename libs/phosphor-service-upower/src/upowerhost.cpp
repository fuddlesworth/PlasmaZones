// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceUPower/UPowerHost.h>
#include <PhosphorServiceUPower/UPowerDevice.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcUPowerHost, "phosphor.service.upower.host")

namespace {
constexpr auto kService = "org.freedesktop.UPower";
constexpr auto kPath = "/org/freedesktop/UPower";
constexpr auto kIface = "org.freedesktop.UPower";
constexpr auto kPropsIface = "org.freedesktop.DBus.Properties";
} // namespace

namespace PhosphorServiceUPower {

class UPowerHost::Private
{
public:
    UPowerHost* owner = nullptr;
    QList<UPowerDevice*> devices;
    UPowerDevice* displayDevice = nullptr;
    QString displayDevicePath; ///< tracks the path so setDisplayDevice can detect a swap
    bool onBattery = false;

    void setOnBattery(bool value)
    {
        if (onBattery == value)
            return;
        onBattery = value;
        Q_EMIT owner->onBatteryChanged();
    }

    // Replace the cached display device if the path actually changed.
    // UPower normally exposes a stable aggregate path
    // (/org/freedesktop/UPower/devices/DisplayDevice), but a daemon
    // restart or a hot-swap of all batteries can produce a different
    // path on the next GetDisplayDevice reply. The previous one-shot
    // behaviour silently retained the old, now-dangling QObject.
    void setDisplayDevice(const QString& path)
    {
        if (path == displayDevicePath)
            return;
        if (displayDevice) {
            displayDevice->deleteLater();
            displayDevice = nullptr;
        }
        displayDevicePath = path;
        if (!path.isEmpty())
            displayDevice = new UPowerDevice(path, owner);
        Q_EMIT owner->displayDeviceChanged();
    }

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
                auto* device = devices.at(i);
                qCDebug(lcUPowerHost) << "Device removed:" << path;
                // Detach from the list BEFORE the public signal so
                // observers that walk devices()/deviceCount() from
                // inside the slot see the post-remove state.
                devices.removeAt(i);
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

    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kPropsIface),
                QStringLiteral("PropertiesChanged"), this,
                SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));

    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface), QStringLiteral("DeviceAdded"),
                this, SLOT(_q_onDeviceAdded(QDBusObjectPath)));
    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface), QStringLiteral("DeviceRemoved"),
                this, SLOT(_q_onDeviceRemoved(QDBusObjectPath)));

    // All three startup queries run asynchronously — a blocking call
    // here would freeze the GUI thread while UPower (and, through the
    // per-device GetAll, every battery) responds. Watchers are parented
    // to `this` so they cancel cleanly if the host is destroyed early.

    // Read OnBattery
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), QLatin1String(kPath),
                                                          QLatin1String(kPropsIface), QStringLiteral("Get"));
        msg << QLatin1String(kIface) << QStringLiteral("OnBattery");
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QDBusVariant> reply = *call;
            if (reply.isError())
                return;
            d->setOnBattery(reply.value().variant().toBool());
        });
    }

    // Get display device. setDisplayDevice's path-tracking makes this
    // safe to call repeatedly: the first reply mounts the aggregate,
    // a subsequent reply (e.g., after a upower respawn) with a
    // different path replaces the QObject instead of leaving the old
    // pointer dangling.
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), QLatin1String(kPath),
                                                          QLatin1String(kIface), QStringLiteral("GetDisplayDevice"));
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QDBusObjectPath> reply = *call;
            if (reply.isError())
                return;
            d->setDisplayDevice(reply.value().path());
        });
    }

    // Enumerate devices
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), QLatin1String(kPath),
                                                          QLatin1String(kIface), QStringLiteral("EnumerateDevices"));
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QList<QDBusObjectPath>> reply = *call;
            if (reply.isError()) {
                qCWarning(lcUPowerHost) << "EnumerateDevices failed:" << reply.error().message();
                return;
            }
            const auto paths = reply.value();
            for (const QDBusObjectPath& p : paths)
                d->addDevice(p.path());
        });
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
    if (iface != QLatin1String(kIface))
        return;
    if (changed.contains(QStringLiteral("OnBattery")))
        d->setOnBattery(changed.value(QStringLiteral("OnBattery")).toBool());
}

void UPowerHost::_q_onDeviceAdded(const QDBusObjectPath& path)
{
    d->addDevice(path.path());
}

void UPowerHost::_q_onDeviceRemoved(const QDBusObjectPath& path)
{
    d->removeDevice(path.path());
}

} // namespace PhosphorServiceUPower
