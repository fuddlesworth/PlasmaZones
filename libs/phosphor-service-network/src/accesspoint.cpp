// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNetwork/AccessPoint.h>

#include <PhosphorDBus/Client.h>

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcAccessPoint, "phosphor.service.network.accesspoint")

namespace {
constexpr auto kService = "org.freedesktop.NetworkManager";
constexpr auto kApIface = "org.freedesktop.NetworkManager.AccessPoint";
constexpr auto kPropsIface = "org.freedesktop.DBus.Properties";

// NM80211ApFlags / NM80211ApSecurityFlags bits we key on.
constexpr uint kApFlagPrivacy = 0x1; // WEP
constexpr uint kSecKeyMgmt8021x = 0x200; // enterprise
constexpr uint kSecKeyMgmtSae = 0x400; // WPA3-Personal

// Derive a human-readable security label from the AP's flag triple. Order
// matters: SAE (WPA3) and 802.1X (enterprise) are checked before the
// generic RSN/WPA buckets so the most specific label wins. SAE is an RSN
// key-management flag that NetworkManager only ever reports in RsnFlags
// (the legacy WPA word never carries it), so it is tested there alone.
// 802.1X, by contrast, can be advertised under either word, so it keeps
// the dual-word test.
QString securityLabel(uint flags, uint wpaFlags, uint rsnFlags)
{
    if (rsnFlags & kSecKeyMgmtSae)
        return QStringLiteral("WPA3");
    if ((rsnFlags & kSecKeyMgmt8021x) || (wpaFlags & kSecKeyMgmt8021x))
        return QStringLiteral("802.1X");
    if (rsnFlags != 0)
        return QStringLiteral("WPA2");
    if (wpaFlags != 0)
        return QStringLiteral("WPA");
    if (flags & kApFlagPrivacy)
        return QStringLiteral("WEP");
    return {};
}
} // namespace

namespace PhosphorServiceNetwork {

class AccessPoint::Private
{
public:
    AccessPoint* owner = nullptr;
    QString path;
    QDBusConnection bus = QDBusConnection::systemBus();

    QString ssid;
    int strength = 0;
    int frequency = 0;
    QString bssid;
    uint flags = 0;
    uint wpaFlags = 0;
    uint rsnFlags = 0;

    template<typename T, typename Signal>
    void setField(T& field, T val, Signal signal)
    {
        if (field == val)
            return;
        field = val;
        Q_EMIT(owner->*signal)();
    }

    void requestAll()
    {
        if (!bus.isConnected())
            return;
        PhosphorDBus::Client client(bus, QLatin1String(kService), path, &lcAccessPoint());
        auto* watcher = new QDBusPendingCallWatcher(
            client.asyncCall(QLatin1String(kPropsIface), QStringLiteral("GetAll"), {QLatin1String(kApIface)}), owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QVariantMap> reply = *call;
            if (reply.isError()) {
                qCDebug(lcAccessPoint) << "GetAll failed for" << path << ":" << reply.error().message();
                return;
            }
            applyProps(reply.value());
        });
    }

    void applyProps(const QVariantMap& props)
    {
        // Snapshot the derived security label up front so a change in ANY
        // of the three flag inputs surfaces exactly one securityChanged.
        const QString oldSecurity = securityLabel(flags, wpaFlags, rsnFlags);

        auto val = [&props](const char* name) -> QVariant {
            return props.value(QLatin1String(name));
        };
        QVariant v;
        if ((v = val("Ssid")).isValid())
            setField(ssid, QString::fromUtf8(v.toByteArray()), &AccessPoint::ssidChanged);
        if ((v = val("Strength")).isValid())
            setField(strength, static_cast<int>(v.toUInt()), &AccessPoint::strengthChanged);
        if ((v = val("Frequency")).isValid())
            setField(frequency, static_cast<int>(v.toUInt()), &AccessPoint::frequencyChanged);
        if ((v = val("HwAddress")).isValid())
            setField(bssid, v.toString(), &AccessPoint::bssidChanged);

        // Flag fields drive only the derived security label, so assign them
        // directly (no per-field NOTIFY) and emit securityChanged once below
        // if the label actually moved.
        if ((v = val("Flags")).isValid())
            flags = v.toUInt();
        if ((v = val("WpaFlags")).isValid())
            wpaFlags = v.toUInt();
        if ((v = val("RsnFlags")).isValid())
            rsnFlags = v.toUInt();

        if (oldSecurity != securityLabel(flags, wpaFlags, rsnFlags))
            Q_EMIT owner->securityChanged();
    }
};

AccessPoint::AccessPoint(const QString& dbusPath, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;
    d->path = dbusPath;

    if (!d->bus.isConnected()) {
        qCWarning(lcAccessPoint) << "system bus unavailable; access point inert:" << dbusPath;
        return;
    }
    const bool ok = d->bus.connect(QLatin1String(kService), dbusPath, QLatin1String(kPropsIface),
                                   QStringLiteral("PropertiesChanged"), this,
                                   SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));
    if (!ok)
        qCWarning(lcAccessPoint) << "PropertiesChanged subscription failed for" << dbusPath;

    d->requestAll();
}

AccessPoint::~AccessPoint() = default;

QString AccessPoint::dbusPath() const
{
    return d->path;
}
QString AccessPoint::ssid() const
{
    return d->ssid;
}
int AccessPoint::strength() const
{
    return d->strength;
}
int AccessPoint::frequency() const
{
    return d->frequency;
}
QString AccessPoint::bssid() const
{
    return d->bssid;
}
QString AccessPoint::security() const
{
    return securityLabel(d->flags, d->wpaFlags, d->rsnFlags);
}
bool AccessPoint::secured() const
{
    return !security().isEmpty();
}

void AccessPoint::_q_onPropertiesChanged(const QString& iface, const QVariantMap& changed,
                                         const QStringList& invalidated)
{
    if (iface != QLatin1String(kApIface))
        return;
    d->applyProps(changed);
    if (!invalidated.isEmpty())
        d->requestAll();
}

} // namespace PhosphorServiceNetwork
