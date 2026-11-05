// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceNetwork/phosphorservicenetwork_export.h>

#include <PhosphorServiceNetwork/NetworkConnection.h>

#include <QAbstractListModel>
#include <QDBusConnection>
#include <QDBusObjectPath>

namespace PhosphorServiceNetwork {

/// List model over the saved connection profiles NetworkManager knows
/// about (`org.freedesktop.NetworkManager.Settings`). Self-contained:
/// it binds the Settings object on construction, lists existing
/// connections, and tracks NewConnection / ConnectionRemoved. Owns the
/// NetworkConnection row objects (parented to it). No host binding
/// needed; just instantiate it.
class PHOSPHORSERVICENETWORK_EXPORT NetworkConnectionModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NetworkConnectionModel)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        ConnectionRole = Qt::UserRole + 1,
        IdRole,
        UuidRole,
        ConnectionTypeRole,
    };
    Q_ENUM(Roles)

    explicit NetworkConnectionModel(QObject* parent = nullptr);
    ~NetworkConnectionModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void countChanged();

private Q_SLOTS:
    void _q_onNewConnection(const QDBusObjectPath& path);
    void _q_onConnectionRemoved(const QDBusObjectPath& path);

private:
    void addConnection(const QString& path);
    void removeConnection(const QString& path);
    void connectConnection(NetworkConnection* connection);

    QDBusConnection m_bus = QDBusConnection::systemBus();
    // Row objects owned by the model (parented to it).
    QList<NetworkConnection*> m_rows;
};

} // namespace PhosphorServiceNetwork
