// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorDBus/ObjectManager.h>

#include <PhosphorDBus/Client.h>
#include <PhosphorDBus/Logging.h>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QMetaType>

#include <mutex>

namespace {
constexpr auto kObjectManagerIface = "org.freedesktop.DBus.ObjectManager";

// Demarshal an `a{sa{sv}}` (interface-name → property-dict) container from the
// current position of @p arg. The inner `a{sv}` demarshals straight into
// QVariantMap via Qt's built-in operators, so only the outer map is walked by
// hand — the same approach NetworkConnection uses for connection settings.
PhosphorDBus::InterfaceMap demarshalInterfaceMap(const QDBusArgument& arg)
{
    PhosphorDBus::InterfaceMap interfaces;
    arg.beginMap();
    while (!arg.atEnd()) {
        arg.beginMapEntry();
        QString interfaceName;
        QVariantMap properties;
        arg >> interfaceName >> properties;
        arg.endMapEntry();
        interfaces.insert(interfaceName, properties);
    }
    arg.endMap();
    return interfaces;
}

// Pull the `as` second argument of InterfacesRemoved out of the message. NM-
// style `as` usually arrives already demarshalled to QStringList, but fall
// back to hand-demarshalling a QDBusArgument so a stricter peer is handled too.
QStringList interfaceNames(const QVariant& value)
{
    if (value.canConvert<QStringList>())
        return value.toStringList();
    if (value.canConvert<QDBusArgument>()) {
        QStringList names;
        value.value<QDBusArgument>() >> names;
        return names;
    }
    return {};
}
} // namespace

namespace PhosphorDBus {

class ObjectManager::Private
{
public:
    ObjectManager* owner = nullptr;
    QDBusConnection bus;
    QString service;
    QString rootPath;
    const QLoggingCategory* log = nullptr;
    bool ready = false;

    explicit Private(QDBusConnection connection)
        : bus(std::move(connection))
    {
    }

    void requestManagedObjects()
    {
        Client client(bus, service, rootPath, log);
        auto* watcher = new QDBusPendingCallWatcher(
            client.asyncCall(QLatin1String(kObjectManagerIface), QStringLiteral("GetManagedObjects")), owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<> reply = *call;
            if (reply.isError()) {
                // Debug, not warning: an absent service (e.g. the daemon isn't
                // running) is an expected steady state, not an operator-visible
                // fault. The observer simply surfaces nothing until it appears.
                qCDebug(*log) << "GetManagedObjects failed for" << service << ":" << reply.error().message();
            } else {
                applyManagedObjects(reply.reply());
            }
            // ready fires exactly once, the first time the round-trip
            // terminates (success or error), so a consumer has a deterministic
            // edge after the initial snapshot. The guard makes the contract
            // code-enforced rather than relying on a single call site.
            if (ready)
                return;
            ready = true;
            Q_EMIT owner->ready();
        });
    }

    // Demarshal the `a{oa{sa{sv}}}` GetManagedObjects reply and surface each
    // object as an interfacesAdded.
    void applyManagedObjects(const QDBusMessage& reply)
    {
        const QVariantList args = reply.arguments();
        if (args.isEmpty() || !args.at(0).canConvert<QDBusArgument>()) {
            qCDebug(*log) << "GetManagedObjects returned an unexpected reply shape for" << service;
            return;
        }
        // The outer arg type is guarded above; the per-entry walk then trusts
        // the `a{oa{sa{sv}}}` signature (QtDBus offers no per-entry signature
        // inspection mid-stream). A peer returning a different entry shape
        // yields default-constructed values here, never a crash.
        const QDBusArgument arg = args.at(0).value<QDBusArgument>();
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            QDBusObjectPath path;
            arg >> path;
            const InterfaceMap interfaces = demarshalInterfaceMap(arg);
            arg.endMapEntry();
            Q_EMIT owner->interfacesAdded(path.path(), interfaces);
        }
        arg.endMap();
    }

    void handleInterfacesAdded(const QDBusMessage& message)
    {
        const QVariantList args = message.arguments();
        if (args.size() < 2 || !args.at(1).canConvert<QDBusArgument>()) {
            qCDebug(*log) << "InterfacesAdded with an unexpected shape from" << service;
            return;
        }
        const QString path = args.at(0).value<QDBusObjectPath>().path();
        const InterfaceMap interfaces = demarshalInterfaceMap(args.at(1).value<QDBusArgument>());
        Q_EMIT owner->interfacesAdded(path, interfaces);
    }

    void handleInterfacesRemoved(const QDBusMessage& message)
    {
        const QVariantList args = message.arguments();
        if (args.size() < 2) {
            qCDebug(*log) << "InterfacesRemoved with an unexpected shape from" << service;
            return;
        }
        const QStringList interfaces = interfaceNames(args.at(1));
        if (interfaces.isEmpty()) {
            // A removal carrying no interface names is either malformed or a
            // no-op; emitting interfacesRemoved(path, {}) would be a signal
            // with no state transition behind it, so drop it.
            qCDebug(*log) << "InterfacesRemoved with no interface names from" << service << "for"
                          << args.at(0).value<QDBusObjectPath>().path();
            return;
        }
        Q_EMIT owner->interfacesRemoved(args.at(0).value<QDBusObjectPath>().path(), interfaces);
    }
};

ObjectManager::ObjectManager(QDBusConnection connection, QString service, QString rootPath, QObject* parent,
                             const QLoggingCategory* log)
    : QObject(parent)
    , d(std::make_unique<Private>(std::move(connection)))
{
    d->owner = this;
    d->service = std::move(service);
    d->rootPath = std::move(rootPath);
    d->log = log ? log : &lcPhosphorDBus();

    // interfacesAdded carries an InterfaceMap; register it so the signal can
    // also cross a queued (cross-thread) connection. Harmless and idempotent
    // for the common same-thread direct-connection consumer.
    static std::once_flag metaTypeOnce;
    std::call_once(metaTypeOnce, [] {
        qRegisterMetaType<InterfaceMap>("PhosphorDBus::InterfaceMap");
    });

    if (!d->bus.isConnected()) {
        qCWarning(*d->log) << "bus unavailable; ObjectManager inert for" << d->service;
        return;
    }

    // Subscribe before the initial walk so an object that appears between the
    // GetManagedObjects request and its reply is still surfaced (the duplicate
    // interfacesAdded is harmless — consumers dedupe by path).
    const bool addedOk =
        d->bus.connect(d->service, d->rootPath, QLatin1String(kObjectManagerIface), QStringLiteral("InterfacesAdded"),
                       this, SLOT(_q_onInterfacesAdded(QDBusMessage)));
    const bool removedOk =
        d->bus.connect(d->service, d->rootPath, QLatin1String(kObjectManagerIface), QStringLiteral("InterfacesRemoved"),
                       this, SLOT(_q_onInterfacesRemoved(QDBusMessage)));
    if (!addedOk || !removedOk)
        qCWarning(*d->log) << "ObjectManager signal subscription failed for" << d->service;

    d->requestManagedObjects();
}

ObjectManager::~ObjectManager() = default;

QString ObjectManager::service() const
{
    return d->service;
}

QString ObjectManager::rootPath() const
{
    return d->rootPath;
}

bool ObjectManager::isReady() const
{
    return d->ready;
}

void ObjectManager::_q_onInterfacesAdded(const QDBusMessage& message)
{
    d->handleInterfacesAdded(message);
}

void ObjectManager::_q_onInterfacesRemoved(const QDBusMessage& message)
{
    d->handleInterfacesRemoved(message);
}

} // namespace PhosphorDBus
