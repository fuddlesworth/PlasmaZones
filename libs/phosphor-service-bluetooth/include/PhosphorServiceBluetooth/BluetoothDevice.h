// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceBluetooth/phosphorservicebluetooth_export.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>

class QDBusConnection;

namespace PhosphorServiceBluetooth {

/**
 * @brief One BlueZ device (`org.bluez.Device1`), e.g.
 * `/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF`.
 *
 * Initial properties are supplied by `BluetoothHost` from the ObjectManager
 * enumeration; subsequent changes arrive via `PropertiesChanged`. The write
 * surface is exposed via connectDevice / disconnectDevice / setTrusted /
 * setBlocked / pair / cancelPairing (fire-and-forget; cached state moves on
 * the PropertiesChanged echo).
 *
 * `rssi` is only reported by BlueZ for devices currently in range (during
 * discovery): it is 0 for a never-seen / cached device, and is reset to 0
 * when BlueZ invalidates it as the device goes out of range. Owned by
 * `BluetoothHost`; never constructed from QML.
 */
class PHOSPHORSERVICEBLUETOOTH_EXPORT BluetoothDevice : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString dbusPath READ dbusPath CONSTANT)
    Q_PROPERTY(QString address READ address NOTIFY addressChanged)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(QString alias READ alias NOTIFY aliasChanged)
    Q_PROPERTY(QString icon READ icon NOTIFY iconChanged)
    Q_PROPERTY(bool paired READ paired NOTIFY pairedChanged)
    Q_PROPERTY(bool trusted READ trusted NOTIFY trustedChanged)
    Q_PROPERTY(bool blocked READ blocked NOTIFY blockedChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(int rssi READ rssi NOTIFY rssiChanged)
    Q_PROPERTY(QString adapter READ adapter NOTIFY adapterChanged)
    Q_PROPERTY(QStringList uuids READ uuids NOTIFY uuidsChanged)

public:
    BluetoothDevice(QDBusConnection connection, const QString& dbusPath, const QVariantMap& initialProperties,
                    QObject* parent = nullptr);
    ~BluetoothDevice() override;

    [[nodiscard]] QString dbusPath() const;
    [[nodiscard]] QString address() const;
    [[nodiscard]] QString name() const;
    [[nodiscard]] QString alias() const;
    [[nodiscard]] QString icon() const;
    [[nodiscard]] bool paired() const;
    [[nodiscard]] bool trusted() const;
    [[nodiscard]] bool blocked() const;
    [[nodiscard]] bool connected() const;
    [[nodiscard]] int rssi() const;
    [[nodiscard]] QString adapter() const;
    [[nodiscard]] QStringList uuids() const;

    /// Establish / tear down a connection to this device
    /// (`org.bluez.Device1.Connect` / `Disconnect`). Fire-and-forget;
    /// `connected` reflects the outcome via PropertiesChanged. Named
    /// `*Device` to avoid shadowing the static `QObject::connect` /
    /// `disconnect`.
    Q_INVOKABLE void connectDevice();
    Q_INVOKABLE void disconnectDevice();

    /// Mark the device (un)trusted / (un)blocked (`org.bluez.Device1`
    /// Properties.Set). Fire-and-forget; the cached value moves only on the
    /// PropertiesChanged echo, never optimistically.
    Q_INVOKABLE void setTrusted(bool trusted);
    Q_INVOKABLE void setBlocked(bool blocked);

    /// Begin / abort pairing (`org.bluez.Device1.Pair` / `CancelPairing`).
    /// Fire-and-forget; during Pair() BlueZ drives the registered
    /// BluetoothAgent's callbacks, and `paired` reflects the outcome via
    /// PropertiesChanged.
    Q_INVOKABLE void pair();
    Q_INVOKABLE void cancelPairing();

Q_SIGNALS:
    void addressChanged();
    void nameChanged();
    void aliasChanged();
    void iconChanged();
    void pairedChanged();
    void trustedChanged();
    void blockedChanged();
    void connectedChanged();
    void rssiChanged();
    void adapterChanged();
    void uuidsChanged();

private Q_SLOTS:
    void _q_onPropertiesChanged(const QString& interfaceName, const QVariantMap& changed,
                                const QStringList& invalidated);

private:
    Q_DISABLE_COPY_MOVE(BluetoothDevice)
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceBluetooth
