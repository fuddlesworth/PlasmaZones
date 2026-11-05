// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNetwork/NetworkDeviceModel.h>
#include <PhosphorServiceNetwork/NetworkDevice.h>
#include <PhosphorServiceNetwork/NetworkHost.h>

namespace PhosphorServiceNetwork {

NetworkDeviceModel::NetworkDeviceModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

NetworkDeviceModel::~NetworkDeviceModel() = default;

NetworkHost* NetworkDeviceModel::host() const
{
    return m_host;
}

void NetworkDeviceModel::setHost(NetworkHost* host)
{
    if (m_host == host)
        return;
    const int previousCount = m_rows.size();
    beginResetModel();
    if (m_host) {
        disconnect(m_host, nullptr, this, nullptr);
        // The old devices belong to the old host (which lives on), so
        // their connections to this model would otherwise leak.
        for (auto* device : std::as_const(m_rows))
            disconnect(device, nullptr, this, nullptr);
    }
    m_host = host;
    m_rows.clear();
    if (m_host) {
        m_rows = m_host->devices();
        for (auto* device : std::as_const(m_rows))
            connectDevice(device);
        connect(m_host, &NetworkHost::deviceAdded, this, &NetworkDeviceModel::onDeviceAdded);
        connect(m_host, &NetworkHost::deviceRemoved, this, &NetworkDeviceModel::onDeviceRemoved);
        // The host owns the devices; if it is destroyed while still set,
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
    // Only emit countChanged when the row count actually moved. A 0 -> 0
    // attach (CI has no NetworkManager daemon) or detach stays silent per
    // CLAUDE.md "only emit on change".
    if (previousCount != m_rows.size())
        Q_EMIT countChanged();
}

int NetworkDeviceModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant NetworkDeviceModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    auto* device = m_rows.at(index.row());
    if (!device)
        return {};
    switch (role) {
    case DeviceRole:
        return QVariant::fromValue<QObject*>(device);
    case InterfaceNameRole:
        return device->interfaceName();
    case DeviceTypeRole:
        return static_cast<int>(device->deviceType());
    case StateRole:
        return static_cast<int>(device->state());
    case ManagedRole:
        return device->managed();
    default:
        return {};
    }
}

QHash<int, QByteArray> NetworkDeviceModel::roleNames() const
{
    return {
        {DeviceRole, "device"},         {InterfaceNameRole, "interfaceName"},
        {DeviceTypeRole, "deviceType"}, {StateRole, "deviceState"},
        {ManagedRole, "managed"},
    };
}

void NetworkDeviceModel::onDeviceAdded(NetworkDevice* device)
{
    if (!device || m_rows.contains(device))
        return;
    const int row = m_rows.size();
    beginInsertRows({}, row, row);
    m_rows.append(device);
    connectDevice(device);
    endInsertRows();
    Q_EMIT countChanged();
}

void NetworkDeviceModel::onDeviceRemoved(NetworkDevice* device)
{
    const int row = m_rows.indexOf(device);
    if (row < 0)
        return;
    disconnect(device, nullptr, this, nullptr);
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

void NetworkDeviceModel::onDeviceDataChanged(NetworkDevice* device, const QList<int>& roles)
{
    const int row = m_rows.indexOf(device);
    if (row >= 0) {
        const auto idx = index(row);
        // Pass the role hint so QML delegates skip re-binding unrelated
        // roles.
        Q_EMIT dataChanged(idx, idx, roles);
    }
}

void NetworkDeviceModel::connectDevice(NetworkDevice* device)
{
    // Wire each model role to exactly the device signal that drives it, so
    // an interface-name update doesn't force a state re-bind and vice versa.
    connect(device, &NetworkDevice::interfaceNameChanged, this, [this, device]() {
        onDeviceDataChanged(device, {InterfaceNameRole});
    });
    connect(device, &NetworkDevice::deviceTypeChanged, this, [this, device]() {
        onDeviceDataChanged(device, {DeviceTypeRole});
    });
    connect(device, &NetworkDevice::stateChanged, this, [this, device]() {
        onDeviceDataChanged(device, {StateRole});
    });
    connect(device, &NetworkDevice::managedChanged, this, [this, device]() {
        onDeviceDataChanged(device, {ManagedRole});
    });
}

} // namespace PhosphorServiceNetwork
