// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBrightness/BrightnessDeviceModel.h>

#include <PhosphorServiceBrightness/BrightnessDevice.h>
#include <PhosphorServiceBrightness/BrightnessHost.h>

namespace PhosphorServiceBrightness {

BrightnessDeviceModel::BrightnessDeviceModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

BrightnessDeviceModel::~BrightnessDeviceModel() = default;

BrightnessHost* BrightnessDeviceModel::host() const
{
    return m_host;
}

void BrightnessDeviceModel::setHost(BrightnessHost* host)
{
    if (m_host == host)
        return;
    const int previousCount = static_cast<int>(m_rows.size());
    beginResetModel();
    if (m_host) {
        disconnect(m_host, nullptr, this, nullptr);
        // The old devices belong to the old host (which lives on), so their
        // connections to this model would otherwise leak.
        for (auto* device : std::as_const(m_rows))
            disconnect(device, nullptr, this, nullptr);
    }
    m_host = host;
    m_rows.clear();
    if (m_host) {
        m_rows = m_host->devices();
        for (auto* device : std::as_const(m_rows))
            connectDevice(device);
        connect(m_host, &BrightnessHost::deviceAdded, this, &BrightnessDeviceModel::onDeviceAdded);
        connect(m_host, &BrightnessHost::deviceRemoved, this, &BrightnessDeviceModel::onDeviceRemoved);
        // The host owns the devices; if it is destroyed while still set, m_host
        // and every m_rows entry would dangle. Drop them all.
        connect(m_host, &QObject::destroyed, this, [this]() {
            const int prev = static_cast<int>(m_rows.size());
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
    if (previousCount != static_cast<int>(m_rows.size()))
        Q_EMIT countChanged();
}

int BrightnessDeviceModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant BrightnessDeviceModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    auto* device = m_rows.at(index.row());
    if (!device)
        return {};
    switch (role) {
    case DeviceRole:
        return QVariant::fromValue<QObject*>(device);
    case IdRole:
        return device->id();
    case NameRole:
        return device->name();
    case KindRole:
        return static_cast<int>(device->kind());
    case BrightnessRole:
        return device->brightness();
    case MaxBrightnessRole:
        return device->maxBrightness();
    case PercentageRole:
        return device->percentage();
    default:
        return {};
    }
}

QHash<int, QByteArray> BrightnessDeviceModel::roleNames() const
{
    return {
        {DeviceRole, "device"},
        {IdRole, "id"},
        {NameRole, "name"},
        {KindRole, "kind"},
        {BrightnessRole, "brightness"},
        {MaxBrightnessRole, "maxBrightness"},
        {PercentageRole, "percentage"},
    };
}

void BrightnessDeviceModel::onDeviceAdded(BrightnessDevice* device)
{
    if (!device || m_rows.contains(device))
        return;
    const int row = static_cast<int>(m_rows.size());
    beginInsertRows({}, row, row);
    m_rows.append(device);
    connectDevice(device);
    endInsertRows();
    Q_EMIT countChanged();
}

void BrightnessDeviceModel::onDeviceRemoved(BrightnessDevice* device)
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

void BrightnessDeviceModel::onDeviceBrightnessChanged(BrightnessDevice* device)
{
    const int row = static_cast<int>(m_rows.indexOf(device));
    if (row >= 0) {
        const auto idx = index(row);
        // brightness and percentage both derive from the brightness value.
        Q_EMIT dataChanged(idx, idx, {BrightnessRole, PercentageRole});
    }
}

void BrightnessDeviceModel::connectDevice(BrightnessDevice* device)
{
    connect(device, &BrightnessDevice::brightnessChanged, this, [this, device]() {
        onDeviceBrightnessChanged(device);
    });
}

} // namespace PhosphorServiceBrightness
