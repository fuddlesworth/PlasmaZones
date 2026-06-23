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

    /// Async Properties.Get for OnBattery. Used both at startup and
    /// when PropertiesChanged carries OnBattery in `invalidated` rather
    /// than `changed`. `receiver` parents the watcher so it cancels
    /// cleanly if the host is destroyed mid-flight.
    void requestOnBattery(QObject* receiver)
    {
        auto bus = QDBusConnection::systemBus();
        if (!bus.isConnected())
            return;
        QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), QLatin1String(kPath),
                                                          QLatin1String(kPropsIface), QStringLiteral("Get"));
        msg << QLatin1String(kIface) << QStringLiteral("OnBattery");
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), receiver);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, receiver, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QDBusVariant> reply = *call;
            if (reply.isError())
                return;
            setOnBattery(reply.value().variant().toBool());
        });
    }

    // Replace the cached display device if the path actually changed.
    // UPower normally exposes a stable aggregate path
    // (/org/freedesktop/UPower/devices/DisplayDevice), so the path
    // tracking is mostly bootstrap logic: empty path on the way in,
    // populated path on the GetDisplayDevice reply. If a UPower daemon
    // respawn produces a different aggregate path on a re-issued query,
    // we swap the cached QObject rather than leaking it. Today no code
    // path re-issues GetDisplayDevice after startup; the tracking lets
    // a future daemon-respawn watcher plug in without rewriting this.
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
            displayDevice = new UPowerDevice(path, owner);
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
        // Escalated from qCInfo: shells binding `host.onBattery` get a
        // permanent `false` with zero diagnostic when the system bus
        // is unreachable. A warning surfaces in journals at the
        // default threshold so the user-visible "battery widget never
        // updates" symptom has a single line of breadcrumb.
        qCWarning(lcUPowerHost) << "system bus unavailable: UPower not accessible";
        return;
    }

    const bool propsOk = bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kPropsIface),
                                     QStringLiteral("PropertiesChanged"), this,
                                     SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));
    const bool addedOk = bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
                                     QStringLiteral("DeviceAdded"), this, SLOT(_q_onDeviceAdded(QDBusObjectPath)));
    const bool removedOk =
        bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
                    QStringLiteral("DeviceRemoved"), this, SLOT(_q_onDeviceRemoved(QDBusObjectPath)));
    if (!propsOk || !addedOk || !removedOk) {
        qCWarning(lcUPowerHost) << "subscription failed: props=" << propsOk << " added=" << addedOk
                                << " removed=" << removedOk;
    }

    // All three startup queries run asynchronously: a blocking call
    // here would freeze the GUI thread while UPower (and, through the
    // per-device GetAll, every battery) responds. Watchers are parented
    // to `this` so they cancel cleanly if the host is destroyed early.

    d->requestOnBattery(this);

    // Get display device. The setDisplayDevice path-tracking exists
    // so a future daemon-respawn watcher can re-issue this query and
    // swap the cached aggregate rather than leak the prior QObject.
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
    d->addDevice(path.path());
}

void UPowerHost::_q_onDeviceRemoved(const QDBusObjectPath& path)
{
    d->removeDevice(path.path());
}

} // namespace PhosphorServiceUPower
