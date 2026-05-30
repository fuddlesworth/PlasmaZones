// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBluetooth/BluetoothDevice.h>

#include <PhosphorDBus/Client.h>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcBluetoothDevice, "phosphor.service.bluetooth.device")

namespace {
constexpr auto kService = "org.bluez";
constexpr auto kDeviceIface = "org.bluez.Device1";
constexpr auto kPropsIface = "org.freedesktop.DBus.Properties";
} // namespace

namespace PhosphorServiceBluetooth {

class BluetoothDevice::Private
{
public:
    BluetoothDevice* owner = nullptr;
    QString path;
    QDBusConnection bus;

    QString address;
    QString name;
    QString alias;
    QString icon;
    bool paired = false;
    bool trusted = false;
    bool blocked = false;
    bool connected = false;
    int rssi = 0;
    QString adapter;
    QStringList uuids;

    explicit Private(QDBusConnection connection)
        : bus(std::move(connection))
    {
    }

    template<typename T, typename Signal>
    void setField(T& field, T val, Signal signal)
    {
        if (field == val)
            return;
        field = val;
        Q_EMIT(owner->*signal)();
    }

    // Re-fetch the whole Device1 interface; used when PropertiesChanged
    // reports invalidated properties (which carry no value inline).
    void requestAll()
    {
        if (!bus.isConnected())
            return;
        PhosphorDBus::Client client(bus, QLatin1String(kService), path, &lcBluetoothDevice());
        auto* watcher = new QDBusPendingCallWatcher(
            client.asyncCall(QLatin1String(kPropsIface), QStringLiteral("GetAll"), {QLatin1String(kDeviceIface)}),
            owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QVariantMap> reply = *call;
            if (reply.isError()) {
                qCDebug(lcBluetoothDevice) << "GetAll failed for" << path << ":" << reply.error().message();
                return;
            }
            applyProps(reply.value());
        });
    }

    // Fire-and-forget Properties.Set on the Device1 interface. The cached
    // value is not touched here; it moves only on the PropertiesChanged echo.
    void setDeviceProperty(const QString& property, const QVariant& value)
    {
        if (!bus.isConnected())
            return;
        PhosphorDBus::Client client(bus, QLatin1String(kService), path, &lcBluetoothDevice());
        client.fireAndForget(owner, QLatin1String(kPropsIface), QStringLiteral("Set"),
                             {QString::fromLatin1(kDeviceIface), property, QVariant::fromValue(QDBusVariant(value))},
                             QStringLiteral("setDeviceProperty"));
    }

    // Fire-and-forget no-argument method call on the Device1 interface.
    void callDeviceMethod(const QString& method)
    {
        if (!bus.isConnected())
            return;
        PhosphorDBus::Client client(bus, QLatin1String(kService), path, &lcBluetoothDevice());
        client.fireAndForget(owner, QLatin1String(kDeviceIface), method, {}, method);
    }

    // Applies a Device1 property map. Works for both the initial map from the
    // ObjectManager enumeration and a partial PropertiesChanged `changed` map;
    // every field is gated on isValid().
    void applyProps(const QVariantMap& props)
    {
        auto val = [&props](const char* key) -> QVariant {
            return props.value(QLatin1String(key));
        };
        QVariant v;
        if ((v = val("Address")).isValid())
            setField(address, v.toString(), &BluetoothDevice::addressChanged);
        if ((v = val("Name")).isValid())
            setField(name, v.toString(), &BluetoothDevice::nameChanged);
        if ((v = val("Alias")).isValid())
            setField(alias, v.toString(), &BluetoothDevice::aliasChanged);
        if ((v = val("Icon")).isValid())
            setField(icon, v.toString(), &BluetoothDevice::iconChanged);
        if ((v = val("Paired")).isValid())
            setField(paired, v.toBool(), &BluetoothDevice::pairedChanged);
        if ((v = val("Trusted")).isValid())
            setField(trusted, v.toBool(), &BluetoothDevice::trustedChanged);
        if ((v = val("Blocked")).isValid())
            setField(blocked, v.toBool(), &BluetoothDevice::blockedChanged);
        if ((v = val("Connected")).isValid())
            setField(connected, v.toBool(), &BluetoothDevice::connectedChanged);
        if ((v = val("RSSI")).isValid())
            setField(rssi, v.toInt(), &BluetoothDevice::rssiChanged);
        if ((v = val("Adapter")).isValid())
            setField(adapter, v.value<QDBusObjectPath>().path(), &BluetoothDevice::adapterChanged);
        if ((v = val("UUIDs")).isValid())
            setField(uuids, extractStringList(v), &BluetoothDevice::uuidsChanged);
    }

    // BlueZ exposes a device's UUIDs as `as`. Over the wire that nests inside
    // the `a{sv}` property dict as a QDBusArgument-wrapped variant, NOT a
    // QStringList, so QVariant::toStringList() would silently yield an empty
    // list; demarshal it by hand in that case. (The in-process initial map can
    // carry a real QStringList, which the fast path handles.)
    static QStringList extractStringList(const QVariant& value)
    {
        if (value.canConvert<QStringList>())
            return value.toStringList();
        if (value.canConvert<QDBusArgument>()) {
            QStringList list;
            value.value<QDBusArgument>() >> list;
            return list;
        }
        return {};
    }

    // Reset fields BlueZ reports as invalidated (no inline value). RSSI is the
    // one tracked property BlueZ drops when a device goes out of range during
    // discovery; clear it to 0 so `rssi` matches the documented out-of-range
    // value rather than going stale at the last-seen reading.
    void clearInvalidated(const QStringList& invalidated)
    {
        if (invalidated.contains(QLatin1String("RSSI")))
            setField(rssi, 0, &BluetoothDevice::rssiChanged);
    }
};

BluetoothDevice::BluetoothDevice(QDBusConnection connection, const QString& dbusPath,
                                 const QVariantMap& initialProperties, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(std::move(connection)))
{
    d->owner = this;
    d->path = dbusPath;
    d->applyProps(initialProperties);

    if (!d->bus.isConnected()) {
        qCWarning(lcBluetoothDevice) << "bus unavailable; device inert:" << dbusPath;
        return;
    }
    const bool ok = d->bus.connect(QLatin1String(kService), dbusPath, QLatin1String(kPropsIface),
                                   QStringLiteral("PropertiesChanged"), this,
                                   SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));
    if (!ok)
        qCWarning(lcBluetoothDevice) << "PropertiesChanged subscription failed for" << dbusPath;
}

BluetoothDevice::~BluetoothDevice() = default;

QString BluetoothDevice::dbusPath() const
{
    return d->path;
}
QString BluetoothDevice::address() const
{
    return d->address;
}
QString BluetoothDevice::name() const
{
    return d->name;
}
QString BluetoothDevice::alias() const
{
    return d->alias;
}
QString BluetoothDevice::icon() const
{
    return d->icon;
}
bool BluetoothDevice::paired() const
{
    return d->paired;
}
bool BluetoothDevice::trusted() const
{
    return d->trusted;
}
bool BluetoothDevice::blocked() const
{
    return d->blocked;
}
bool BluetoothDevice::connected() const
{
    return d->connected;
}
int BluetoothDevice::rssi() const
{
    return d->rssi;
}
QString BluetoothDevice::adapter() const
{
    return d->adapter;
}
QStringList BluetoothDevice::uuids() const
{
    return d->uuids;
}

void BluetoothDevice::connectDevice()
{
    d->callDeviceMethod(QStringLiteral("Connect"));
}

void BluetoothDevice::disconnectDevice()
{
    d->callDeviceMethod(QStringLiteral("Disconnect"));
}

void BluetoothDevice::setTrusted(bool trusted)
{
    d->setDeviceProperty(QStringLiteral("Trusted"), trusted);
}

void BluetoothDevice::setBlocked(bool blocked)
{
    d->setDeviceProperty(QStringLiteral("Blocked"), blocked);
}

void BluetoothDevice::pair()
{
    d->callDeviceMethod(QStringLiteral("Pair"));
}

void BluetoothDevice::cancelPairing()
{
    d->callDeviceMethod(QStringLiteral("CancelPairing"));
}

void BluetoothDevice::_q_onPropertiesChanged(const QString& interfaceName, const QVariantMap& changed,
                                             const QStringList& invalidated)
{
    if (interfaceName != QLatin1String(kDeviceIface))
        return;
    d->applyProps(changed);
    if (!invalidated.isEmpty()) {
        d->clearInvalidated(invalidated);
        // A bare RSSI drop is the common discovery case and is already handled
        // by clearInvalidated; re-fetching only to find RSSI still absent is
        // pure overhead, so re-fetch only when something else was invalidated.
        const bool onlyRssi = invalidated.size() == 1 && invalidated.contains(QLatin1String("RSSI"));
        if (!onlyRssi)
            d->requestAll();
    }
}

} // namespace PhosphorServiceBluetooth
