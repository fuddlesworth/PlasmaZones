// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNetwork/NetworkDevice.h>

#include <PhosphorDBus/Client.h>

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcNetworkDevice, "phosphor.service.network.device")

namespace {
constexpr auto kService = "org.freedesktop.NetworkManager";
constexpr auto kDeviceIface = "org.freedesktop.NetworkManager.Device";
// The Wi-Fi association lives on a SEPARATE interface from the generic
// device one, so a device that is Wifi is watched on both. A non-Wi-Fi
// device never subscribes to this and reports an empty active path.
constexpr auto kWirelessIface = "org.freedesktop.NetworkManager.Device.Wireless";
constexpr auto kPropsIface = "org.freedesktop.DBus.Properties";

// NetworkManager's DeviceType / State are sparse wire enums; map only the
// declared values and fall back to the Unknown enumerator for anything
// else, so a future-protocol or misbehaving-daemon value never aliases an
// enumerator we don't model.
PhosphorServiceNetwork::NetworkDevice::DeviceType deviceTypeFromRaw(uint raw)
{
    using DT = PhosphorServiceNetwork::NetworkDevice::DeviceType;
    switch (raw) {
    case DT::Ethernet:
    case DT::Wifi:
    case DT::Bluetooth:
    case DT::OlpcMesh:
    case DT::Wimax:
    case DT::Modem:
    case DT::Infiniband:
    case DT::Bond:
    case DT::Vlan:
    case DT::Adsl:
    case DT::Bridge:
    case DT::Generic:
    case DT::Team:
    case DT::Tun:
    case DT::IpTunnel:
    case DT::Macvlan:
    case DT::Vxlan:
    case DT::Veth:
    case DT::Macsec:
    case DT::Dummy:
    case DT::Ppp:
    case DT::Wireguard:
        return static_cast<DT>(raw);
    default:
        return DT::Unknown;
    }
}

PhosphorServiceNetwork::NetworkDevice::DeviceState deviceStateFromRaw(uint raw)
{
    using DS = PhosphorServiceNetwork::NetworkDevice::DeviceState;
    switch (raw) {
    case DS::Unmanaged:
    case DS::Unavailable:
    case DS::Disconnected:
    case DS::Prepare:
    case DS::Config:
    case DS::NeedAuth:
    case DS::IpConfig:
    case DS::IpCheck:
    case DS::Secondaries:
    case DS::Activated:
    case DS::Deactivating:
    case DS::Failed:
        return static_cast<DS>(raw);
    default:
        return DS::UnknownState;
    }
}
} // namespace

namespace PhosphorServiceNetwork {

class NetworkDevice::Private
{
public:
    NetworkDevice* owner = nullptr;
    QString path;
    QDBusConnection bus = QDBusConnection::systemBus();

    QString interfaceName;
    DeviceType deviceType = Unknown;
    DeviceState state = UnknownState;
    bool managed = false;
    QString activeAccessPointPath;
    // Whether the Wireless-interface subscription has been made. Guarded
    // because DeviceType arrives in an async GetAll reply and again in
    // every PropertiesChanged, so without this the same match rule would
    // be installed repeatedly for the life of the device.
    bool wirelessWatched = false;

    template<typename T, typename Signal>
    void setField(T& field, T val, Signal signal)
    {
        if (field == val)
            return;
        field = val;
        Q_EMIT(owner->*signal)();
    }

    // Async GetAll on the Properties interface: one round trip for the
    // whole device interface, never blocking. The watcher is parented to
    // `owner` so an in-flight reply cancels cleanly if the device is
    // removed mid-query.
    void requestAll()
    {
        if (!bus.isConnected())
            return;
        PhosphorDBus::Client client(bus, QLatin1String(kService), path, &lcNetworkDevice());
        auto* watcher = new QDBusPendingCallWatcher(
            client.asyncCall(QLatin1String(kPropsIface), QStringLiteral("GetAll"), {QLatin1String(kDeviceIface)}),
            owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QVariantMap> reply = *call;
            if (reply.isError()) {
                qCDebug(lcNetworkDevice) << "GetAll failed for" << path << ":" << reply.error().message();
                return;
            }
            applyProps(reply.value());
        });
    }

    // Subscribe to, and fetch, the Wireless interface. Called once, when
    // the device is first known to be Wi-Fi.
    void watchWireless()
    {
        if (wirelessWatched)
            return;
        wirelessWatched = true;
        // The PropertiesChanged subscription made in the constructor is
        // path-scoped and NOT interface-scoped, so it already delivers the
        // Wireless interface's changes; _q_onPropertiesChanged routes them
        // by the `iface` argument. Only the initial fetch is needed here.
        fetchWireless();
    }

    // The GetAll itself, separate from the one-shot subscription guard so
    // an invalidated property can be re-fetched. Folding the two together
    // made the re-fetch a no-op, since the guard had already latched.
    void fetchWireless()
    {
        if (!bus.isConnected())
            return;
        PhosphorDBus::Client client(bus, QLatin1String(kService), path, &lcNetworkDevice());
        auto* watcher = new QDBusPendingCallWatcher(
            client.asyncCall(QLatin1String(kPropsIface), QStringLiteral("GetAll"), {QLatin1String(kWirelessIface)}),
            owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QVariantMap> reply = *call;
            if (reply.isError()) {
                qCDebug(lcNetworkDevice) << "Wireless GetAll failed for" << path << ":" << reply.error().message();
                return;
            }
            applyWirelessProps(reply.value());
        });
    }

    // Applies a Wireless-interface property map, from a GetAll reply or a
    // partial PropertiesChanged. NM sends ActiveAccessPoint as an object
    // path, and "/" is its idiom for "none" — mapped to an empty string so
    // a consumer comparing against a real access point's path cannot
    // accidentally match the placeholder.
    void applyWirelessProps(const QVariantMap& props)
    {
        const QVariant v = props.value(QLatin1String("ActiveAccessPoint"));
        if (!v.isValid())
            return;
        QString apPath = qvariant_cast<QDBusObjectPath>(v).path();
        if (apPath == QLatin1String("/"))
            apPath.clear();
        setField(activeAccessPointPath, apPath, &NetworkDevice::activeAccessPointPathChanged);
    }

    // Applies a device-interface property map. Works for both a full
    // GetAll reply and a partial PropertiesChanged `changed` map; every
    // field is gated on isValid().
    void applyProps(const QVariantMap& props)
    {
        auto val = [&props](const char* name) -> QVariant {
            return props.value(QLatin1String(name));
        };
        QVariant v;

        if ((v = val("Interface")).isValid())
            setField(interfaceName, v.toString(), &NetworkDevice::interfaceNameChanged);
        if ((v = val("DeviceType")).isValid()) {
            setField(deviceType, deviceTypeFromRaw(v.toUInt()), &NetworkDevice::deviceTypeChanged);
            if (deviceType == Wifi)
                watchWireless();
        }
        if ((v = val("State")).isValid())
            setField(state, deviceStateFromRaw(v.toUInt()), &NetworkDevice::stateChanged);
        if ((v = val("Managed")).isValid())
            setField(managed, v.toBool(), &NetworkDevice::managedChanged);
    }
};

NetworkDevice::NetworkDevice(const QString& dbusPath, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;
    d->path = dbusPath;

    if (!d->bus.isConnected()) {
        // A device constructed against a disconnected bus stays a
        // permanent stub; log so the symptom (network widget never
        // populates) is debuggable from a single journal line.
        qCWarning(lcNetworkDevice) << "system bus unavailable; device inert:" << dbusPath;
        return;
    }
    const bool ok = d->bus.connect(QLatin1String(kService), dbusPath, QLatin1String(kPropsIface),
                                   QStringLiteral("PropertiesChanged"), this,
                                   SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));
    if (!ok)
        qCWarning(lcNetworkDevice) << "PropertiesChanged subscription failed for" << dbusPath;

    d->requestAll();
}

NetworkDevice::~NetworkDevice() = default;

QString NetworkDevice::dbusPath() const
{
    return d->path;
}
QString NetworkDevice::interfaceName() const
{
    return d->interfaceName;
}
NetworkDevice::DeviceType NetworkDevice::deviceType() const
{
    return d->deviceType;
}
NetworkDevice::DeviceState NetworkDevice::state() const
{
    return d->state;
}
bool NetworkDevice::managed() const
{
    return d->managed;
}
QString NetworkDevice::activeAccessPointPath() const
{
    return d->activeAccessPointPath;
}

void NetworkDevice::_q_onPropertiesChanged(const QString& iface, const QVariantMap& changed,
                                           const QStringList& invalidated)
{
    if (iface == QLatin1String(kWirelessIface)) {
        d->applyWirelessProps(changed);
        // An invalidated ActiveAccessPoint carries no value; re-fetch.
        if (invalidated.contains(QLatin1String("ActiveAccessPoint")))
            d->fetchWireless();
        return;
    }
    if (iface != QLatin1String(kDeviceIface))
        return;
    d->applyProps(changed);
    // Invalidated properties carry no value; re-fetch the whole interface
    // asynchronously to pick them up.
    if (!invalidated.isEmpty())
        d->requestAll();
}

} // namespace PhosphorServiceNetwork
