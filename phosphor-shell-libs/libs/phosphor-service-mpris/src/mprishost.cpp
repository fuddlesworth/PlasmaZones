// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceMpris/MprisHost.h>
#include <PhosphorServiceMpris/MprisPlayer.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcMprisHost, "phosphor.service.mpris.host")

static const QLatin1String kMprisPrefix("org.mpris.MediaPlayer2.");

namespace PhosphorServiceMpris {

class MprisHost::Private
{
public:
    MprisHost* owner = nullptr;
    QList<MprisPlayer*> players;

    void addService(const QString& service)
    {
        if (!service.startsWith(kMprisPrefix))
            return;
        // Reject the bare prefix (literal "org.mpris.MediaPlayer2."
        // with empty suffix) and any other malformed name. The MPRIS
        // spec guarantees the suffix is a non-empty bus-name segment;
        // accepting an empty one would build a MprisPlayer with an
        // unusable service name.
        if (service.size() <= kMprisPrefix.size())
            return;
        for (auto* p : std::as_const(players)) {
            if (p->serviceName() == service)
                return;
        }
        auto* player = new MprisPlayer(service, owner);
        players.append(player);
        // identity() is still empty here; it populates from an async
        // GetAll, so it is deliberately not logged.
        qCDebug(lcMprisHost) << "Player added:" << service;
        Q_EMIT owner->playerAdded(player);
        Q_EMIT owner->playerCountChanged();
    }

    void removeService(const QString& service)
    {
        for (int i = 0; i < players.size(); ++i) {
            if (players.at(i)->serviceName() == service) {
                auto* player = players.at(i);
                qCDebug(lcMprisHost) << "Player removed:" << service;
                // Remove from the list BEFORE emitting playerRemoved so
                // observers (including the synchronously-running
                // _q_nameOwnerChanged ownership-transfer path that
                // follows this call with addService(service)) see the
                // already-detached state. Earlier order (emit then
                // remove) lost the new player on every transfer
                // because addService's dedup check at line 30 would
                // match the still-listed old entry and return early.
                players.removeAt(i);
                Q_EMIT owner->playerRemoved(player);
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
    if (!bus.isConnected()) {
        // CI / headless / sandboxed environments may run without a
        // session bus. We never block on bus presence, but logging it
        // here makes "the media widget is empty" diagnosable without
        // a strace.
        qCInfo(lcMprisHost) << "session bus unavailable; MprisHost is inert";
        return;
    }

    // Subscribe to NameOwnerChanged directly. QDBusServiceWatcher does
    // NOT support wildcards, addWatchedService("org.mpris.MediaPlayer2.*")
    // builds a match rule with arg0 set to that literal string, which
    // never fires for real MPRIS service names like
    // "org.mpris.MediaPlayer2.spotify". The startup scan below would
    // catch already-running players, but a player launched AFTER the
    // host's construction would never produce a serviceRegistered
    // signal, symptom: the panel widget stays hidden until the shell
    // is restarted with the player already up.
    //
    // The bus-wide NameOwnerChanged subscription has no arg0 filter so
    // it sees every name change on the session bus. We filter by
    // prefix in the handler. The volume of NameOwnerChanged on a
    // typical session is low (handfuls per second at peak), so the
    // global subscription cost is negligible and is the canonical fix
    // used by playerctld, plasma-mpris, and friends.
    const bool subscribed = bus.connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"),
                                        this, SLOT(_q_nameOwnerChanged(QString, QString, QString)));
    if (!subscribed) {
        // Symptom of a silently-failed subscription: the host catches
        // already-running players via the ListNames scan below, but
        // anything launched afterwards is invisible to us. Loud so the
        // bug shows up in journals when it happens.
        qCWarning(lcMprisHost) << "NameOwnerChanged subscription failed; new players will not be detected";
    }

    // Startup scan for already-running players via an async ListNames.
    // A blocking call here would freeze the GUI thread on a slow session
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
    // (rare for MPRIS), handled by destroying the old MprisPlayer and
    // constructing a fresh one against the new owner.
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

} // namespace PhosphorServiceMpris
