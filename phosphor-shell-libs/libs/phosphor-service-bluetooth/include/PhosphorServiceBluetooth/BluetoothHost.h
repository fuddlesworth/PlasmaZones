// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceBluetooth/phosphorservicebluetooth_export.h>

#include <QList>
#include <QObject>
#include <QString>

#include <memory>

class QDBusConnection;

namespace PhosphorServiceBluetooth {

class BluetoothAdapter;
class BluetoothAgent;
class BluetoothDevice;

/**
 * @brief Facade over `org.bluez`: enumerates adapters and devices.
 *
 * Built on `PhosphorDBus::ObjectManager` pointed at the BlueZ root (`/`).
 * Adapters (`org.bluez.Adapter1`) and devices (`org.bluez.Device1`) are
 * materialised as typed children, parented to the host and vended through
 * the `*Added` / `*Removed` signals. When an adapter goes away its devices
 * are dropped too (cascade), independent of BlueZ's own per-device removals.
 *
 * Inert when the bus is unavailable (no daemon → empty lists, no crash).
 * The discovery / pairing / connect write surface lives on the adapter and
 * device objects; the pairing agent is exposed via agent().
 */
class PHOSPHORSERVICEBLUETOOTH_EXPORT BluetoothHost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int adapterCount READ adapterCount NOTIFY adapterCountChanged)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY deviceCountChanged)
    Q_PROPERTY(PhosphorServiceBluetooth::BluetoothAgent* agent READ agent CONSTANT)

public:
    /// Observes the system bus's `org.bluez` (the production wiring).
    explicit BluetoothHost(QObject* parent = nullptr);

    /// Observes @p service on @p connection. Lets a consumer (or a test
    /// harness) point the host at a non-default bus / service name.
    BluetoothHost(QDBusConnection connection, QString service, QObject* parent = nullptr);

    ~BluetoothHost() override;

    [[nodiscard]] QList<BluetoothAdapter*> adapters() const;
    [[nodiscard]] QList<BluetoothDevice*> devices() const;
    [[nodiscard]] int adapterCount() const;
    [[nodiscard]] int deviceCount() const;

    /// The pairing agent registered with BlueZ (KeyboardDisplay capability).
    /// Connect to its request signals and answer via its respond* slots to
    /// drive an interactive pairing flow. Null when the bus is unavailable or
    /// the agent object could not be exported (it would be unusable either
    /// way). Owned by the host.
    [[nodiscard]] BluetoothAgent* agent() const;

    /// Adapter / device at @p index, or nullptr when out of range.
    [[nodiscard]] Q_INVOKABLE PhosphorServiceBluetooth::BluetoothAdapter* adapterAt(int index) const;
    [[nodiscard]] Q_INVOKABLE PhosphorServiceBluetooth::BluetoothDevice* deviceAt(int index) const;

Q_SIGNALS:
    void adapterAdded(PhosphorServiceBluetooth::BluetoothAdapter* adapter);
    void adapterRemoved(PhosphorServiceBluetooth::BluetoothAdapter* adapter);
    void deviceAdded(PhosphorServiceBluetooth::BluetoothDevice* device);
    void deviceRemoved(PhosphorServiceBluetooth::BluetoothDevice* device);
    void adapterCountChanged();
    void deviceCountChanged();

private:
    Q_DISABLE_COPY_MOVE(BluetoothHost)
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceBluetooth
