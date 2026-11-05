// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBluetooth/BluetoothAdapterModel.h>

#include <PhosphorServiceBluetooth/BluetoothAdapter.h>
#include <PhosphorServiceBluetooth/BluetoothHost.h>

namespace PhosphorServiceBluetooth {

BluetoothAdapterModel::BluetoothAdapterModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

BluetoothAdapterModel::~BluetoothAdapterModel() = default;

BluetoothHost* BluetoothAdapterModel::host() const
{
    return m_host;
}

void BluetoothAdapterModel::setHost(BluetoothHost* host)
{
    if (m_host == host)
        return;
    const int previousCount = m_rows.size();
    beginResetModel();
    if (m_host) {
        disconnect(m_host, nullptr, this, nullptr);
        // The old adapters belong to the old host (which lives on), so their
        // connections to this model would otherwise leak.
        for (auto* adapter : std::as_const(m_rows))
            disconnect(adapter, nullptr, this, nullptr);
    }
    m_host = host;
    m_rows.clear();
    if (m_host) {
        m_rows = m_host->adapters();
        for (auto* adapter : std::as_const(m_rows))
            connectAdapter(adapter);
        connect(m_host, &BluetoothHost::adapterAdded, this, &BluetoothAdapterModel::onAdapterAdded);
        connect(m_host, &BluetoothHost::adapterRemoved, this, &BluetoothAdapterModel::onAdapterRemoved);
        // The host owns the adapters; if it is destroyed while still set,
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
    // Only emit countChanged when the row count actually moved (a 0 -> 0
    // attach/detach stays silent per CLAUDE.md "only emit on change").
    if (previousCount != m_rows.size())
        Q_EMIT countChanged();
}

int BluetoothAdapterModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant BluetoothAdapterModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    auto* adapter = m_rows.at(index.row());
    if (!adapter)
        return {};
    switch (role) {
    case AdapterRole:
        return QVariant::fromValue<QObject*>(adapter);
    case AddressRole:
        return adapter->address();
    case NameRole:
        return adapter->name();
    case AliasRole:
        return adapter->alias();
    case PoweredRole:
        return adapter->powered();
    case DiscoverableRole:
        return adapter->discoverable();
    case PairableRole:
        return adapter->pairable();
    case DiscoveringRole:
        return adapter->discovering();
    default:
        return {};
    }
}

QHash<int, QByteArray> BluetoothAdapterModel::roleNames() const
{
    return {
        {AdapterRole, "adapter"},   {AddressRole, "address"},         {NameRole, "name"},
        {AliasRole, "alias"},       {PoweredRole, "powered"},         {DiscoverableRole, "discoverable"},
        {PairableRole, "pairable"}, {DiscoveringRole, "discovering"},
    };
}

void BluetoothAdapterModel::onAdapterAdded(BluetoothAdapter* adapter)
{
    if (!adapter || m_rows.contains(adapter))
        return;
    const int row = static_cast<int>(m_rows.size());
    beginInsertRows({}, row, row);
    m_rows.append(adapter);
    connectAdapter(adapter);
    endInsertRows();
    Q_EMIT countChanged();
}

void BluetoothAdapterModel::onAdapterRemoved(BluetoothAdapter* adapter)
{
    const int row = static_cast<int>(m_rows.indexOf(adapter));
    if (row < 0)
        return;
    disconnect(adapter, nullptr, this, nullptr);
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

void BluetoothAdapterModel::onAdapterDataChanged(BluetoothAdapter* adapter, const QList<int>& roles)
{
    const int row = static_cast<int>(m_rows.indexOf(adapter));
    if (row >= 0) {
        const auto idx = index(row);
        // Pass the role hint so QML delegates skip re-binding unrelated roles.
        Q_EMIT dataChanged(idx, idx, roles);
    }
}

void BluetoothAdapterModel::connectAdapter(BluetoothAdapter* adapter)
{
    // Wire each model role to exactly the adapter signal that drives it.
    connect(adapter, &BluetoothAdapter::addressChanged, this, [this, adapter]() {
        onAdapterDataChanged(adapter, {AddressRole});
    });
    connect(adapter, &BluetoothAdapter::nameChanged, this, [this, adapter]() {
        onAdapterDataChanged(adapter, {NameRole});
    });
    connect(adapter, &BluetoothAdapter::aliasChanged, this, [this, adapter]() {
        onAdapterDataChanged(adapter, {AliasRole});
    });
    connect(adapter, &BluetoothAdapter::poweredChanged, this, [this, adapter]() {
        onAdapterDataChanged(adapter, {PoweredRole});
    });
    connect(adapter, &BluetoothAdapter::discoverableChanged, this, [this, adapter]() {
        onAdapterDataChanged(adapter, {DiscoverableRole});
    });
    connect(adapter, &BluetoothAdapter::pairableChanged, this, [this, adapter]() {
        onAdapterDataChanged(adapter, {PairableRole});
    });
    connect(adapter, &BluetoothAdapter::discoveringChanged, this, [this, adapter]() {
        onAdapterDataChanged(adapter, {DiscoveringRole});
    });
}

} // namespace PhosphorServiceBluetooth
