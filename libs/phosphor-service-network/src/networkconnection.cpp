// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNetwork/NetworkConnection.h>

#include <PhosphorDBus/Client.h>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcNetworkConnection, "phosphor.service.network.connection")

namespace {
constexpr auto kService = "org.freedesktop.NetworkManager";
constexpr auto kConnIface = "org.freedesktop.NetworkManager.Settings.Connection";
} // namespace

namespace PhosphorServiceNetwork {

class NetworkConnection::Private
{
public:
    NetworkConnection* owner = nullptr;
    QString path;
    QDBusConnection bus = QDBusConnection::systemBus();

    QString id;
    QString uuid;
    QString connectionType;

    template<typename T, typename Signal>
    void setField(T& field, T val, Signal signal)
    {
        if (field == val)
            return;
        field = val;
        Q_EMIT(owner->*signal)();
    }

    // GetSettings returns a{sa{sv}}: a map of setting-group name to a
    // property dict. We only need the "connection" group's id / uuid /
    // type, so iterate the outer map by hand rather than registering a
    // nested-container metatype.
    void requestSettings()
    {
        if (!bus.isConnected())
            return;
        PhosphorDBus::Client client(bus, QLatin1String(kService), path, &lcNetworkConnection());
        auto* watcher = new QDBusPendingCallWatcher(
            client.asyncCall(QLatin1String(kConnIface), QStringLiteral("GetSettings")), owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            QDBusPendingReply<> reply = *call;
            if (reply.isError()) {
                qCDebug(lcNetworkConnection) << "GetSettings failed for" << path << ":" << reply.error().message();
                return;
            }
            // Guard the wire shape before demarshalling: a malformed peer
            // (or a non-NM service squatting the path) could return a reply
            // with no arguments or a non-container first arg, and calling
            // beginMap() on a default-constructed QDBusArgument is not safe.
            const QVariantList replyArgs = reply.reply().arguments();
            if (replyArgs.isEmpty() || !replyArgs.at(0).canConvert<QDBusArgument>()) {
                qCDebug(lcNetworkConnection) << "GetSettings returned an unexpected reply shape for" << path;
                return;
            }
            applySettings(replyArgs.at(0).value<QDBusArgument>());
        });
    }

    void applySettings(const QDBusArgument& arg)
    {
        // a{sa{sv}}
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            QString group;
            QVariantMap dict;
            arg >> group >> dict;
            arg.endMapEntry();
            if (group == QLatin1String("connection")) {
                setField(id, dict.value(QStringLiteral("id")).toString(), &NetworkConnection::idChanged);
                setField(uuid, dict.value(QStringLiteral("uuid")).toString(), &NetworkConnection::uuidChanged);
                setField(connectionType, dict.value(QStringLiteral("type")).toString(),
                         &NetworkConnection::connectionTypeChanged);
                // The "connection" group is the only one we read; stop here
                // rather than demarshalling the remaining (often large)
                // ipv4/ipv6/security dicts only to discard them.
                break;
            }
        }
        arg.endMap();
    }
};

NetworkConnection::NetworkConnection(const QString& dbusPath, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;
    d->path = dbusPath;

    if (!d->bus.isConnected()) {
        qCWarning(lcNetworkConnection) << "system bus unavailable; connection inert:" << dbusPath;
        return;
    }
    const bool ok = d->bus.connect(QLatin1String(kService), dbusPath, QLatin1String(kConnIface),
                                   QStringLiteral("Updated"), this, SLOT(_q_onUpdated()));
    if (!ok)
        qCWarning(lcNetworkConnection) << "Updated subscription failed for" << dbusPath;

    d->requestSettings();
}

NetworkConnection::~NetworkConnection() = default;

QString NetworkConnection::dbusPath() const
{
    return d->path;
}
QString NetworkConnection::id() const
{
    return d->id;
}
QString NetworkConnection::uuid() const
{
    return d->uuid;
}
QString NetworkConnection::connectionType() const
{
    return d->connectionType;
}

void NetworkConnection::_q_onUpdated()
{
    d->requestSettings();
}

} // namespace PhosphorServiceNetwork
