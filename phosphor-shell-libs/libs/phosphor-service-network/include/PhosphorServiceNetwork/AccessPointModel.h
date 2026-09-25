// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceNetwork/phosphorservicenetwork_export.h>

#include <PhosphorServiceNetwork/AccessPoint.h>
#include <PhosphorServiceNetwork/NetworkDevice.h>

#include <QAbstractListModel>
#include <QDBusConnection>
#include <QDBusObjectPath>

namespace PhosphorServiceNetwork {

/// List model over the access points a single Wi-Fi `NetworkDevice` last
/// scanned. Bind `device` to a Wi-Fi device, call NetworkHost::scanWifi()
/// to refresh, and drive a list view off the rows. The model owns the
/// AccessPoint row objects (parented to it) and keeps them live via the
/// device's wireless-interface AccessPointAdded / AccessPointRemoved
/// signals. Rows are in NetworkManager's enumeration order; sort by
/// `strength` in a proxy/delegate if a signal-ordered list is wanted.
class PHOSPHORSERVICENETWORK_EXPORT AccessPointModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AccessPointModel)
    Q_PROPERTY(PhosphorServiceNetwork::NetworkDevice* device READ device WRITE setDevice NOTIFY deviceChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        AccessPointRole = Qt::UserRole + 1,
        SsidRole,
        StrengthRole,
        FrequencyRole,
        BssidRole,
        SecurityRole,
        SecuredRole,
    };
    Q_ENUM(Roles)

    explicit AccessPointModel(QObject* parent = nullptr);
    ~AccessPointModel() override;

    [[nodiscard]] NetworkDevice* device() const;
    void setDevice(NetworkDevice* device);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void deviceChanged();
    void countChanged();

private Q_SLOTS:
    void _q_onAccessPointAdded(const QDBusObjectPath& path);
    void _q_onAccessPointRemoved(const QDBusObjectPath& path);

private:
    void subscribe();
    void unsubscribe();
    void rebuild();
    void clearRows();
    void addAccessPoint(const QString& path);
    void removeAccessPoint(const QString& path);
    void connectAccessPoint(AccessPoint* ap);

    NetworkDevice* m_device = nullptr;
    // The device path the AccessPointAdded/Removed match rules were
    // registered against. Cached so unsubscribe() never has to touch
    // m_device — which is unsafe from the device-destroyed handler, where
    // QObject::destroyed fires after NetworkDevice's pimpl is already gone.
    QString m_subscribedPath;
    QDBusConnection m_bus = QDBusConnection::systemBus();
    // Row objects owned by the model (parented to it). data()/rowCount
    // index into this list.
    QList<AccessPoint*> m_rows;
};

} // namespace PhosphorServiceNetwork
