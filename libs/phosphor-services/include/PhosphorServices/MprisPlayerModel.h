// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServices/phosphorservices_export.h>

#include <PhosphorServices/MprisHost.h>
#include <PhosphorServices/MprisPlayer.h>

#include <QAbstractListModel>

namespace PhosphorServices {

class PHOSPHORSERVICES_EXPORT MprisPlayerModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(PhosphorServices::MprisHost* host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        PlayerRole = Qt::UserRole + 1,
        IdentityRole,
        PlaybackStateRole,
        TrackTitleRole,
        TrackArtistRole,
        ArtUrlRole,
    };
    Q_ENUM(Roles)

    explicit MprisPlayerModel(QObject* parent = nullptr);
    ~MprisPlayerModel() override;

    [[nodiscard]] MprisHost* host() const;
    void setHost(MprisHost* host);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void hostChanged();
    void countChanged();

private Q_SLOTS:
    void onPlayerAdded(PhosphorServices::MprisPlayer* player);
    void onPlayerRemoved(PhosphorServices::MprisPlayer* player);
    void onPlayerDataChanged(PhosphorServices::MprisPlayer* player);

private:
    void connectPlayer(MprisPlayer* player);

    MprisHost* m_host = nullptr;
    // Row mirror owned by the model. rowCount()/data() index into this
    // list, never the host's, so the begin/end-insert/remove transaction
    // boundaries always straddle the actual mutation regardless of when
    // the host emits playerAdded/playerRemoved relative to its own list.
    QList<MprisPlayer*> m_rows;
};

} // namespace PhosphorServices
