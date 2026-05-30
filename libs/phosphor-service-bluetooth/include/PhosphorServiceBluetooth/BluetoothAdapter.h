// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceBluetooth/phosphorservicebluetooth_export.h>

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <memory>

class QDBusConnection;

namespace PhosphorServiceBluetooth {

/**
 * @brief One BlueZ adapter (`org.bluez.Adapter1`), e.g. `/org/bluez/hci0`.
 *
 * Initial properties are supplied by `BluetoothHost` from the ObjectManager
 * enumeration (no construction-time round-trip); subsequent changes arrive via
 * `PropertiesChanged`. The power / discoverable / discovery write surface is
 * exposed via setPowered / setDiscoverable / startDiscovery / stopDiscovery /
 * removeDevice (fire-and-forget; cached state moves on the PropertiesChanged
 * echo).
 *
 * Owned by `BluetoothHost`; never constructed from QML.
 */
class PHOSPHORSERVICEBLUETOOTH_EXPORT BluetoothAdapter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString dbusPath READ dbusPath CONSTANT)
    Q_PROPERTY(QString address READ address NOTIFY addressChanged)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(QString alias READ alias NOTIFY aliasChanged)
    Q_PROPERTY(bool powered READ powered NOTIFY poweredChanged)
    Q_PROPERTY(bool discoverable READ discoverable NOTIFY discoverableChanged)
    Q_PROPERTY(bool pairable READ pairable NOTIFY pairableChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY discoveringChanged)

public:
    BluetoothAdapter(QDBusConnection connection, const QString& dbusPath, const QVariantMap& initialProperties,
                     QObject* parent = nullptr);
    ~BluetoothAdapter() override;

    [[nodiscard]] QString dbusPath() const;
    [[nodiscard]] QString address() const;
    [[nodiscard]] QString name() const;
    [[nodiscard]] QString alias() const;
    [[nodiscard]] bool powered() const;
    [[nodiscard]] bool discoverable() const;
    [[nodiscard]] bool pairable() const;
    [[nodiscard]] bool discovering() const;

    /// Power the adapter on/off (`org.bluez.Adapter1.Powered`). Fire-and-
    /// forget: the cached `powered` updates when BlueZ echoes
    /// PropertiesChanged, never optimistically.
    Q_INVOKABLE void setPowered(bool powered);

    /// Toggle inbound discoverability (`org.bluez.Adapter1.Discoverable`).
    /// Fire-and-forget, same echo semantics as setPowered.
    Q_INVOKABLE void setDiscoverable(bool discoverable);

    /// Begin/end scanning for nearby devices (`StartDiscovery` /
    /// `StopDiscovery`). `discovering` reflects the live state via
    /// PropertiesChanged; new devices surface through BluetoothHost.
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void stopDiscovery();

    /// Forget a device under this adapter (`org.bluez.Adapter1.RemoveDevice`),
    /// dropping its pairing/cache. Fire-and-forget; the device disappears from
    /// BluetoothHost via InterfacesRemoved.
    Q_INVOKABLE void removeDevice(const QString& devicePath);

Q_SIGNALS:
    void addressChanged();
    void nameChanged();
    void aliasChanged();
    void poweredChanged();
    void discoverableChanged();
    void pairableChanged();
    void discoveringChanged();

private Q_SLOTS:
    void _q_onPropertiesChanged(const QString& interfaceName, const QVariantMap& changed,
                                const QStringList& invalidated);

private:
    Q_DISABLE_COPY_MOVE(BluetoothAdapter)
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceBluetooth
