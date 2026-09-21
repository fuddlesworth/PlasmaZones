// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNetwork/NetworkHost.h>
#include <PhosphorServiceNetwork/AccessPoint.h>
#include <PhosphorServiceNetwork/NetworkConnection.h>
#include <PhosphorServiceNetwork/NetworkDevice.h>

#include <PhosphorDBus/Client.h>

#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDBusServiceWatcher>
#include <QLoggingCategory>
#include <QPointer>

#include <mutex>

// NetworkManager's connection-settings type a{sa{sv}} — a map of
// setting-group name to a property dict. Typedef'd so the comma inside the
// template args doesn't trip the Q_DECLARE_METATYPE macro, then declared +
// registered so it rides through QVariant / QtDBus marshalling for
// AddAndActivateConnection.
using NMConnectionSettings = QMap<QString, QVariantMap>;
Q_DECLARE_METATYPE(NMConnectionSettings)

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

// WPA-PSK accepts either an 8-63 character ASCII passphrase or a 64-character
// hexadecimal pre-shared key. A 64-character non-hex value is NOT a valid PSK,
// so it must be rejected too rather than marshalled into a profile NM drops.
bool isValidWpaPassphrase(const QString& passphrase)
{
    const auto length = passphrase.size();
    if (length >= 8 && length <= 63) {
        for (const QChar ch : passphrase) {
            if (ch.unicode() < 32 || ch.unicode() > 126)
                return false;
        }
        return true;
    }
    if (length != 64)
        return false;
    for (const QChar ch : passphrase) {
        const bool isHexDigit = (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
            || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) || (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'));
        if (!isHexDigit)
            return false;
    }
    return true;
}

// Register the a{sa{sv}} connection-settings marshaller exactly once.
void ensureConnectionSettingsRegistered()
{
    static std::once_flag once;
    std::call_once(once, [] {
        qDBusRegisterMetaType<QMap<QString, QVariantMap>>();
    });
}
} // namespace

namespace PhosphorServiceNetwork {

class NetworkHost::Private
{
public:
    NetworkHost* owner = nullptr;
    QDBusConnection bus = QDBusConnection::systemBus();
    QList<NetworkDevice*> devices;

    bool available = false;
    bool wirelessHardwareEnabled = false;
    QString connectivityCheckUri;
    quint64 generation = 0;
    QHash<QString, quint64> activationGenerations;
    bool networkingEnabled = false;
    bool wirelessEnabled = false;
    Connectivity connectivity = UnknownConnectivity;
    QString primaryConnectionType;

    [[nodiscard]] PhosphorDBus::Client manager() const
    {
        return PhosphorDBus::Client(bus, QLatin1String(kService), QLatin1String(kPath), &lcNetworkHost());
    }

    void setAvailable(bool value)
    {
        if (available == value)
            return;
        available = value;
        Q_EMIT owner->availableChanged();
    }

    void operation(const QString& path, const QString& iface, const QString& method, const QVariantList& arguments,
                   const QString& operationName, const QString& activationDevice = {})
    {
        if (!bus.isConnected()) {
            Q_EMIT owner->operationFinished(operationName, path,
                                            QStringLiteral("org.freedesktop.DBus.Error.Disconnected"));
            return;
        }
        const auto activation = activationGenerations.value(activationDevice);
        auto* watcher =
            new QDBusPendingCallWatcher(PhosphorDBus::Client(bus, QLatin1String(kService), path, &lcNetworkHost())
                                            .asyncCall(iface, method, arguments),
                                        owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner,
                         [this, path, operationName, activationDevice, activation](QDBusPendingCallWatcher* call) {
                             call->deleteLater();
                             const QDBusPendingReply<> reply = *call;
                             if (!activationDevice.isEmpty() && !reply.isError()
                                 && activation != activationGenerations.value(activationDevice)) {
                                 const auto arguments = reply.reply().arguments();
                                 if (!arguments.isEmpty()) {
                                     const auto activePath = qvariant_cast<QDBusObjectPath>(arguments.constLast());
                                     if (!activePath.path().isEmpty() && activePath.path() != QLatin1String("/"))
                                         operation(QLatin1String(kPath), QLatin1String(kManagerIface),
                                                   QStringLiteral("DeactivateConnection"),
                                                   {QVariant::fromValue(activePath)}, QStringLiteral("disconnect"));
                                 }
                             }
                             Q_EMIT owner->operationFinished(operationName, path,
                                                             reply.isError() ? reply.error().name() : QString{});
                         });
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
        if ((v = val("WirelessHardwareEnabled")).isValid() && wirelessHardwareEnabled != v.toBool()) {
            wirelessHardwareEnabled = v.toBool();
            Q_EMIT owner->wirelessHardwareEnabledChanged();
        }
        if ((v = val("ConnectivityCheckUri")).isValid() && connectivityCheckUri != v.toString()) {
            connectivityCheckUri = v.toString();
            Q_EMIT owner->connectivityCheckUriChanged();
        }
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

    auto* serviceWatcher =
        new QDBusServiceWatcher(QLatin1String(kService), d->bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString&, const QString&, const QString& owner) {
                ++d->generation;
                d->setAvailable(false);
                d->setNetworkingEnabled(false);
                d->setWirelessEnabled(false);
                d->setConnectivity(UnknownConnectivity);
                d->setPrimaryConnectionType({});
                while (!d->devices.isEmpty())
                    d->removeDevice(d->devices.constFirst()->dbusPath());
                if (!owner.isEmpty())
                    refresh();
            });
    refresh();
}

void NetworkHost::refresh()
{
    const auto generation = ++d->generation;
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
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            if (generation != d->generation)
                return;
            const QDBusPendingReply<QVariantMap> reply = *call;
            if (reply.isError()) {
                d->setAvailable(false);
                qCWarning(lcNetworkHost) << "manager GetAll failed:" << reply.error().message();
                return;
            }
            d->applyManagerProps(reply.value());
            d->setAvailable(true);
        });
    }

    // Device list. GetDevices returns a clean QList<QDBusObjectPath>,
    // avoiding the QDBusArgument demarshal the `Devices` property would
    // need out of the GetAll QVariantMap.
    {
        auto* watcher = new QDBusPendingCallWatcher(
            d->manager().asyncCall(QLatin1String(kManagerIface), QStringLiteral("GetDevices")), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            if (generation != d->generation)
                return;
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
bool NetworkHost::available() const
{
    return d->available;
}
bool NetworkHost::wirelessHardwareEnabled() const
{
    return d->wirelessHardwareEnabled;
}
QString NetworkHost::connectivityCheckUri() const
{
    return d->connectivityCheckUri;
}

void NetworkHost::disconnectDevice(NetworkDevice* device)
{
    if (device)
        ++d->activationGenerations[device->dbusPath()];
    if (device)
        d->operation(device->dbusPath(), QStringLiteral("org.freedesktop.NetworkManager.Device"),
                     QStringLiteral("Disconnect"), {}, QStringLiteral("disconnect"));
}

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
    d->operation(
        QLatin1String(kPath), QLatin1String(kPropsIface), QStringLiteral("Set"),
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
        d->operation(dev->dbusPath(), QLatin1String(kWirelessIface), QStringLiteral("RequestScan"),
                     {QVariant::fromValue(QVariantMap{})}, QStringLiteral("RequestScan"));
    }
}

void NetworkHost::activateConnection(NetworkConnection* connection, NetworkDevice* device)
{
    if (!connection || !device || !d->bus.isConnected())
        return;
    // ActivateConnection(connection o, device o, specific_object o). "/"
    // is the "no specific object" sentinel (NM picks the best AP itself
    // for a Wi-Fi connection).
    d->operation(QLatin1String(kPath), QLatin1String(kManagerIface), QStringLiteral("ActivateConnection"),
                 {QVariant::fromValue(QDBusObjectPath(connection->dbusPath())),
                  QVariant::fromValue(QDBusObjectPath(device->dbusPath())),
                  QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")))},
                 QStringLiteral("activateConnection"), device->dbusPath());
}

void NetworkHost::connectToAccessPoint(NetworkDevice* device, AccessPoint* accessPoint, const QString& passphrase,
                                       bool autoConnect, NetworkConnection* existing)
{
    if (!device || !accessPoint || !d->bus.isConnected())
        return;
    // A hidden-network AccessPoint legitimately reports an empty SSID, and a
    // stale AP can carry an empty path; either would marshal a malformed
    // 802-11-wireless profile that NetworkManager is bound to reject.
    // Connecting to a hidden SSID needs an explicit name the AP can't supply
    // here, so refuse at the boundary rather than fire a doomed call.
    if (accessPoint->ssid().isEmpty() || accessPoint->dbusPath().isEmpty()) {
        Q_EMIT operationFinished(QStringLiteral("connectToAccessPoint"), device->dbusPath(),
                                 QStringLiteral("UnsupportedNetwork"));
        return;
    }
    // An empty passphrase is the open-network case (no security block is
    // attached below). A non-empty one is treated as WPA-PSK, which accepts
    // an 8-63 character ASCII passphrase or a 64-character hex pre-shared
    // key; anything else marshals a profile NetworkManager rejects
    // asynchronously with no result surface here, so reject it at the
    // boundary rather than fire a doomed call.
    if (!passphrase.isEmpty() && !isValidWpaPassphrase(passphrase)) {
        Q_EMIT operationFinished(QStringLiteral("connectToAccessPoint"), device->dbusPath(),
                                 QStringLiteral("InvalidPassphrase"));
        return;
    }
    if (accessPoint->secured()
        && (accessPoint->security() == QLatin1String("802.1X") || accessPoint->security() == QLatin1String("WEP")
            || passphrase.isEmpty())) {
        Q_EMIT operationFinished(QStringLiteral("connectToAccessPoint"), device->dbusPath(),
                                 QStringLiteral("UnsupportedNetwork"));
        return;
    }
    ensureConnectionSettingsRegistered();

    if (existing) {
        if (existing->ssid() != accessPoint->ssid() || existing->connectionType() != QLatin1String("802-11-wireless")) {
            Q_EMIT operationFinished(QStringLiteral("connectToAccessPoint"), device->dbusPath(),
                                     QStringLiteral("UnsupportedNetwork"));
            return;
        }
        const QString profilePath = existing->dbusPath();
        const QString devicePath = device->dbusPath();
        const auto activation = d->activationGenerations.value(devicePath);
        const QPointer<NetworkDevice> guardedDevice(device);
        const QPointer<NetworkConnection> guardedProfile(existing);
        auto* get = new QDBusPendingCallWatcher(
            PhosphorDBus::Client(d->bus, QLatin1String(kService), profilePath, &lcNetworkHost())
                .asyncCall(QStringLiteral("org.freedesktop.NetworkManager.Settings.Connection"),
                           QStringLiteral("GetSettings")),
            this);
        connect(get, &QDBusPendingCallWatcher::finished, this,
                [this, profilePath, devicePath, activation, guardedDevice, guardedProfile, passphrase,
                 autoConnect](QDBusPendingCallWatcher* call) {
                    call->deleteLater();
                    const QDBusPendingReply<NMConnectionSettings> reply = *call;
                    if (!guardedDevice || !guardedProfile || activation != d->activationGenerations.value(devicePath)
                        || reply.isError()) {
                        Q_EMIT operationFinished(QStringLiteral("connectToAccessPoint"), profilePath,
                                                 reply.isError() ? reply.error().name()
                                                                 : QStringLiteral("DeviceRemoved"));
                        return;
                    }
                    auto settings = reply.value();
                    settings[QStringLiteral("connection")][QStringLiteral("autoconnect")] = autoConnect;
                    settings[QStringLiteral("802-11-wireless-security")][QStringLiteral("psk")] = passphrase;
                    auto* update = new QDBusPendingCallWatcher(
                        PhosphorDBus::Client(d->bus, QLatin1String(kService), profilePath, &lcNetworkHost())
                            .asyncCall(QStringLiteral("org.freedesktop.NetworkManager.Settings.Connection"),
                                       QStringLiteral("Update"), {QVariant::fromValue(settings)}),
                        this);
                    connect(update, &QDBusPendingCallWatcher::finished, this,
                            [this, profilePath, devicePath, activation, guardedDevice,
                             guardedProfile](QDBusPendingCallWatcher* updated) {
                                updated->deleteLater();
                                const QDBusPendingReply<> result = *updated;
                                if (!guardedDevice || !guardedProfile
                                    || activation != d->activationGenerations.value(devicePath) || result.isError()) {
                                    Q_EMIT operationFinished(QStringLiteral("connectToAccessPoint"), profilePath,
                                                             result.isError() ? result.error().name()
                                                                              : QStringLiteral("DeviceRemoved"));
                                    return;
                                }
                                activateConnection(guardedProfile, guardedDevice);
                            });
                });
        return;
    }

    // Minimal Wi-Fi profile. NM fills in uuid + the rest of the defaults;
    // we name the profile after the SSID and, when a passphrase is given,
    // attach a WPA-PSK security block. Open networks omit it entirely.
    QMap<QString, QVariantMap> settings;
    settings.insert(QStringLiteral("connection"),
                    QVariantMap{{QStringLiteral("id"), accessPoint->ssid()},
                                {QStringLiteral("type"), QStringLiteral("802-11-wireless")},
                                {QStringLiteral("autoconnect"), autoConnect}});
    settings.insert(QStringLiteral("802-11-wireless"),
                    QVariantMap{{QStringLiteral("ssid"), accessPoint->ssid().toUtf8()},
                                {QStringLiteral("mode"), QStringLiteral("infrastructure")}});
    if (!passphrase.isEmpty()) {
        settings.insert(QStringLiteral("802-11-wireless-security"),
                        QVariantMap{{QStringLiteral("key-mgmt"),
                                     accessPoint->security() == QLatin1String("WPA3") ? QStringLiteral("sae")
                                                                                      : QStringLiteral("wpa-psk")},
                                    {QStringLiteral("psk"), passphrase}});
    }

    // AddAndActivateConnection(connection a{sa{sv}}, device o, specific_object o).
    // The AP path is the specific object so NM activates against this exact BSSID's network.
    d->operation(QLatin1String(kPath), QLatin1String(kManagerIface), QStringLiteral("AddAndActivateConnection"),
                 {QVariant::fromValue(settings), QVariant::fromValue(QDBusObjectPath(device->dbusPath())),
                  QVariant::fromValue(QDBusObjectPath(accessPoint->dbusPath()))},
                 QStringLiteral("connectToAccessPoint"), device->dbusPath());
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
