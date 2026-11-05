// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceNetwork/phosphorservicenetwork_export.h>

#include <PhosphorServiceNetwork/NetworkDevice.h>

#include <QDBusObjectPath>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServiceNetwork {

class AccessPoint;
class NetworkConnection;

/// Root of the NetworkManager surface. Binds the system bus, exposes the
/// manager-level connectivity / radio-toggle state, and materialises one
/// NetworkDevice per `org.freedesktop.NetworkManager.Device`. Construct
/// one, hand its pointer to a NetworkDeviceModel for list views, or bind
/// the scalar properties directly for a status indicator.
class PHOSPHORSERVICENETWORK_EXPORT NetworkHost : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NetworkHost)
    Q_PROPERTY(bool networkingEnabled READ networkingEnabled NOTIFY networkingEnabledChanged)
    Q_PROPERTY(bool wirelessEnabled READ wirelessEnabled WRITE setWirelessEnabled NOTIFY wirelessEnabledChanged)
    Q_PROPERTY(Connectivity connectivity READ connectivity NOTIFY connectivityChanged)
    Q_PROPERTY(QString primaryConnectionType READ primaryConnectionType NOTIFY primaryConnectionTypeChanged)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY deviceCountChanged)

public:
    // Mirrors org.freedesktop.NetworkManager `Connectivity` (NMConnectivityState).
    // Contiguous 0..4; unrecognised raw values map to UnknownConnectivity.
    enum Connectivity {
        UnknownConnectivity = 0,
        NoConnectivity = 1,
        Portal = 2,
        Limited = 3,
        Full = 4,
    };
    Q_ENUM(Connectivity)

    explicit NetworkHost(QObject* parent = nullptr);
    ~NetworkHost() override;

    [[nodiscard]] bool networkingEnabled() const;
    [[nodiscard]] bool wirelessEnabled() const;
    /// Toggle the global Wi-Fi radio (NetworkManager `WirelessEnabled`).
    /// Issues an async Properties.Set; the cached value updates when the
    /// daemon echoes the change back via PropertiesChanged, so reading the
    /// property immediately after the call still returns the old value.
    void setWirelessEnabled(bool enabled);
    [[nodiscard]] Connectivity connectivity() const;
    [[nodiscard]] QString primaryConnectionType() const;
    [[nodiscard]] int deviceCount() const;
    [[nodiscard]] QList<NetworkDevice*> devices() const;
    [[nodiscard]] Q_INVOKABLE PhosphorServiceNetwork::NetworkDevice* deviceAt(int index) const;

    /// Trigger a Wi-Fi scan on every managed Wi-Fi device (NM
    /// `Device.Wireless.RequestScan`). Fire-and-forget; the scan refreshes
    /// each device's access-point list daemon-side, which an
    /// AccessPointModel bound to that device then surfaces (its rows
    /// update as the daemon emits AccessPointAdded/Removed). No-op when no
    /// Wi-Fi device is present.
    Q_INVOKABLE void scanWifi();

    /// Activate an existing saved connection on a device (NM
    /// ActivateConnection). Fire-and-forget; the result surfaces through
    /// the device's state and the manager's connectivity. No-op if either
    /// argument is null or the bus is unavailable.
    Q_INVOKABLE void activateConnection(PhosphorServiceNetwork::NetworkConnection* connection,
                                        PhosphorServiceNetwork::NetworkDevice* device);

    /// Create and activate a new Wi-Fi connection for an access point (NM
    /// AddAndActivateConnection). An empty `passphrase` builds an open
    /// profile; a non-empty one builds a WPA-PSK profile. Fire-and-forget.
    /// No-op if either argument is null or the bus is unavailable.
    Q_INVOKABLE void connectToAccessPoint(PhosphorServiceNetwork::NetworkDevice* device,
                                          PhosphorServiceNetwork::AccessPoint* accessPoint,
                                          const QString& passphrase = {});

Q_SIGNALS:
    void networkingEnabledChanged();
    void wirelessEnabledChanged();
    void connectivityChanged();
    void primaryConnectionTypeChanged();
    void deviceAdded(PhosphorServiceNetwork::NetworkDevice* device);
    void deviceRemoved(PhosphorServiceNetwork::NetworkDevice* device);
    void deviceCountChanged();

private Q_SLOTS:
    void _q_onPropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& invalidated);
    void _q_onDeviceAdded(const QDBusObjectPath& path);
    void _q_onDeviceRemoved(const QDBusObjectPath& path);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceNetwork
