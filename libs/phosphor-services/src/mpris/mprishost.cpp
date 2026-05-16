// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/MprisHost.h>
#include <PhosphorServices/MprisPlayer.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcMprisHost, "phosphorservices.mpris.host")

static const QLatin1String kMprisPrefix("org.mpris.MediaPlayer2.");

namespace PhosphorServices {

class MprisHost::Private
{
public:
    MprisHost* owner = nullptr;
    QList<MprisPlayer*> players;

    void addService(const QString& service)
    {
        if (!service.startsWith(kMprisPrefix))
            return;
        for (auto* p : std::as_const(players)) {
            if (p->serviceName() == service)
                return;
        }
        auto* player = new MprisPlayer(service, owner);
        players.append(player);
        qCDebug(lcMprisHost) << "Player added:" << service << player->identity();
        Q_EMIT owner->playerAdded(player);
        Q_EMIT owner->playerCountChanged();
    }

    void removeService(const QString& service)
    {
        for (int i = 0; i < players.size(); ++i) {
            if (players.at(i)->serviceName() == service) {
                auto* player = players.at(i);
                qCDebug(lcMprisHost) << "Player removed:" << service;
                Q_EMIT owner->playerRemoved(player);
                players.removeAt(i);
                Q_EMIT owner->playerCountChanged();
                player->deleteLater();
                return;
            }
        }
    }
};

MprisHost::MprisHost(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;

    auto bus = QDBusConnection::sessionBus();

    // Subscribe to NameOwnerChanged directly. QDBusServiceWatcher does
    // NOT support wildcards — addWatchedService("org.mpris.MediaPlayer2.*")
    // builds a match rule with arg0 set to that literal string, which
    // never fires for real MPRIS service names like
    // "org.mpris.MediaPlayer2.spotify". The startup scan below would
    // catch already-running players, but a player launched AFTER the
    // host's construction would never produce a serviceRegistered
    // signal — symptom: the panel widget stays hidden until the shell
    // is restarted with the player already up.
    //
    // The bus-wide NameOwnerChanged subscription has no arg0 filter so
    // it sees every name change on the session bus. We filter by
    // prefix in the handler. The volume of NameOwnerChanged on a
    // typical session is low (handfuls per second at peak), so the
    // global subscription cost is negligible and is the canonical fix
    // used by playerctld, plasma-mpris, and friends.
    bus.connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"), this,
                SLOT(_q_nameOwnerChanged(QString, QString, QString)));

    // Startup scan for already-running players via an async ListNames —
    // a blocking call here would freeze the GUI thread on a slow session
    // bus. The watcher is parented to `this` so it cancels cleanly if the
    // host is destroyed before the reply lands.
    QDBusMessage listMsg =
        QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                       QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ListNames"));
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(listMsg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        const QDBusPendingReply<QStringList> reply = *call;
        if (reply.isError()) {
            qCWarning(lcMprisHost) << "ListNames failed:" << reply.error().message();
            return;
        }
        const QStringList services = reply.value();
        for (const QString& service : services) {
            d->addService(service);
        }
    });
}

MprisHost::~MprisHost() = default;

void MprisHost::_q_nameOwnerChanged(const QString& service, const QString& oldOwner, const QString& newOwner)
{
    if (!service.startsWith(kMprisPrefix))
        return;
    // NameOwnerChanged semantics: oldOwner empty + newOwner non-empty
    // = service registered. oldOwner non-empty + newOwner empty =
    // service unregistered. Both non-empty = ownership transferred
    // (rare for MPRIS, but treat as add — the player object will
    // re-resolve its proxy).
    if (newOwner.isEmpty()) {
        d->removeService(service);
    } else if (oldOwner.isEmpty()) {
        d->addService(service);
    } else {
        d->removeService(service);
        d->addService(service);
    }
}

int MprisHost::playerCount() const
{
    return d->players.size();
}

QList<MprisPlayer*> MprisHost::players() const
{
    return d->players;
}

MprisPlayer* MprisHost::playerAt(int index) const
{
    if (index < 0 || index >= d->players.size())
        return nullptr;
    return d->players.at(index);
}

} // namespace PhosphorServices
