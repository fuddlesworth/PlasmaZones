// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AirplaneController.h"
#include "PhosphorShellI18n.h"

#include <PhosphorServiceBluetooth/BluetoothAdapter.h>
#include <PhosphorServiceBluetooth/BluetoothHost.h>
#include <PhosphorServiceNetwork/NetworkDevice.h>
#include <PhosphorServiceNetwork/NetworkHost.h>

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QJsonArray>
#include <QJsonDocument>

namespace PhosphorShellApp {
namespace {
const QString networkService = QStringLiteral("org.freedesktop.NetworkManager");
const QString networkPath = QStringLiteral("/org/freedesktop/NetworkManager");
const QString bluetoothService = QStringLiteral("org.bluez");
const QString bluetoothInterface = QStringLiteral("org.bluez.Adapter1");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString wifiKey = QStringLiteral("wifi");
const QString wwanKey = QStringLiteral("wwan");
}

AirplaneController::AirplaneController(QObject* parent)
    : AirplaneController(QDBusConnection::systemBus(), parent)
{
}

AirplaneController::AirplaneController(QDBusConnection system, QObject* parent)
    : QObject(parent)
    , m_bus(std::move(system))
    , m_network(new PhosphorServiceNetwork::NetworkHost(m_bus, this))
    , m_bluetooth(new PhosphorServiceBluetooth::BluetoothHost(m_bus, bluetoothService, this))
{
    using Network = PhosphorServiceNetwork::NetworkHost;
    for (auto signal : {&Network::availableChanged, &Network::wirelessEnabledChanged,
                        &Network::wirelessHardwareEnabledChanged, &Network::wwanChanged, &Network::deviceCountChanged})
        connect(m_network, signal, this, &AirplaneController::radiosChanged);
    connect(m_network, &Network::deviceAdded, this, [this](PhosphorServiceNetwork::NetworkDevice* device) {
        connect(device, &PhosphorServiceNetwork::NetworkDevice::deviceTypeChanged, this, &AirplaneController::publish);
        publish();
    });
    connect(m_bluetooth, &PhosphorServiceBluetooth::BluetoothHost::adapterAdded, this,
            &AirplaneController::watchAdapter);
    connect(m_bluetooth, &PhosphorServiceBluetooth::BluetoothHost::adapterRemoved, this,
            [this](PhosphorServiceBluetooth::BluetoothAdapter* adapter) {
                if (m_targets.contains(adapter->dbusPath()))
                    fail(PhosphorI18n::tr("A Bluetooth adapter was disconnected before its radio change completed."));
                radiosChanged();
            });
    auto* watcher = new QDBusServiceWatcher(this);
    watcher->setConnection(m_bus);
    watcher->setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    watcher->addWatchedService(networkService);
    watcher->addWatchedService(bluetoothService);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString&, const QString& oldOwner, const QString&) {
                if (!oldOwner.isEmpty() && (m_pending || m_enabled))
                    fail(PhosphorI18n::tr(
                        "A radio service restarted. Check the radio states before enabling airplane mode again."));
            });
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(10000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        fail(PhosphorI18n::tr("The radio change was not confirmed. Check the radio states and try again."));
    });
    publish();
}

bool AirplaneController::available() const
{
    return m_network->available() || bluetoothAvailable();
}
bool AirplaneController::wifiAvailable() const
{
    if (!m_network->available())
        return false;
    for (auto* device : m_network->devices()) {
        if (device->deviceType() == PhosphorServiceNetwork::NetworkDevice::Wifi)
            return true;
    }
    return false;
}
bool AirplaneController::wifiEnabled() const
{
    return m_network->available() && m_network->wirelessEnabled();
}
bool AirplaneController::wifiHardwareEnabled() const
{
    return m_network->available() && m_network->wirelessHardwareEnabled();
}
bool AirplaneController::wwanAvailable() const
{
    if (!m_network->available() || !m_network->wwanSupported())
        return false;
    for (auto* device : m_network->devices()) {
        if (device->deviceType() == PhosphorServiceNetwork::NetworkDevice::Modem)
            return true;
    }
    return false;
}
bool AirplaneController::wwanEnabled() const
{
    return m_network->available() && m_network->wwanEnabled();
}
bool AirplaneController::wwanHardwareEnabled() const
{
    return m_network->available() && m_network->wwanHardwareEnabled();
}
bool AirplaneController::bluetoothAvailable() const
{
    return m_bluetooth->adapterCount() > 0;
}
bool AirplaneController::bluetoothEnabled() const
{
    for (auto* adapter : m_bluetooth->adapters()) {
        if (adapter->powered())
            return true;
    }
    return false;
}

void AirplaneController::publish()
{
    const auto value =
        QJsonDocument(QJsonArray{available(), enabled(), pending(), error(), wifiAvailable(), wifiEnabled(),
                                 wifiHardwareEnabled(), wwanAvailable(), wwanEnabled(), wwanHardwareEnabled(),
                                 bluetoothAvailable(), bluetoothEnabled(), restoreAvailable()})
            .toJson(QJsonDocument::Compact);
    if (value == m_published)
        return;
    m_published = value;
    Q_EMIT changed();
}

void AirplaneController::clearSnapshot()
{
    m_snapshot = false;
    m_savedNetwork = false;
    m_savedBluetooth.clear();
}

void AirplaneController::radiosChanged()
{
    if (!m_pending && m_enabled && (wifiEnabled() || wwanEnabled() || (!m_allowBluetooth && bluetoothEnabled()))) {
        // Respect another client's explicit radio change. Do not restore other
        // radios or immediately fight the new value by writing it back.
        m_enabled = false;
        clearSnapshot();
    }
    finishIfReady();
    publish();
}

QString AirplaneController::adapterKey(PhosphorServiceBluetooth::BluetoothAdapter* adapter) const
{
    return adapter->address().isEmpty() ? adapter->dbusPath() : adapter->address();
}

void AirplaneController::watchAdapter(PhosphorServiceBluetooth::BluetoothAdapter* adapter)
{
    connect(adapter, &PhosphorServiceBluetooth::BluetoothAdapter::poweredChanged, this,
            &AirplaneController::radiosChanged);
    if ((m_enabled || (m_pending && m_desiredMode)) && !m_allowBluetooth) {
        const auto key = adapterKey(adapter);
        if (!m_savedBluetooth.contains(key))
            m_savedBluetooth.insert(key, adapter->powered());
        if (!m_pending)
            begin(true);
        write(adapter->dbusPath(), false);
        m_batch = false;
        finishIfReady();
    }
    publish();
}

void AirplaneController::refresh()
{
    m_network->refresh();
    m_bluetooth->refresh();
}

void AirplaneController::begin(bool desiredMode)
{
    ++m_generation;
    m_pending = true;
    m_batch = true;
    m_desiredMode = desiredMode;
    m_calls = 0;
    m_targets.clear();
    m_error.clear();
    m_clearOnSuccess = false;
    m_timeout.start();
}

bool AirplaneController::radioValue(const QString& key) const
{
    if (key == wifiKey)
        return wifiEnabled();
    if (key == wwanKey)
        return wwanEnabled();
    for (auto* adapter : m_bluetooth->adapters()) {
        if (adapter->dbusPath() == key)
            return adapter->powered();
    }
    return false;
}

void AirplaneController::write(const QString& key, bool value)
{
    m_targets.insert(key, value);
    if (radioValue(key) == value)
        return;
    const bool network = key == wifiKey || key == wwanKey;
    auto message = QDBusMessage::createMethodCall(network ? networkService : bluetoothService,
                                                  network ? networkPath : key, properties, QStringLiteral("Set"));
    message << (network ? networkService : bluetoothInterface)
            << (network ? (key == wifiKey ? QStringLiteral("WirelessEnabled") : QStringLiteral("WwanEnabled"))
                        : QStringLiteral("Powered"))
            << QVariant::fromValue(QDBusVariant(value));
    const auto generation = m_generation;
    ++m_calls;
    auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 8000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation, network] {
        const QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        --m_calls;
        if (reply.isError()) {
            // Other writes may already have succeeded. Keep the snapshot so
            // the explicit Restore action can undo just this operation.
            m_error = PhosphorI18n::tr(
                "Some radios could not be changed. Check their states or restore the previous settings.");
        }
        if (network)
            m_network->refresh();
        finishIfReady();
        publish();
    });
}

void AirplaneController::finishIfReady()
{
    if (!m_pending || m_batch || m_calls > 0)
        return;
    if (!m_error.isEmpty()) {
        fail(m_error);
        return;
    }
    if ((m_targets.contains(wifiKey) || m_targets.contains(wwanKey)) && !m_network->available())
        return;
    for (auto it = m_targets.cbegin(); it != m_targets.cend(); ++it) {
        if (radioValue(it.key()) != it.value())
            return;
    }
    m_timeout.stop();
    m_pending = false;
    m_targets.clear();
    m_enabled = m_desiredMode;
    if (m_enabled && (wifiEnabled() || wwanEnabled() || (!m_allowBluetooth && bluetoothEnabled()))) {
        m_enabled = false;
        clearSnapshot();
    }
    if (m_restoring || m_clearOnSuccess)
        clearSnapshot();
    m_restoring = false;
    publish();
}

void AirplaneController::fail(const QString& message)
{
    ++m_generation;
    m_timeout.stop();
    m_pending = false;
    m_enabled = false;
    m_restoring = false;
    m_batch = false;
    m_calls = 0;
    m_targets.clear();
    m_error = message;
    publish();
}

void AirplaneController::setEnabled(bool value)
{
    if (m_pending || !available())
        return;
    if (!value) {
        restoreRadios();
        return;
    }
    if (m_enabled)
        return;
    if (!m_snapshot) {
        m_snapshot = true;
        m_savedNetwork = m_network->available();
        m_savedWifi = wifiEnabled();
        m_savedWwanSupported = m_network->wwanSupported();
        m_savedWwan = wwanEnabled();
        for (auto* adapter : m_bluetooth->adapters())
            m_savedBluetooth.insert(adapterKey(adapter), adapter->powered());
    }
    m_allowBluetooth = false;
    begin(true);
    if (m_network->available()) {
        write(wifiKey, false);
        if (m_network->wwanSupported())
            write(wwanKey, false);
    }
    for (auto* adapter : m_bluetooth->adapters())
        write(adapter->dbusPath(), false);
    m_batch = false;
    finishIfReady();
    publish();
}

void AirplaneController::restoreRadios()
{
    if (m_pending || !m_snapshot)
        return;
    begin(false);
    m_restoring = true;
    if (m_savedNetwork) {
        if (!m_network->available()) {
            m_error = PhosphorI18n::tr("NetworkManager is unavailable. Restore the radio settings when it returns.");
        } else {
            write(wifiKey, m_savedWifi);
            if (m_savedWwanSupported && m_network->wwanSupported())
                write(wwanKey, m_savedWwan);
        }
    }
    for (auto* adapter : m_bluetooth->adapters()) {
        const auto saved = m_savedBluetooth.constFind(adapterKey(adapter));
        if (saved != m_savedBluetooth.cend())
            write(adapter->dbusPath(), *saved);
    }
    m_batch = false;
    finishIfReady();
    publish();
}

void AirplaneController::setWifiEnabled(bool value)
{
    if (m_pending || !wifiAvailable() || (value && !wifiHardwareEnabled()))
        return;
    begin(value ? false : m_enabled);
    m_clearOnSuccess = value;
    write(wifiKey, value);
    m_batch = false;
    finishIfReady();
    publish();
}

void AirplaneController::setWwanEnabled(bool value)
{
    if (m_pending || !wwanAvailable() || (value && !wwanHardwareEnabled()))
        return;
    begin(value ? false : m_enabled);
    m_clearOnSuccess = value;
    write(wwanKey, value);
    m_batch = false;
    finishIfReady();
    publish();
}

void AirplaneController::setBluetoothEnabled(bool value)
{
    if (m_pending || !bluetoothAvailable())
        return;
    begin(m_enabled);
    m_allowBluetooth = value;
    for (auto* adapter : m_bluetooth->adapters())
        write(adapter->dbusPath(), value);
    m_batch = false;
    finishIfReady();
    publish();
}
}
