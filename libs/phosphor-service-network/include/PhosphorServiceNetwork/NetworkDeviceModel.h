// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceNetwork/phosphorservicenetwork_export.h>

#include <PhosphorServiceNetwork/NetworkDevice.h>
#include <PhosphorServiceNetwork/NetworkHost.h>

#include <QAbstractListModel>

namespace PhosphorServiceNetwork {

/// List model over a NetworkHost's devices. Bind `host`, then drive a
/// Repeater/ListView off the rows. The model owns a local row mirror so
/// its begin/end-insert/remove transactions always straddle the actual
/// mutation regardless of when the host emits deviceAdded/deviceRemoved.
class PHOSPHORSERVICENETWORK_EXPORT NetworkDeviceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NetworkDeviceModel)
    Q_PROPERTY(PhosphorServiceNetwork::NetworkHost* host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        DeviceRole = Qt::UserRole + 1,
        InterfaceNameRole,
        DeviceTypeRole,
        StateRole,
        ManagedRole,
    };
    Q_ENUM(Roles)

    explicit NetworkDeviceModel(QObject* parent = nullptr);
    ~NetworkDeviceModel() override;

    [[nodiscard]] NetworkHost* host() const;
    void setHost(NetworkHost* host);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void hostChanged();
    void countChanged();

private Q_SLOTS:
    void onDeviceAdded(PhosphorServiceNetwork::NetworkDevice* device);
    void onDeviceRemoved(PhosphorServiceNetwork::NetworkDevice* device);
    void onDeviceDataChanged(PhosphorServiceNetwork::NetworkDevice* device, const QList<int>& roles);

private:
    void connectDevice(NetworkDevice* device);

    NetworkHost* m_host = nullptr;
    // Row mirror of host-owned NetworkDevice pointers. rowCount and data
    // index into this list, never the host's, so the transaction
    // boundaries always straddle the actual mutation. The model does NOT
    // own these objects and never deletes them.
    //
    // Mirror-safety invariant (relied on instead of a per-device
    // destroyed watch): NetworkHost only ever destroys a device AFTER
    // emitting deviceRemoved (see NetworkHost::Private::removeDevice,
    // which removes the row via onDeviceRemoved before deleteLater), and
    // on host teardown QObject::destroyed fires before the host's device
    // children are deleted (so the host-destroyed lambda clears m_rows
    // while every pointer is still valid). A device pointer can therefore
    // never dangle in m_rows.
    QList<NetworkDevice*> m_rows;
};

} // namespace PhosphorServiceNetwork
