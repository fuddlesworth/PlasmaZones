// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSession/SessionHost.h>

#include <PhosphorDBus/Client.h>

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

#include <utility>

Q_LOGGING_CATEGORY(lcSessionHost, "phosphor.service.session.host")

namespace {
constexpr auto kManagerPath = "/org/freedesktop/login1";
constexpr auto kManagerIface = "org.freedesktop.login1.Manager";
} // namespace

namespace PhosphorServiceSession {

namespace {
// logind's Can* methods answer with one of these strings; anything else (an
// error, an empty reply, an unknown distro extension) maps to Unknown.
SessionHost::Availability parseAvailability(const QString& value)
{
    if (value == QLatin1String("yes"))
        return SessionHost::Availability::Yes;
    if (value == QLatin1String("no"))
        return SessionHost::Availability::No;
    if (value == QLatin1String("na"))
        return SessionHost::Availability::NotApplicable;
    if (value == QLatin1String("challenge"))
        return SessionHost::Availability::Challenge;
    return SessionHost::Availability::Unknown;
}
} // namespace

class SessionHost::Private
{
public:
    Private(QDBusConnection connection, QString service)
        : bus(std::move(connection))
        , service(std::move(service))
    {
    }

    SessionHost* owner = nullptr;
    QDBusConnection bus;
    QString service;

    Availability canPowerOff = Availability::Unknown;
    Availability canReboot = Availability::Unknown;
    Availability canHalt = Availability::Unknown;
    Availability canSuspend = Availability::Unknown;
    Availability canHibernate = Availability::Unknown;
    Availability canHybridSleep = Availability::Unknown;
    Availability canSuspendThenHibernate = Availability::Unknown;

    // Issue one async Manager.Can* call per capability and fold each reply into
    // the cached value, emitting capabilitiesChanged only on an actual change.
    // Inert when the bus is not connected (no logind): the values stay Unknown
    // and no call is issued.
    void refresh()
    {
        if (!bus.isConnected())
            return;

        PhosphorDBus::Client manager(bus, service, QLatin1String(kManagerPath), &lcSessionHost());

        struct Cap
        {
            const char* method;
            Availability Private::* slot;
        };
        static constexpr Cap caps[] = {
            {"CanPowerOff", &Private::canPowerOff},
            {"CanReboot", &Private::canReboot},
            {"CanHalt", &Private::canHalt},
            {"CanSuspend", &Private::canSuspend},
            {"CanHibernate", &Private::canHibernate},
            {"CanHybridSleep", &Private::canHybridSleep},
            {"CanSuspendThenHibernate", &Private::canSuspendThenHibernate},
        };

        for (const auto& cap : caps) {
            const QDBusPendingCall pending = manager.asyncCall(QLatin1String(kManagerIface), QLatin1String(cap.method));
            auto* watcher = new QDBusPendingCallWatcher(pending, owner);
            const auto slot = cap.slot;
            QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner,
                             [this, slot](QDBusPendingCallWatcher* call) {
                                 call->deleteLater();
                                 const QDBusPendingReply<QString> reply = *call;
                                 const Availability value =
                                     reply.isError() ? Availability::Unknown : parseAvailability(reply.value());
                                 if (this->*slot != value) {
                                     this->*slot = value;
                                     Q_EMIT owner->capabilitiesChanged();
                                 }
                             });
        }
    }
};

SessionHost::SessionHost(QObject* parent)
    : SessionHost(QDBusConnection::systemBus(), QStringLiteral("org.freedesktop.login1"), parent)
{
}

SessionHost::SessionHost(QDBusConnection connection, QString service, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(std::move(connection), std::move(service)))
{
    d->owner = this;
    d->refresh();
}

SessionHost::~SessionHost() = default;

SessionHost::Availability SessionHost::canPowerOff() const
{
    return d->canPowerOff;
}

SessionHost::Availability SessionHost::canReboot() const
{
    return d->canReboot;
}

SessionHost::Availability SessionHost::canHalt() const
{
    return d->canHalt;
}

SessionHost::Availability SessionHost::canSuspend() const
{
    return d->canSuspend;
}

SessionHost::Availability SessionHost::canHibernate() const
{
    return d->canHibernate;
}

SessionHost::Availability SessionHost::canHybridSleep() const
{
    return d->canHybridSleep;
}

SessionHost::Availability SessionHost::canSuspendThenHibernate() const
{
    return d->canSuspendThenHibernate;
}

void SessionHost::refreshCapabilities()
{
    d->refresh();
}

} // namespace PhosphorServiceSession
