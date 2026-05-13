// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/UPowerHost.h>
#include <PhosphorServices/UPowerDevice.h>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcUPowerHost, "phosphorservices.upower.host")

namespace {
constexpr auto kService = "org.freedesktop.UPower";
constexpr auto kPath = "/org/freedesktop/UPower";
constexpr auto kIface = "org.freedesktop.UPower";
constexpr auto kPropsIface = "org.freedesktop.DBus.Properties";
} // namespace

namespace PhosphorServices {

class UPowerHost::Private
{
public:
    UPowerHost* owner = nullptr;
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

    bus.connect(QLatin1String(kService), QLatin1String(kPath), QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"), this,
                SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));

    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface), QStringLiteral("DeviceAdded"),
                this, SLOT(_q_onDeviceAdded(QDBusObjectPath)));
    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface), QStringLiteral("DeviceRemoved"),
                this, SLOT(_q_onDeviceRemoved(QDBusObjectPath)));

    // Read OnBattery
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), QLatin1String(kPath),
                                                          QLatin1String(kPropsIface), QStringLiteral("Get"));
        msg << QLatin1String(kIface) << QStringLiteral("OnBattery");
        QDBusMessage reply = bus.call(msg, QDBus::Block, 500);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
            d->onBattery = reply.arguments().first().value<QDBusVariant>().variant().toBool();
    }

    // Get display device
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), QLatin1String(kPath),
                                                          QLatin1String(kIface), QStringLiteral("GetDisplayDevice"));
        QDBusMessage reply = bus.call(msg, QDBus::Block, 500);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            QString devPath = reply.arguments().first().value<QDBusObjectPath>().path();
            if (!devPath.isEmpty()) {
                d->displayDevice = new UPowerDevice(devPath, this);
                Q_EMIT displayDeviceChanged();
            }
        }
    }

    // Enumerate devices
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), QLatin1String(kPath),
                                                          QLatin1String(kIface), QStringLiteral("EnumerateDevices"));
        QDBusMessage reply = bus.call(msg, QDBus::Block, 500);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const auto paths = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().first());
            for (const QDBusObjectPath& p : paths)
                d->addDevice(p.path());
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
    if (iface != QLatin1String(kIface))
        return;
    if (changed.contains(QStringLiteral("OnBattery"))) {
        bool val = changed.value(QStringLiteral("OnBattery")).toBool();
        if (d->onBattery != val) {
            d->onBattery = val;
            Q_EMIT onBatteryChanged();
        }
    }
}

void UPowerHost::_q_onDeviceAdded(const QDBusObjectPath& path)
{
    d->addDevice(path.path());
}

void UPowerHost::_q_onDeviceRemoved(const QDBusObjectPath& path)
{
    d->removeDevice(path.path());
}

} // namespace PhosphorServices
