// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/MprisHost.h>
#include <PhosphorServices/MprisPlayer.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcMprisHost, "phosphorservices.mpris.host")

static const QLatin1String kMprisPrefix("org.mpris.MediaPlayer2.");

namespace PhosphorServices {

class MprisHost::Private
{
public:
    MprisHost* owner = nullptr;
    QList<MprisPlayer*> players;
    QDBusServiceWatcher* watcher = nullptr;

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
                auto* player = players.takeAt(i);
                qCDebug(lcMprisHost) << "Player removed:" << service;
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
    d->watcher = new QDBusServiceWatcher(this);
    d->watcher->setConnection(bus);
    d->watcher->setWatchMode(QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration);
    d->watcher->addWatchedService(QStringLiteral("org.mpris.MediaPlayer2.*"));

    connect(d->watcher, &QDBusServiceWatcher::serviceRegistered, this, [this](const QString& service) {
        d->addService(service);
    });
    connect(d->watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString& service) {
        d->removeService(service);
    });

    const QStringList services = bus.interface()->registeredServiceNames().value();
    for (const QString& service : services) {
        d->addService(service);
    }
}

MprisHost::~MprisHost() = default;

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
