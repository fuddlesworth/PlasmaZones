// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNetwork/NetworkConnectionModel.h>

#include <PhosphorDBus/Client.h>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcNetworkConnectionModel, "phosphor.service.network.connectionmodel")

namespace {
constexpr auto kService = "org.freedesktop.NetworkManager";
constexpr auto kSettingsPath = "/org/freedesktop/NetworkManager/Settings";
constexpr auto kSettingsIface = "org.freedesktop.NetworkManager.Settings";
} // namespace

namespace PhosphorServiceNetwork {

NetworkConnectionModel::NetworkConnectionModel(QObject* parent)
    : QAbstractListModel(parent)
{
    if (!m_bus.isConnected()) {
        qCWarning(lcNetworkConnectionModel) << "system bus unavailable: NetworkManager settings not accessible";
        return;
    }

    const bool newOk =
        m_bus.connect(QLatin1String(kService), QLatin1String(kSettingsPath), QLatin1String(kSettingsIface),
                      QStringLiteral("NewConnection"), this, SLOT(_q_onNewConnection(QDBusObjectPath)));
    const bool removedOk =
        m_bus.connect(QLatin1String(kService), QLatin1String(kSettingsPath), QLatin1String(kSettingsIface),
                      QStringLiteral("ConnectionRemoved"), this, SLOT(_q_onConnectionRemoved(QDBusObjectPath)));
    if (!newOk || !removedOk)
        qCWarning(lcNetworkConnectionModel) << "subscription failed: new=" << newOk << " removed=" << removedOk;

    PhosphorDBus::Client client(m_bus, QLatin1String(kService), QLatin1String(kSettingsPath),
                                &lcNetworkConnectionModel());
    auto* watcher = new QDBusPendingCallWatcher(
        client.asyncCall(QLatin1String(kSettingsIface), QStringLiteral("ListConnections")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        const QDBusPendingReply<QList<QDBusObjectPath>> reply = *call;
        if (reply.isError()) {
            qCWarning(lcNetworkConnectionModel) << "ListConnections failed:" << reply.error().message();
            return;
        }
        const auto paths = reply.value();
        for (const QDBusObjectPath& p : paths)
            addConnection(p.path());
    });
}

NetworkConnectionModel::~NetworkConnectionModel() = default;

int NetworkConnectionModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant NetworkConnectionModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    auto* connection = m_rows.at(index.row());
    if (!connection)
        return {};
    switch (role) {
    case ConnectionRole:
        return QVariant::fromValue<QObject*>(connection);
    case IdRole:
        return connection->id();
    case UuidRole:
        return connection->uuid();
    case ConnectionTypeRole:
        return connection->connectionType();
    default:
        return {};
    }
}

QHash<int, QByteArray> NetworkConnectionModel::roleNames() const
{
    return {
        {ConnectionRole, "connection"},
        {IdRole, "id"},
        {UuidRole, "uuid"},
        {ConnectionTypeRole, "connectionType"},
    };
}

void NetworkConnectionModel::addConnection(const QString& path)
{
    for (auto* c : std::as_const(m_rows)) {
        if (c->dbusPath() == path)
            return;
    }
    const int row = m_rows.size();
    beginInsertRows({}, row, row);
    auto* connection = new NetworkConnection(path, this);
    m_rows.append(connection);
    connectConnection(connection);
    endInsertRows();
    Q_EMIT countChanged();
}

void NetworkConnectionModel::removeConnection(const QString& path)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i)->dbusPath() == path) {
            auto* connection = m_rows.at(i);
            beginRemoveRows({}, i, i);
            m_rows.removeAt(i);
            endRemoveRows();
            connection->deleteLater();
            Q_EMIT countChanged();
            return;
        }
    }
}

void NetworkConnectionModel::connectConnection(NetworkConnection* connection)
{
    auto emitRoles = [this, connection](const QList<int>& roles) {
        const int row = m_rows.indexOf(connection);
        if (row >= 0) {
            const auto idx = index(row);
            Q_EMIT dataChanged(idx, idx, roles);
        }
    };
    connect(connection, &NetworkConnection::idChanged, this, [emitRoles]() {
        emitRoles({IdRole});
    });
    connect(connection, &NetworkConnection::uuidChanged, this, [emitRoles]() {
        emitRoles({UuidRole});
    });
    connect(connection, &NetworkConnection::connectionTypeChanged, this, [emitRoles]() {
        emitRoles({ConnectionTypeRole});
    });
}

void NetworkConnectionModel::_q_onNewConnection(const QDBusObjectPath& path)
{
    addConnection(path.path());
}

void NetworkConnectionModel::_q_onConnectionRemoved(const QDBusObjectPath& path)
{
    removeConnection(path.path());
}

} // namespace PhosphorServiceNetwork
