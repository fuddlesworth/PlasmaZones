// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/UPowerDeviceModel.h>
#include <PhosphorServices/UPowerDevice.h>
#include <PhosphorServices/UPowerHost.h>

namespace PhosphorServices {

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
    beginResetModel();
    if (m_host)
        disconnect(m_host, nullptr, this, nullptr);
    m_host = host;
    if (m_host) {
        for (auto* device : m_host->devices())
            connectDevice(device);
        connect(m_host, &UPowerHost::deviceAdded, this, &UPowerDeviceModel::onDeviceAdded);
        connect(m_host, &UPowerHost::deviceRemoved, this, &UPowerDeviceModel::onDeviceRemoved);
    }
    endResetModel();
    Q_EMIT hostChanged();
    Q_EMIT countChanged();
}

int UPowerDeviceModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_host ? m_host->deviceCount() : 0;
}

QVariant UPowerDeviceModel::data(const QModelIndex& index, int role) const
{
    if (!m_host || !index.isValid() || index.row() >= m_host->deviceCount())
        return {};
    auto* device = m_host->deviceAt(index.row());
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
    if (!m_host)
        return;
    const int row = m_host->deviceCount() - 1;
    beginInsertRows({}, row, row);
    connectDevice(device);
    endInsertRows();
    Q_EMIT countChanged();
}

void UPowerDeviceModel::onDeviceRemoved(UPowerDevice* device)
{
    if (!m_host)
        return;
    const auto& devices = m_host->devices();
    int row = -1;
    for (int i = 0; i < devices.size(); ++i) {
        if (devices.at(i) == device) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        beginResetModel();
        endResetModel();
    } else {
        beginRemoveRows({}, row, row);
        endRemoveRows();
    }
    Q_EMIT countChanged();
}

void UPowerDeviceModel::onDeviceDataChanged(UPowerDevice* device)
{
    if (!m_host)
        return;
    const auto& devices = m_host->devices();
    for (int i = 0; i < devices.size(); ++i) {
        if (devices.at(i) == device) {
            Q_EMIT dataChanged(index(i), index(i));
            return;
        }
    }
}

void UPowerDeviceModel::connectDevice(UPowerDevice* device)
{
    connect(device, &UPowerDevice::propertiesRefreshed, this, [this, device]() {
        onDeviceDataChanged(device);
    });
}

} // namespace PhosphorServices
