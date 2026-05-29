// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceUPower/UPowerDeviceModel.h>
#include <PhosphorServiceUPower/UPowerDevice.h>
#include <PhosphorServiceUPower/UPowerHost.h>

namespace PhosphorServiceUPower {

UPowerDeviceModel::UPowerDeviceModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

UPowerDeviceModel::~UPowerDeviceModel() = default;

UPowerHost* UPowerDeviceModel::host() const
{
    return m_host;
}

void UPowerDeviceModel::setHost(UPowerHost* host)
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
        connect(m_host, &UPowerHost::deviceAdded, this, &UPowerDeviceModel::onDeviceAdded);
        connect(m_host, &UPowerHost::deviceRemoved, this, &UPowerDeviceModel::onDeviceRemoved);
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
    // Only emit countChanged when the row count actually moved. A 0
    // -> 0 attach (CI has no UPower daemon) or detach should stay
    // silent per CLAUDE.md "only emit on change".
    if (previousCount != m_rows.size())
        Q_EMIT countChanged();
}

int UPowerDeviceModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant UPowerDeviceModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    auto* device = m_rows.at(index.row());
    if (!device)
        return {};
    switch (role) {
    case DeviceRole:
        return QVariant::fromValue<QObject*>(device);
    case PercentageRole:
        return device->percentage();
    case StateRole:
        return static_cast<int>(device->state());
    case TypeRole:
        return static_cast<int>(device->type());
    case IconNameRole:
        return device->iconName();
    case IsLaptopBatteryRole:
        return device->isLaptopBattery();
    default:
        return {};
    }
}

QHash<int, QByteArray> UPowerDeviceModel::roleNames() const
{
    return {
        {DeviceRole, "device"},   {PercentageRole, "percentage"}, {StateRole, "deviceState"},
        {TypeRole, "deviceType"}, {IconNameRole, "iconName"},     {IsLaptopBatteryRole, "isLaptopBattery"},
    };
}

void UPowerDeviceModel::onDeviceAdded(UPowerDevice* device)
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

void UPowerDeviceModel::onDeviceRemoved(UPowerDevice* device)
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

void UPowerDeviceModel::onDeviceDataChanged(UPowerDevice* device)
{
    const int row = m_rows.indexOf(device);
    if (row >= 0)
        Q_EMIT dataChanged(index(row), index(row));
}

void UPowerDeviceModel::connectDevice(UPowerDevice* device)
{
    connect(device, &UPowerDevice::propertiesRefreshed, this, [this, device]() {
        onDeviceDataChanged(device);
    });
}

} // namespace PhosphorServiceUPower
