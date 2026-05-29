// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceMpris/MprisPlayerModel.h>
#include <PhosphorServiceMpris/MprisHost.h>
#include <PhosphorServiceMpris/MprisPlayer.h>

namespace PhosphorServiceMpris {

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
    const int previousCount = m_rows.size();
    beginResetModel();
    if (m_host) {
        disconnect(m_host, nullptr, this, nullptr);
        // The old players belong to the old host (which lives on), so
        // their connections to this model would otherwise leak.
        for (auto* player : std::as_const(m_rows))
            disconnect(player, nullptr, this, nullptr);
    }
    m_host = host;
    m_rows.clear();
    if (m_host) {
        m_rows = m_host->players();
        for (auto* player : std::as_const(m_rows))
            connectPlayer(player);
        connect(m_host, &MprisHost::playerAdded, this, &MprisPlayerModel::onPlayerAdded);
        connect(m_host, &MprisHost::playerRemoved, this, &MprisPlayerModel::onPlayerRemoved);
        // The host owns the players; if it is destroyed while still set,
        // m_host and every m_rows entry would dangle. Drop them all.
        connect(m_host, &QObject::destroyed, this, [this]() {
            const int prev = m_rows.size();
            beginResetModel();
            m_rows.clear();
            m_host = nullptr;
            endResetModel();
            Q_EMIT hostChanged();
            if (prev != 0)
                Q_EMIT countChanged();
        });
    }
    endResetModel();
    Q_EMIT hostChanged();
    // Only emit countChanged when the row count actually moved
    // (CLAUDE.md "only emit on change" rule). An attach-empty or
    // detach-from-empty path produces no count delta.
    if (previousCount != m_rows.size())
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
    disconnect(player, nullptr, this, nullptr);
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

void MprisPlayerModel::onPlayerDataChanged(MprisPlayer* player, const QList<int>& roles)
{
    const int row = m_rows.indexOf(player);
    if (row >= 0) {
        const auto idx = index(row);
        // Passing the role hint lets QML delegates skip rebinding for
        // roles that did not change. An empty list (the prior default)
        // forces every delegate to refresh every role on every signal.
        Q_EMIT dataChanged(idx, idx, roles);
    }
}

void MprisPlayerModel::connectPlayer(MprisPlayer* player)
{
    connect(player, &MprisPlayer::identityChanged, this, [this, player]() {
        onPlayerDataChanged(player, {IdentityRole});
    });
    connect(player, &MprisPlayer::playbackStateChanged, this, [this, player]() {
        onPlayerDataChanged(player, {PlaybackStateRole});
    });
    connect(player, &MprisPlayer::metadataChanged, this, [this, player]() {
        onPlayerDataChanged(player, {TrackTitleRole, TrackArtistRole, ArtUrlRole});
    });
}

} // namespace PhosphorServiceMpris
