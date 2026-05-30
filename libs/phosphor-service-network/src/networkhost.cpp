// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNetwork/NetworkHost.h>
#include <PhosphorServiceNetwork/NetworkDevice.h>

#include <PhosphorDBus/Client.h>

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcNetworkHost, "phosphor.service.network.host")

namespace {
constexpr auto kService = "org.freedesktop.NetworkManager";
constexpr auto kPath = "/org/freedesktop/NetworkManager";
constexpr auto kManagerIface = "org.freedesktop.NetworkManager";
constexpr auto kWirelessIface = "org.freedesktop.NetworkManager.Device.Wireless";
constexpr auto kPropsIface = "org.freedesktop.DBus.Properties";

// NetworkManager object paths are always under /org/freedesktop/NetworkManager/.
// Reject the bare root and anything outside that subtree at the boundary so
// a misbehaving daemon can't spin up a NetworkDevice on a nonsense path.
bool isValidDevicePath(const QString& path)
{
    static const QString kPrefix = QStringLiteral("/org/freedesktop/NetworkManager/Devices/");
    return path.size() > kPrefix.size() && path.startsWith(kPrefix);
}
} // namespace

namespace PhosphorServiceNetwork {

class NetworkHost::Private
{
public:
    NetworkHost* owner = nullptr;
    QDBusConnection bus = QDBusConnection::systemBus();
    QList<NetworkDevice*> devices;

    bool networkingEnabled = false;
    bool wirelessEnabled = false;
    Connectivity connectivity = UnknownConnectivity;
    QString primaryConnectionType;

    [[nodiscard]] PhosphorDBus::Client manager() const
    {
        return PhosphorDBus::Client(bus, QLatin1String(kService), QLatin1String(kPath), &lcNetworkHost());
    }

    void setNetworkingEnabled(bool value)
    {
        if (networkingEnabled == value)
            return;
        networkingEnabled = value;
        Q_EMIT owner->networkingEnabledChanged();
    }

    void setWirelessEnabled(bool value)
    {
        if (wirelessEnabled == value)
            return;
        wirelessEnabled = value;
        Q_EMIT owner->wirelessEnabledChanged();
    }

    void setConnectivity(Connectivity value)
    {
        if (connectivity == value)
            return;
        connectivity = value;
        Q_EMIT owner->connectivityChanged();
    }

    void setPrimaryConnectionType(const QString& value)
    {
        if (primaryConnectionType == value)
            return;
        primaryConnectionType = value;
        Q_EMIT owner->primaryConnectionTypeChanged();
    }

    // Apply a manager-interface property map (full GetAll reply or a
    // partial PropertiesChanged `changed` map). Connectivity is clamped:
    // NMConnectivityState is contiguous 0..4 today, anything above is a
    // future-protocol value we surface as Unknown rather than casting to
    // an undeclared enumerator.
    void applyManagerProps(const QVariantMap& props)
    {
        auto val = [&props](const char* name) -> QVariant {
            return props.value(QLatin1String(name));
        };
        QVariant v;
        if ((v = val("NetworkingEnabled")).isValid())
            setNetworkingEnabled(v.toBool());
        if ((v = val("WirelessEnabled")).isValid())
            setWirelessEnabled(v.toBool());
        if ((v = val("Connectivity")).isValid()) {
            const uint raw = v.toUInt();
            setConnectivity(raw <= static_cast<uint>(Full) ? static_cast<Connectivity>(raw) : UnknownConnectivity);
        }
        if ((v = val("PrimaryConnectionType")).isValid())
            setPrimaryConnectionType(v.toString());
    }

    void addDevice(const QString& path)
    {
        if (!isValidDevicePath(path)) {
            qCDebug(lcNetworkHost) << "Ignoring add for non-device path:" << path;
            return;
        }
        for (auto* dev : std::as_const(devices)) {
            if (dev->dbusPath() == path) {
                qCDebug(lcNetworkHost) << "Device already known, ignoring duplicate add:" << path;
                return;
            }
        }
        auto* device = new NetworkDevice(path, owner);
        devices.append(device);
        qCDebug(lcNetworkHost) << "Device added:" << path;
        Q_EMIT owner->deviceAdded(device);
        Q_EMIT owner->deviceCountChanged();
    }

    void removeDevice(const QString& path)
    {
        for (int i = 0; i < devices.size(); ++i) {
            if (devices.at(i)->dbusPath() == path) {
                auto* device = devices.at(i);
                qCDebug(lcNetworkHost) << "Device removed:" << path;
                // Detach from the list BEFORE the public signal so
                // observers that walk devices()/deviceCount() from inside
                // the slot see the post-remove state.
                devices.removeAt(i);
                Q_EMIT owner->deviceRemoved(device);
                Q_EMIT owner->deviceCountChanged();
                device->deleteLater();
                return;
            }
        }
    }
};

NetworkHost::NetworkHost(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;

    if (!d->bus.isConnected()) {
        // Shells binding `host.connectivity` get a permanent Unknown with
        // zero diagnostic when the system bus is unreachable. A warning
        // surfaces at the default threshold so the "network widget never
        // updates" symptom has a single breadcrumb.
        qCWarning(lcNetworkHost) << "system bus unavailable: NetworkManager not accessible";
        return;
    }

    const bool propsOk = d->bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kPropsIface),
                                        QStringLiteral("PropertiesChanged"), this,
                                        SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));
    const bool addedOk = d->bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kManagerIface),
                                        QStringLiteral("DeviceAdded"), this, SLOT(_q_onDeviceAdded(QDBusObjectPath)));
    const bool removedOk =
        d->bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kManagerIface),
                       QStringLiteral("DeviceRemoved"), this, SLOT(_q_onDeviceRemoved(QDBusObjectPath)));
    if (!propsOk || !addedOk || !removedOk) {
        qCWarning(lcNetworkHost) << "subscription failed: props=" << propsOk << " added=" << addedOk
                                 << " removed=" << removedOk;
    }

    // Bootstrap queries run asynchronously: a blocking call here would
    // freeze the GUI thread while NetworkManager (and, through the
    // per-device GetAll, every device) responds. Watchers are parented to
    // `this` so they cancel cleanly if the host is destroyed early.

    // Manager scalar properties.
    {
        auto* watcher =
            new QDBusPendingCallWatcher(d->manager().asyncCall(QLatin1String(kPropsIface), QStringLiteral("GetAll"),
                                                               {QLatin1String(kManagerIface)}),
                                        this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QVariantMap> reply = *call;
            if (reply.isError()) {
                qCWarning(lcNetworkHost) << "manager GetAll failed:" << reply.error().message();
                return;
            }
            d->applyManagerProps(reply.value());
        });
    }

    // Device list. GetDevices returns a clean QList<QDBusObjectPath>,
    // avoiding the QDBusArgument demarshal the `Devices` property would
    // need out of the GetAll QVariantMap.
    {
        auto* watcher = new QDBusPendingCallWatcher(
            d->manager().asyncCall(QLatin1String(kManagerIface), QStringLiteral("GetDevices")), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QList<QDBusObjectPath>> reply = *call;
            if (reply.isError()) {
                qCWarning(lcNetworkHost) << "GetDevices failed:" << reply.error().message();
                return;
            }
            const auto paths = reply.value();
            for (const QDBusObjectPath& p : paths)
                d->addDevice(p.path());
        });
    }
}

NetworkHost::~NetworkHost() = default;

bool NetworkHost::networkingEnabled() const
{
    return d->networkingEnabled;
}
bool NetworkHost::wirelessEnabled() const
{
    return d->wirelessEnabled;
}

void NetworkHost::setWirelessEnabled(bool enabled)
{
    if (!d->bus.isConnected())
        return;
    // Properties.Set takes the value as a D-Bus variant ('v'). The cached
    // flag is NOT updated optimistically: NetworkManager echoes the change
    // back via PropertiesChanged, which flips wirelessEnabled (and emits
    // the NOTIFY) once the radio actually toggled.
    d->manager().fireAndForget(
        this, QLatin1String(kPropsIface), QStringLiteral("Set"),
        {QLatin1String(kManagerIface), QStringLiteral("WirelessEnabled"), QVariant::fromValue(QDBusVariant(enabled))},
        QStringLiteral("setWirelessEnabled"));
}

NetworkHost::Connectivity NetworkHost::connectivity() const
{
    return d->connectivity;
}
QString NetworkHost::primaryConnectionType() const
{
    return d->primaryConnectionType;
}
int NetworkHost::deviceCount() const
{
    return d->devices.size();
}
QList<NetworkDevice*> NetworkHost::devices() const
{
    return d->devices;
}

NetworkDevice* NetworkHost::deviceAt(int index) const
{
    if (index < 0 || index >= d->devices.size())
        return nullptr;
    return d->devices.at(index);
}

void NetworkHost::scanWifi()
{
    if (!d->bus.isConnected())
        return;
    for (auto* dev : std::as_const(d->devices)) {
        if (dev->deviceType() != NetworkDevice::Wifi)
            continue;
        // RequestScan(a{sv}) — pass an empty options dict. Fire-and-forget;
        // results land daemon-side on the device's access-point list.
        PhosphorDBus::Client(d->bus, QLatin1String(kService), dev->dbusPath(), &lcNetworkHost())
            .fireAndForget(this, QLatin1String(kWirelessIface), QStringLiteral("RequestScan"),
                           {QVariant::fromValue(QVariantMap{})}, QStringLiteral("RequestScan"));
    }
}

void NetworkHost::_q_onPropertiesChanged(const QString& iface, const QVariantMap& changed,
                                         const QStringList& invalidated)
{
    if (iface != QLatin1String(kManagerIface))
        return;
    d->applyManagerProps(changed);
    // The manager's scalar properties are always carried in `changed`;
    // NetworkManager does not invalidate them, so no re-fetch is needed.
    Q_UNUSED(invalidated);
}

void NetworkHost::_q_onDeviceAdded(const QDBusObjectPath& path)
{
    d->addDevice(path.path());
}

void NetworkHost::_q_onDeviceRemoved(const QDBusObjectPath& path)
{
    d->removeDevice(path.path());
}

} // namespace PhosphorServiceNetwork
