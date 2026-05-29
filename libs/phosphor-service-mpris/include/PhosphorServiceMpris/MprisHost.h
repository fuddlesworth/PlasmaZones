// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceMpris/phosphorservicempris_export.h>

#include <PhosphorServiceMpris/MprisPlayer.h>

#include <QList>
#include <QObject>

namespace PhosphorServiceMpris {

class PHOSPHORSERVICEMPRIS_EXPORT MprisHost : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MprisHost)
    Q_PROPERTY(int playerCount READ playerCount NOTIFY playerCountChanged)

public:
    explicit MprisHost(QObject* parent = nullptr);
    ~MprisHost() override;

    [[nodiscard]] int playerCount() const;
    [[nodiscard]] QList<MprisPlayer*> players() const;
    [[nodiscard]] Q_INVOKABLE PhosphorServiceMpris::MprisPlayer* playerAt(int index) const;

Q_SIGNALS:
    void playerAdded(PhosphorServiceMpris::MprisPlayer* player);
    void playerRemoved(PhosphorServiceMpris::MprisPlayer* player);
    void playerCountChanged();

private Q_SLOTS:
    /// org.freedesktop.DBus.NameOwnerChanged handler. Connected via
    /// QDBusConnection::connect with a SLOT() string match because Qt's
    /// D-Bus API doesn't expose a lambda-friendly overload. Routed to
    /// the Private to add/remove the corresponding MprisPlayer.
    void _q_nameOwnerChanged(const QString& service, const QString& oldOwner, const QString& newOwner);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceMpris
