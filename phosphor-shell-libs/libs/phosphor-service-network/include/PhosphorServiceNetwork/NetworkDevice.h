// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceNetwork/phosphorservicenetwork_export.h>

#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServiceNetwork {

/// One `org.freedesktop.NetworkManager.Device` object. Owned by
/// NetworkHost (parented to it), vended to consumers via the host's
/// deviceAdded / deviceRemoved signals. Reads its interface name, type,
/// state, and managed flag asynchronously via Properties.GetAll and
/// keeps them live through a PropertiesChanged subscription.
class PHOSPHORSERVICENETWORK_EXPORT NetworkDevice : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NetworkDevice)

    Q_PROPERTY(QString interfaceName READ interfaceName NOTIFY interfaceNameChanged)
    Q_PROPERTY(DeviceType deviceType READ deviceType NOTIFY deviceTypeChanged)
    Q_PROPERTY(DeviceState state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool managed READ managed NOTIFY managedChanged)

public:
    // Mirrors org.freedesktop.NetworkManager.Device `DeviceType` (NM 1.0+,
    // see NetworkManager's NMDeviceType). Values are wire constants; the
    // enum is sparse (3 and 4 are unused upstream, and NM's OVS/WPAN/6LoWPAN
    // types at 24-28 are intentionally not modelled, so the table ends at
    // Wireguard=29). Unrecognised raw values map to Unknown rather than
    // casting to an enumerator we don't declare.
    enum DeviceType {
        Unknown = 0,
        Ethernet = 1,
        Wifi = 2,
        Bluetooth = 5,
        OlpcMesh = 6,
        Wimax = 7,
        Modem = 8,
        Infiniband = 9,
        Bond = 10,
        Vlan = 11,
        Adsl = 12,
        Bridge = 13,
        Generic = 14,
        Team = 15,
        Tun = 16,
        IpTunnel = 17,
        Macvlan = 18,
        Vxlan = 19,
        Veth = 20,
        Macsec = 21,
        Dummy = 22,
        Ppp = 23,
        Wireguard = 29,
    };
    Q_ENUM(DeviceType)

    // Mirrors org.freedesktop.NetworkManager.Device `State` (NMDeviceState).
    // Values are sparse (multiples of ten); unrecognised raw values map to
    // UnknownState.
    enum DeviceState {
        UnknownState = 0,
        Unmanaged = 10,
        Unavailable = 20,
        Disconnected = 30,
        Prepare = 40,
        Config = 50,
        NeedAuth = 60,
        IpConfig = 70,
        IpCheck = 80,
        Secondaries = 90,
        Activated = 100,
        Deactivating = 110,
        Failed = 120,
    };
    Q_ENUM(DeviceState)

    explicit NetworkDevice(const QString& dbusPath, QObject* parent = nullptr);
    ~NetworkDevice() override;

    [[nodiscard]] QString dbusPath() const;
    [[nodiscard]] QString interfaceName() const;
    [[nodiscard]] DeviceType deviceType() const;
    [[nodiscard]] DeviceState state() const;
    [[nodiscard]] bool managed() const;

Q_SIGNALS:
    void interfaceNameChanged();
    void deviceTypeChanged();
    void stateChanged();
    void managedChanged();

private Q_SLOTS:
    void _q_onPropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& invalidated);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceNetwork
