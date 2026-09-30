// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusConnection>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

namespace PhosphorServiceNetwork {
class NetworkHost;
}
namespace PhosphorServiceBluetooth {
class BluetoothHost;
class BluetoothAdapter;
}

namespace PhosphorShellApp {

// Coordinates the radios exposed by NetworkManager and BlueZ. It never
// disables global networking, so wired connections stay available.
class AirplaneController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY changed)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY changed)
    Q_PROPERTY(bool wifiHardwareEnabled READ wifiHardwareEnabled NOTIFY changed)
    Q_PROPERTY(bool wwanAvailable READ wwanAvailable NOTIFY changed)
    Q_PROPERTY(bool wwanEnabled READ wwanEnabled NOTIFY changed)
    Q_PROPERTY(bool wwanHardwareEnabled READ wwanHardwareEnabled NOTIFY changed)
    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY changed)
    Q_PROPERTY(bool bluetoothEnabled READ bluetoothEnabled NOTIFY changed)
    Q_PROPERTY(bool restoreAvailable READ restoreAvailable NOTIFY changed)
public:
    explicit AirplaneController(QObject* parent = nullptr);
    explicit AirplaneController(QDBusConnection system, QObject* parent = nullptr);
    bool available() const;
    bool enabled() const
    {
        return m_enabled;
    }
    bool pending() const
    {
        return m_pending;
    }
    QString error() const
    {
        return m_error;
    }
    bool wifiAvailable() const;
    bool wifiEnabled() const;
    bool wifiHardwareEnabled() const;
    bool wwanAvailable() const;
    bool wwanEnabled() const;
    bool wwanHardwareEnabled() const;
    bool bluetoothAvailable() const;
    bool bluetoothEnabled() const;
    bool restoreAvailable() const
    {
        return m_snapshot && !m_enabled && !m_pending;
    }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void setWifiEnabled(bool enabled);
    Q_INVOKABLE void setWwanEnabled(bool enabled);
    Q_INVOKABLE void setBluetoothEnabled(bool enabled);
    Q_INVOKABLE void restoreRadios();
Q_SIGNALS:
    void changed();

private:
    void publish();
    void radiosChanged();
    void watchAdapter(PhosphorServiceBluetooth::BluetoothAdapter* adapter);
    void begin(bool desiredMode);
    void write(const QString& key, bool value);
    void finishIfReady();
    void fail(const QString& message);
    void clearSnapshot();
    bool radioValue(const QString& key) const;
    QString adapterKey(PhosphorServiceBluetooth::BluetoothAdapter* adapter) const;

    QDBusConnection m_bus;
    PhosphorServiceNetwork::NetworkHost* m_network;
    PhosphorServiceBluetooth::BluetoothHost* m_bluetooth;
    QTimer m_timeout;
    QHash<QString, bool> m_targets;
    QHash<QString, bool> m_savedBluetooth;
    QByteArray m_published;
    QString m_error;
    quint64 m_generation = 0;
    int m_calls = 0;
    bool m_batch = false;
    bool m_pending = false;
    bool m_enabled = false;
    bool m_desiredMode = false;
    bool m_allowBluetooth = false;
    bool m_snapshot = false;
    bool m_savedNetwork = false;
    bool m_savedWifi = false;
    bool m_savedWwanSupported = false;
    bool m_savedWwan = false;
    bool m_restoring = false;
    bool m_clearOnSuccess = false;
};
}
