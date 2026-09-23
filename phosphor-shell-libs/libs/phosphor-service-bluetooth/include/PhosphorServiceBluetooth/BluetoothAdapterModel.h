// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceBluetooth/phosphorservicebluetooth_export.h>

#include <PhosphorServiceBluetooth/BluetoothAdapter.h>
#include <PhosphorServiceBluetooth/BluetoothHost.h>

#include <QAbstractListModel>

namespace PhosphorServiceBluetooth {

/// List model over a BluetoothHost's adapters. Bind `host`, then drive a
/// Repeater/ListView off the rows. The model keeps a local row mirror so its
/// begin/end-insert/remove transactions always straddle the actual mutation
/// regardless of when the host emits adapterAdded/adapterRemoved.
class PHOSPHORSERVICEBLUETOOTH_EXPORT BluetoothAdapterModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(BluetoothAdapterModel)
    Q_PROPERTY(PhosphorServiceBluetooth::BluetoothHost* host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        AdapterRole = Qt::UserRole + 1,
        AddressRole,
        NameRole,
        AliasRole,
        PoweredRole,
        DiscoverableRole,
        PairableRole,
        DiscoveringRole,
    };
    Q_ENUM(Roles)

    explicit BluetoothAdapterModel(QObject* parent = nullptr);
    ~BluetoothAdapterModel() override;

    [[nodiscard]] BluetoothHost* host() const;
    void setHost(BluetoothHost* host);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void hostChanged();
    void countChanged();

private Q_SLOTS:
    void onAdapterAdded(PhosphorServiceBluetooth::BluetoothAdapter* adapter);
    void onAdapterRemoved(PhosphorServiceBluetooth::BluetoothAdapter* adapter);
    void onAdapterDataChanged(PhosphorServiceBluetooth::BluetoothAdapter* adapter, const QList<int>& roles);

private:
    void connectAdapter(BluetoothAdapter* adapter);

    BluetoothHost* m_host = nullptr;
    // Row mirror of host-owned BluetoothAdapter pointers; rowCount/data index
    // into this list. The model does NOT own these objects and never deletes
    // them. Safe against dangling because BluetoothHost emits adapterRemoved
    // before destroying an adapter, and QObject::destroyed fires before the
    // host's children are deleted (the host-destroyed lambda clears m_rows
    // while every pointer is still valid).
    QList<BluetoothAdapter*> m_rows;
};

} // namespace PhosphorServiceBluetooth
