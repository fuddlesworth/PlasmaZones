// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBluetooth/BluetoothAdapter.h>

#include <PhosphorDBus/Client.h>

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcBluetoothAdapter, "phosphor.service.bluetooth.adapter")

namespace {
constexpr auto kService = "org.bluez";
constexpr auto kAdapterIface = "org.bluez.Adapter1";
constexpr auto kPropsIface = "org.freedesktop.DBus.Properties";
} // namespace

namespace PhosphorServiceBluetooth {

class BluetoothAdapter::Private
{
public:
    BluetoothAdapter* owner = nullptr;
    QString path;
    QDBusConnection bus;

    QString address;
    QString name;
    QString alias;
    bool powered = false;
    bool discoverable = false;
    bool pairable = false;
    bool discovering = false;

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

    // Re-fetch the whole Adapter1 interface; used when PropertiesChanged
    // reports invalidated properties (which carry no value inline).
    void requestAll()
    {
        if (!bus.isConnected())
            return;
        PhosphorDBus::Client client(bus, QLatin1String(kService), path, &lcBluetoothAdapter());
        auto* watcher = new QDBusPendingCallWatcher(
            client.asyncCall(QLatin1String(kPropsIface), QStringLiteral("GetAll"), {QLatin1String(kAdapterIface)}),
            owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QVariantMap> reply = *call;
            if (reply.isError()) {
                qCDebug(lcBluetoothAdapter) << "GetAll failed for" << path << ":" << reply.error().message();
                return;
            }
            applyProps(reply.value());
        });
    }

    // Applies an Adapter1 property map. Works for both the initial map from
    // the ObjectManager enumeration and a partial PropertiesChanged `changed`
    // map; every field is gated on isValid().
    void applyProps(const QVariantMap& props)
    {
        auto val = [&props](const char* key) -> QVariant {
            return props.value(QLatin1String(key));
        };
        QVariant v;
        if ((v = val("Address")).isValid())
            setField(address, v.toString(), &BluetoothAdapter::addressChanged);
        if ((v = val("Name")).isValid())
            setField(name, v.toString(), &BluetoothAdapter::nameChanged);
        if ((v = val("Alias")).isValid())
            setField(alias, v.toString(), &BluetoothAdapter::aliasChanged);
        if ((v = val("Powered")).isValid())
            setField(powered, v.toBool(), &BluetoothAdapter::poweredChanged);
        if ((v = val("Discoverable")).isValid())
            setField(discoverable, v.toBool(), &BluetoothAdapter::discoverableChanged);
        if ((v = val("Pairable")).isValid())
            setField(pairable, v.toBool(), &BluetoothAdapter::pairableChanged);
        if ((v = val("Discovering")).isValid())
            setField(discovering, v.toBool(), &BluetoothAdapter::discoveringChanged);
    }
};

BluetoothAdapter::BluetoothAdapter(QDBusConnection connection, const QString& dbusPath,
                                   const QVariantMap& initialProperties, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(std::move(connection)))
{
    d->owner = this;
    d->path = dbusPath;
    d->applyProps(initialProperties);

    if (!d->bus.isConnected()) {
        qCWarning(lcBluetoothAdapter) << "bus unavailable; adapter inert:" << dbusPath;
        return;
    }
    const bool ok = d->bus.connect(QLatin1String(kService), dbusPath, QLatin1String(kPropsIface),
                                   QStringLiteral("PropertiesChanged"), this,
                                   SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));
    if (!ok)
        qCWarning(lcBluetoothAdapter) << "PropertiesChanged subscription failed for" << dbusPath;
}

BluetoothAdapter::~BluetoothAdapter() = default;

QString BluetoothAdapter::dbusPath() const
{
    return d->path;
}
QString BluetoothAdapter::address() const
{
    return d->address;
}
QString BluetoothAdapter::name() const
{
    return d->name;
}
QString BluetoothAdapter::alias() const
{
    return d->alias;
}
bool BluetoothAdapter::powered() const
{
    return d->powered;
}
bool BluetoothAdapter::discoverable() const
{
    return d->discoverable;
}
bool BluetoothAdapter::pairable() const
{
    return d->pairable;
}
bool BluetoothAdapter::discovering() const
{
    return d->discovering;
}

void BluetoothAdapter::_q_onPropertiesChanged(const QString& interfaceName, const QVariantMap& changed,
                                              const QStringList& invalidated)
{
    if (interfaceName != QLatin1String(kAdapterIface))
        return;
    d->applyProps(changed);
    if (!invalidated.isEmpty())
        d->requestAll();
}

} // namespace PhosphorServiceBluetooth
