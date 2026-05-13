// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/MprisPlayerModel.h>
#include <PhosphorServices/MprisHost.h>
#include <PhosphorServices/MprisPlayer.h>

namespace PhosphorServices {

MprisPlayerModel::MprisPlayerModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

MprisPlayerModel::~MprisPlayerModel() = default;

MprisHost* MprisPlayerModel::host() const
{
    return m_host;
}

void MprisPlayerModel::setHost(MprisHost* host)
{
    if (m_host == host)
        return;
    beginResetModel();
    if (m_host) {
        disconnect(m_host, nullptr, this, nullptr);
    }
    m_host = host;
    if (m_host) {
        for (auto* player : m_host->players())
            connectPlayer(player);
        connect(m_host, &MprisHost::playerAdded, this, &MprisPlayerModel::onPlayerAdded);
        connect(m_host, &MprisHost::playerRemoved, this, &MprisPlayerModel::onPlayerRemoved);
    }
    endResetModel();
    Q_EMIT hostChanged();
    Q_EMIT countChanged();
}

int MprisPlayerModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_host ? m_host->playerCount() : 0;
}

QVariant MprisPlayerModel::data(const QModelIndex& index, int role) const
{
    if (!m_host || !index.isValid() || index.row() >= m_host->playerCount())
        return {};
    auto* player = m_host->playerAt(index.row());
    if (!player)
        return {};
    switch (role) {
    case PlayerRole:
        return QVariant::fromValue<QObject*>(player);
    case IdentityRole:
        return player->identity();
    case PlaybackStateRole:
        return static_cast<int>(player->playbackState());
    case TrackTitleRole:
        return player->trackTitle();
    case TrackArtistRole:
        return player->trackArtist();
    case ArtUrlRole:
        return player->trackArtUrl();
    default:
        return {};
    }
}

QHash<int, QByteArray> MprisPlayerModel::roleNames() const
{
    return {
        {PlayerRole, "player"},         {IdentityRole, "identity"},       {PlaybackStateRole, "playbackState"},
        {TrackTitleRole, "trackTitle"}, {TrackArtistRole, "trackArtist"}, {ArtUrlRole, "artUrl"},
    };
}

void MprisPlayerModel::onPlayerAdded(MprisPlayer* player)
{
    if (!m_host)
        return;
    const int row = m_host->playerCount() - 1;
    beginInsertRows({}, row, row);
    connectPlayer(player);
    endInsertRows();
    Q_EMIT countChanged();
}

void MprisPlayerModel::onPlayerRemoved(MprisPlayer* player)
{
    if (!m_host)
        return;
    const auto& players = m_host->players();
    int row = -1;
    for (int i = 0; i < players.size(); ++i) {
        if (players.at(i) == player) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        beginResetModel();
        endResetModel();
    } else {
        beginRemoveRows({}, row, row);
        endRemoveRows();
    }
    Q_EMIT countChanged();
}

void MprisPlayerModel::onPlayerDataChanged(MprisPlayer* player)
{
    if (!m_host)
        return;
    const auto& players = m_host->players();
    for (int i = 0; i < players.size(); ++i) {
        if (players.at(i) == player) {
            Q_EMIT dataChanged(index(i), index(i));
            return;
        }
    }
}

void MprisPlayerModel::connectPlayer(MprisPlayer* player)
{
    auto refresh = [this, player]() {
        onPlayerDataChanged(player);
    };
    connect(player, &MprisPlayer::identityChanged, this, refresh);
    connect(player, &MprisPlayer::playbackStateChanged, this, refresh);
    connect(player, &MprisPlayer::metadataChanged, this, refresh);
}

} // namespace PhosphorServices
