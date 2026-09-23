// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceBluetooth/phosphorservicebluetooth_export.h>

#include <PhosphorServiceBluetooth/BluetoothAdapter.h>
#include <PhosphorServiceBluetooth/BluetoothDevice.h>
#include <PhosphorServiceBluetooth/BluetoothHost.h>

#include <QAbstractListModel>

namespace PhosphorServiceBluetooth {

/// List model over a BluetoothHost's devices, optionally scoped to a single
/// adapter. Bind `host`, optionally set `adapter` to show only that adapter's
/// devices (leave null for all), then drive a Repeater/ListView off the rows.
/// The model keeps a local row mirror so its begin/end-insert/remove
/// transactions always straddle the actual mutation.
class PHOSPHORSERVICEBLUETOOTH_EXPORT BluetoothDeviceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(BluetoothDeviceModel)
    Q_PROPERTY(PhosphorServiceBluetooth::BluetoothHost* host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(PhosphorServiceBluetooth::BluetoothAdapter* adapter READ adapter WRITE setAdapter NOTIFY adapterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        DeviceRole = Qt::UserRole + 1,
        AddressRole,
        NameRole,
        AliasRole,
        IconRole,
        PairedRole,
        TrustedRole,
        BlockedRole,
        ConnectedRole,
        RssiRole,
        AdapterRole,
        UuidsRole,
    };
    Q_ENUM(Roles)

    explicit BluetoothDeviceModel(QObject* parent = nullptr);
    ~BluetoothDeviceModel() override;

    [[nodiscard]] BluetoothHost* host() const;
    void setHost(BluetoothHost* host);

    /// Adapter filter. Null shows every device across all adapters; non-null
    /// shows only devices whose `adapter` path matches it.
    [[nodiscard]] BluetoothAdapter* adapter() const;
    void setAdapter(BluetoothAdapter* adapter);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void hostChanged();
    void adapterChanged();
    void countChanged();

private Q_SLOTS:
    void onDeviceAdded(PhosphorServiceBluetooth::BluetoothDevice* device);
    void onDeviceRemoved(PhosphorServiceBluetooth::BluetoothDevice* device);
    void onDeviceDataChanged(PhosphorServiceBluetooth::BluetoothDevice* device, const QList<int>& roles);

private:
    void rebuild();
    void connectDevice(BluetoothDevice* device);
    [[nodiscard]] bool accepts(BluetoothDevice* device) const;

    BluetoothHost* m_host = nullptr;
    BluetoothAdapter* m_adapterFilter = nullptr;
    // Row mirror of host-owned BluetoothDevice pointers (post-filter); the
    // model never owns or deletes these. Dangling-safe for the same reason as
    // BluetoothAdapterModel: the host removes a device from its list and emits
    // deviceRemoved before deleteLater, and clears on host destruction.
    QList<BluetoothDevice*> m_rows;
};

} // namespace PhosphorServiceBluetooth
