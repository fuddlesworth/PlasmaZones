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
#include <QDBusServiceWatcher>
#include <QSet>
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
    QDBusConnection bus = QDBusConnection::systemBus();
    QString service = QLatin1String(kService);
    bool available = false;
    bool pending = false;
    QString error;
    quint64 generation = 0;
    int remaining = 0;
    QSet<QString> removedDuringEnumeration;

    void setStatus(bool ready, bool loading, const QString& message = {})
    {
        if (available == ready && pending == loading && error == message)
            return;
        available = ready;
        pending = loading;
        error = message;
        Q_EMIT owner->availabilityChanged();
    }
    void clear()
    {
        setDisplayDevice(QString());
        while (!devices.isEmpty())
            removeDevice(devices.first()->dbusPath());
        setOnBattery(false);
    }
    void completed(const QString& message)
    {
        if (!message.isEmpty())
            error = message;
        if (--remaining == 0) {
            const auto failure = error;
            setStatus(failure.isEmpty(), false, failure);
        }
    }

    void setOnBattery(bool value)
    {
        if (onBattery == value)
            return;
        onBattery = value;
        Q_EMIT owner->onBatteryChanged();
    }

    /// Async Properties.Get for OnBattery. Used both at startup and
    /// when PropertiesChanged carries OnBattery in `invalidated` rather
    /// than `changed`. `receiver` parents the watcher so it cancels
    /// cleanly if the host is destroyed mid-flight.
    void requestOnBattery(QObject* receiver)
    {
        if (!bus.isConnected())
            return;
        QDBusMessage msg = QDBusMessage::createMethodCall(service, QLatin1String(kPath), QLatin1String(kPropsIface),
                                                          QStringLiteral("Get"));
        msg << QLatin1String(kIface) << QStringLiteral("OnBattery");
        const auto revision = generation;
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg, 5000), receiver);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, receiver,
                         [this, revision](QDBusPendingCallWatcher* call) {
                             call->deleteLater();
                             const QDBusPendingReply<QDBusVariant> reply = *call;
                             if (revision != generation || reply.isError())
                                 return;
                             setOnBattery(reply.value().variant().toBool());
                         });
    }

    // Aggregate devices are replaced after a daemon restart.
    void setDisplayDevice(const QString& path)
    {
        if (path == displayDevicePath)
            return;
        // The bare "/" sentinel (UPower's "no display device") and any
        // path outside `/org/freedesktop/UPower/devices/` reach us only
        // from a misbehaving daemon, but instantiating a UPowerDevice
        // against them would create a permanent-zero stub whose
        // `bus.connect` and GetAll silently fail. Normalize to the
        // "no display device" state instead.
        const bool hasReal = !path.isEmpty() && isValidDevicePath(path);
        if (!hasReal && !path.isEmpty())
            qCDebug(lcUPowerHost) << "Ignoring DisplayDevice with non-device path:" << path;
        if (!hasReal && !displayDevice && displayDevicePath.isEmpty()) {
            // No-op: an invalid non-empty path against an already-empty
            // displayDevice slot is a wire artifact. Skip the
            // displayDeviceChanged emit so QML doesn't see a
            // notification for a value that didn't move. (The check
            // above already logged the rejection.)
            return;
        }
        if (displayDevice) {
            displayDevice->deleteLater();
            displayDevice = nullptr;
        }
        displayDevicePath = hasReal ? path : QString();
        if (hasReal)
            displayDevice = new UPowerDevice(path, bus, service, owner);
        Q_EMIT owner->displayDeviceChanged();
    }

    // UPower has been observed to send DeviceAdded for the bare "/"
    // sentinel or a path that isn't under devices/ on old daemons and
    // suspend/resume races. Filter at the boundary so we don't spin up
    // a UPowerDevice subscribed to a nonsense object path. Require at
    // least one character after the `/devices/` prefix so the trailing-
    // slash-only string doesn't slip through; bus.connect would fail
    // downstream anyway, but rejecting at the boundary keeps the
    // duplicate-add guard and the qCDebug log honest about scope.
    bool isValidDevicePath(const QString& path) const
    {
        static const QString kPrefix = QStringLiteral("/org/freedesktop/UPower/devices/");
        return path.size() > kPrefix.size() && path.startsWith(kPrefix);
    }

    void addDevice(const QString& path)
    {
        if (!isValidDevicePath(path)) {
            qCDebug(lcUPowerHost) << "Ignoring add for non-device path:" << path;
            return;
        }
        for (auto* dev : std::as_const(devices)) {
            if (dev->dbusPath() == path) {
                qCDebug(lcUPowerHost) << "Device already known, ignoring duplicate add:" << path;
                return;
            }
        }
        auto* device = new UPowerDevice(path, bus, service, owner);
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
    : UPowerHost(QDBusConnection::systemBus(), QLatin1String(kService), parent)
{
}
UPowerHost::UPowerHost(const QDBusConnection& bus, const QString& service, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;
    d->bus = bus;
    d->service = service;
    d->bus.connect(service, QLatin1String(kPath), QLatin1String(kPropsIface), QStringLiteral("PropertiesChanged"), this,
                   SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));
    d->bus.connect(service, QLatin1String(kPath), QLatin1String(kIface), QStringLiteral("DeviceAdded"), this,
                   SLOT(_q_onDeviceAdded(QDBusObjectPath)));
    d->bus.connect(service, QLatin1String(kPath), QLatin1String(kIface), QStringLiteral("DeviceRemoved"), this,
                   SLOT(_q_onDeviceRemoved(QDBusObjectPath)));
    auto* watcher = new QDBusServiceWatcher(service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString&, const QString&, const QString& owner) {
                ++d->generation;
                d->clear();
                if (owner.isEmpty())
                    d->setStatus(false, false);
                else
                    refresh();
            });
    refresh();
}
void UPowerHost::refresh()
{
    const auto revision = ++d->generation;
    d->clear();
    d->removedDuringEnumeration.clear();
    d->remaining = 2;
    d->setStatus(false, true);
    d->requestOnBattery(this);
    auto displayCall = QDBusMessage::createMethodCall(d->service, QLatin1String(kPath), QLatin1String(kIface),
                                                      QStringLiteral("GetDisplayDevice"));
    auto* display = new QDBusPendingCallWatcher(d->bus.asyncCall(displayCall, 5000), this);
    connect(display, &QDBusPendingCallWatcher::finished, this, [this, revision](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        if (revision != d->generation)
            return;
        const QDBusPendingReply<QDBusObjectPath> reply = *call;
        if (!reply.isError())
            d->setDisplayDevice(reply.value().path());
        d->completed(reply.isError() ? reply.error().message() : QString());
    });
    auto enumerateCall = QDBusMessage::createMethodCall(d->service, QLatin1String(kPath), QLatin1String(kIface),
                                                        QStringLiteral("EnumerateDevices"));
    auto* enumerate = new QDBusPendingCallWatcher(d->bus.asyncCall(enumerateCall, 5000), this);
    connect(enumerate, &QDBusPendingCallWatcher::finished, this, [this, revision](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        if (revision != d->generation)
            return;
        const QDBusPendingReply<QList<QDBusObjectPath>> reply = *call;
        if (!reply.isError()) {
            for (const auto& path : reply.value()) {
                if (!d->removedDuringEnumeration.contains(path.path()))
                    d->addDevice(path.path());
            }
        }
        d->removedDuringEnumeration.clear();
        d->completed(reply.isError() ? reply.error().message() : QString());
    });
}
bool UPowerHost::available() const
{
    return d->available;
}
bool UPowerHost::pending() const
{
    return d->pending;
}
QString UPowerHost::error() const
{
    return d->error;
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
                                        const QStringList& invalidated)
{
    if (iface != QLatin1String(kIface))
        return;
    if (changed.contains(QStringLiteral("OnBattery")))
        d->setOnBattery(changed.value(QStringLiteral("OnBattery")).toBool());
    // UPower may invalidate (rather than change) OnBattery during a
    // daemon-side reload. Without an explicit re-fetch the cached flag
    // would silently stick at the prior value; share the same async-Get
    // helper that the constructor uses so the iface/path/props strings
    // (and the QDBusVariant unwrap that Properties.Get's "v" signature
    // requires) stay honest in one place.
    if (invalidated.contains(QStringLiteral("OnBattery")))
        d->requestOnBattery(this);
}

void UPowerHost::_q_onDeviceAdded(const QDBusObjectPath& path)
{
    d->removedDuringEnumeration.remove(path.path());
    d->addDevice(path.path());
}

void UPowerHost::_q_onDeviceRemoved(const QDBusObjectPath& path)
{
    if (d->pending)
        d->removedDuringEnumeration.insert(path.path());
    d->removeDevice(path.path());
}

} // namespace PhosphorServiceUPower
