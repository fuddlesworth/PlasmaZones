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
    if (m_host)
        disconnect(m_host, nullptr, this, nullptr);
    m_host = host;
    m_rows.clear();
    if (m_host) {
        m_rows = m_host->players();
        for (auto* player : std::as_const(m_rows))
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
    return m_rows.size();
}

QVariant MprisPlayerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    auto* player = m_rows.at(index.row());
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
    if (!player || m_rows.contains(player))
        return;
    const int row = m_rows.size();
    beginInsertRows({}, row, row);
    m_rows.append(player);
    connectPlayer(player);
    endInsertRows();
    Q_EMIT countChanged();
}

void MprisPlayerModel::onPlayerRemoved(MprisPlayer* player)
{
    const int row = m_rows.indexOf(player);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

void MprisPlayerModel::onPlayerDataChanged(MprisPlayer* player)
{
    const int row = m_rows.indexOf(player);
    if (row >= 0)
        Q_EMIT dataChanged(index(row), index(row));
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
