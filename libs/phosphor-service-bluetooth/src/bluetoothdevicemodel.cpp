// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBluetooth/BluetoothDeviceModel.h>

#include <PhosphorServiceBluetooth/BluetoothAdapter.h>
#include <PhosphorServiceBluetooth/BluetoothDevice.h>
#include <PhosphorServiceBluetooth/BluetoothHost.h>

namespace PhosphorServiceBluetooth {

BluetoothDeviceModel::BluetoothDeviceModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

BluetoothDeviceModel::~BluetoothDeviceModel() = default;

BluetoothHost* BluetoothDeviceModel::host() const
{
    return m_host;
}

void BluetoothDeviceModel::setHost(BluetoothHost* host)
{
    if (m_host == host)
        return;
    if (m_host)
        disconnect(m_host, nullptr, this, nullptr);
    m_host = host;
    if (m_host) {
        connect(m_host, &BluetoothHost::deviceAdded, this, &BluetoothDeviceModel::onDeviceAdded);
        connect(m_host, &BluetoothHost::deviceRemoved, this, &BluetoothDeviceModel::onDeviceRemoved);
        // The host owns the devices; if it is destroyed while still set, every
        // m_rows entry would dangle. destroyed fires before the children are
        // deleted, so clearing here happens while the pointers are still valid.
        connect(m_host, &QObject::destroyed, this, [this]() {
            m_host = nullptr;
            rebuild();
            Q_EMIT hostChanged();
        });
    }
    rebuild();
    Q_EMIT hostChanged();
}

BluetoothAdapter* BluetoothDeviceModel::adapter() const
{
    return m_adapterFilter;
}

void BluetoothDeviceModel::setAdapter(BluetoothAdapter* adapter)
{
    if (m_adapterFilter == adapter)
        return;
    if (m_adapterFilter)
        disconnect(m_adapterFilter, &QObject::destroyed, this, nullptr);
    m_adapterFilter = adapter;
    if (m_adapterFilter) {
        // If the adapter we filter on disappears, fall back to showing every
        // device (its own devices are removed by the host cascade anyway).
        connect(m_adapterFilter, &QObject::destroyed, this, [this]() {
            setAdapter(nullptr);
        });
    }
    rebuild();
    Q_EMIT adapterChanged();
}

int BluetoothDeviceModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant BluetoothDeviceModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    auto* device = m_rows.at(index.row());
    if (!device)
        return {};
    switch (role) {
    case DeviceRole:
        return QVariant::fromValue<QObject*>(device);
    case AddressRole:
        return device->address();
    case NameRole:
        return device->name();
    case AliasRole:
        return device->alias();
    case IconRole:
        return device->icon();
    case PairedRole:
        return device->paired();
    case TrustedRole:
        return device->trusted();
    case BlockedRole:
        return device->blocked();
    case ConnectedRole:
        return device->connected();
    case RssiRole:
        return device->rssi();
    case AdapterRole:
        return device->adapter();
    case UuidsRole:
        return device->uuids();
    default:
        return {};
    }
}

QHash<int, QByteArray> BluetoothDeviceModel::roleNames() const
{
    return {
        {DeviceRole, "device"},       {AddressRole, "address"}, {NameRole, "name"},       {AliasRole, "alias"},
        {IconRole, "icon"},           {PairedRole, "paired"},   {TrustedRole, "trusted"}, {BlockedRole, "blocked"},
        {ConnectedRole, "connected"}, {RssiRole, "rssi"},       {AdapterRole, "adapter"}, {UuidsRole, "uuids"},
    };
}

void BluetoothDeviceModel::onDeviceAdded(BluetoothDevice* device)
{
    if (!device || !accepts(device) || m_rows.contains(device))
        return;
    const int row = static_cast<int>(m_rows.size());
    beginInsertRows({}, row, row);
    m_rows.append(device);
    connectDevice(device);
    endInsertRows();
    Q_EMIT countChanged();
}

void BluetoothDeviceModel::onDeviceRemoved(BluetoothDevice* device)
{
    const int row = static_cast<int>(m_rows.indexOf(device));
    if (row < 0)
        return;
    disconnect(device, nullptr, this, nullptr);
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

void BluetoothDeviceModel::onDeviceDataChanged(BluetoothDevice* device, const QList<int>& roles)
{
    const int row = static_cast<int>(m_rows.indexOf(device));
    if (row >= 0) {
        const auto idx = index(row);
        // Pass the role hint so QML delegates skip re-binding unrelated roles.
        Q_EMIT dataChanged(idx, idx, roles);
    }
}

void BluetoothDeviceModel::rebuild()
{
    const int previousCount = static_cast<int>(m_rows.size());
    beginResetModel();
    for (auto* device : std::as_const(m_rows))
        disconnect(device, nullptr, this, nullptr);
    m_rows.clear();
    if (m_host) {
        const auto all = m_host->devices();
        for (auto* device : all) {
            if (accepts(device)) {
                m_rows.append(device);
                connectDevice(device);
            }
        }
    }
    endResetModel();
    if (previousCount != m_rows.size())
        Q_EMIT countChanged();
}

void BluetoothDeviceModel::connectDevice(BluetoothDevice* device)
{
    // Wire each model role to exactly the device signal that drives it.
    connect(device, &BluetoothDevice::addressChanged, this, [this, device]() {
        onDeviceDataChanged(device, {AddressRole});
    });
    connect(device, &BluetoothDevice::nameChanged, this, [this, device]() {
        onDeviceDataChanged(device, {NameRole});
    });
    connect(device, &BluetoothDevice::aliasChanged, this, [this, device]() {
        onDeviceDataChanged(device, {AliasRole});
    });
    connect(device, &BluetoothDevice::iconChanged, this, [this, device]() {
        onDeviceDataChanged(device, {IconRole});
    });
    connect(device, &BluetoothDevice::pairedChanged, this, [this, device]() {
        onDeviceDataChanged(device, {PairedRole});
    });
    connect(device, &BluetoothDevice::trustedChanged, this, [this, device]() {
        onDeviceDataChanged(device, {TrustedRole});
    });
    connect(device, &BluetoothDevice::blockedChanged, this, [this, device]() {
        onDeviceDataChanged(device, {BlockedRole});
    });
    connect(device, &BluetoothDevice::connectedChanged, this, [this, device]() {
        onDeviceDataChanged(device, {ConnectedRole});
    });
    connect(device, &BluetoothDevice::rssiChanged, this, [this, device]() {
        onDeviceDataChanged(device, {RssiRole});
    });
    // A device's adapter is fixed for its lifetime: BlueZ binds each Device1
    // object to the adapter under whose path it was created and never reparents
    // it (the same physical device seen by another adapter is a distinct
    // object), so this only ever refreshes the displayed role, never changes
    // filter membership. accepts() is therefore evaluated once at insertion.
    connect(device, &BluetoothDevice::adapterChanged, this, [this, device]() {
        onDeviceDataChanged(device, {AdapterRole});
    });
    connect(device, &BluetoothDevice::uuidsChanged, this, [this, device]() {
        onDeviceDataChanged(device, {UuidsRole});
    });
}

bool BluetoothDeviceModel::accepts(BluetoothDevice* device) const
{
    if (!device)
        return false;
    if (!m_adapterFilter)
        return true;
    return device->adapter() == m_adapterFilter->dbusPath();
}

} // namespace PhosphorServiceBluetooth
