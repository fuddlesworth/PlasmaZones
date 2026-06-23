// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBluetooth/BluetoothHost.h>

#include <PhosphorServiceBluetooth/BluetoothAdapter.h>
#include <PhosphorServiceBluetooth/BluetoothAgent.h>
#include <PhosphorServiceBluetooth/BluetoothDevice.h>

#include <PhosphorDBus/Client.h>
#include <PhosphorDBus/ObjectManager.h>

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcBluetoothHost, "phosphor.service.bluetooth.host")

namespace {
constexpr auto kService = "org.bluez";
constexpr auto kRootPath = "/";
constexpr auto kManagerPath = "/org/bluez";
constexpr auto kAgentManagerIface = "org.bluez.AgentManager1";
constexpr auto kAdapterIface = "org.bluez.Adapter1";
constexpr auto kDeviceIface = "org.bluez.Device1";
constexpr auto kAgentCapability = "KeyboardDisplay";
} // namespace

namespace PhosphorServiceBluetooth {

class BluetoothHost::Private
{
public:
    BluetoothHost* owner = nullptr;
    QDBusConnection bus;
    QString service;
    PhosphorDBus::ObjectManager* objectManager = nullptr;
    BluetoothAgent* agent = nullptr;

    QList<BluetoothAdapter*> adapters;
    QList<BluetoothDevice*> devices;

    explicit Private(QDBusConnection connection)
        : bus(std::move(connection))
    {
    }

    int indexOfAdapter(const QString& path) const
    {
        for (int i = 0; i < adapters.size(); ++i) {
            if (adapters.at(i)->dbusPath() == path)
                return i;
        }
        return -1;
    }

    int indexOfDevice(const QString& path) const
    {
        for (int i = 0; i < devices.size(); ++i) {
            if (devices.at(i)->dbusPath() == path)
                return i;
        }
        return -1;
    }

    void handleInterfacesAdded(const QString& path, const PhosphorDBus::InterfaceMap& interfaces)
    {
        if (interfaces.contains(QLatin1String(kAdapterIface)))
            addAdapter(path, interfaces.value(QLatin1String(kAdapterIface)));
        if (interfaces.contains(QLatin1String(kDeviceIface)))
            addDevice(path, interfaces.value(QLatin1String(kDeviceIface)));
    }

    void handleInterfacesRemoved(const QString& path, const QStringList& interfaces)
    {
        if (interfaces.contains(QLatin1String(kAdapterIface))) {
            removeAdapter(path);
            // A removed adapter takes its devices with it. BlueZ usually
            // emits per-device removals too, but drop any survivors keyed
            // under this adapter's path so the device list can't dangle.
            removeDevicesUnder(path);
        }
        if (interfaces.contains(QLatin1String(kDeviceIface)))
            removeDevice(path);
    }

    void addAdapter(const QString& path, const QVariantMap& properties)
    {
        if (indexOfAdapter(path) != -1)
            return;
        auto* adapter = new BluetoothAdapter(bus, path, properties, owner);
        adapters.append(adapter);
        Q_EMIT owner->adapterAdded(adapter);
        Q_EMIT owner->adapterCountChanged();
    }

    void removeAdapter(const QString& path)
    {
        const int index = indexOfAdapter(path);
        if (index == -1)
            return;
        // Detach from the list before signalling so observers see post-remove
        // state, then defer destruction past any in-flight slot invocations.
        BluetoothAdapter* adapter = adapters.takeAt(index);
        Q_EMIT owner->adapterRemoved(adapter);
        Q_EMIT owner->adapterCountChanged();
        adapter->deleteLater();
    }

    void addDevice(const QString& path, const QVariantMap& properties)
    {
        if (indexOfDevice(path) != -1)
            return;
        auto* device = new BluetoothDevice(bus, path, properties, owner);
        devices.append(device);
        Q_EMIT owner->deviceAdded(device);
        Q_EMIT owner->deviceCountChanged();
    }

    void removeDevice(const QString& path)
    {
        const int index = indexOfDevice(path);
        if (index == -1)
            return;
        BluetoothDevice* device = devices.takeAt(index);
        Q_EMIT owner->deviceRemoved(device);
        Q_EMIT owner->deviceCountChanged();
        device->deleteLater();
    }

    void removeDevicesUnder(const QString& adapterPath)
    {
        const QString prefix = adapterPath + QLatin1Char('/');
        // Iterate over a copy of the paths: removeDevice mutates `devices`.
        const QList<BluetoothDevice*> snapshot = devices;
        for (BluetoothDevice* device : snapshot) {
            if (device->dbusPath().startsWith(prefix))
                removeDevice(device->dbusPath());
        }
    }

    void start()
    {
        objectManager =
            new PhosphorDBus::ObjectManager(bus, service, QLatin1String(kRootPath), owner, &lcBluetoothHost());
        QObject::connect(objectManager, &PhosphorDBus::ObjectManager::interfacesAdded, owner,
                         [this](const QString& path, const PhosphorDBus::InterfaceMap& interfaces) {
                             handleInterfacesAdded(path, interfaces);
                         });
        QObject::connect(objectManager, &PhosphorDBus::ObjectManager::interfacesRemoved, owner,
                         [this](const QString& path, const QStringList& interfaces) {
                             handleInterfacesRemoved(path, interfaces);
                         });
        registerAgent();
    }

    void registerAgent()
    {
        agent = new BluetoothAgent(owner);
        if (!bus.registerObject(BluetoothAgent::agentPath(), agent, QDBusConnection::ExportAllSlots)) {
            qCWarning(lcBluetoothHost) << "failed to export the pairing agent at" << BluetoothAgent::agentPath();
            // An agent that isn't exported can never be driven by BlueZ, so drop
            // it: agent() then returns null, matching its documented contract
            // (present only when the agent is actually usable).
            delete agent;
            agent = nullptr;
            return;
        }
        // Register with BlueZ and request default-agent status. Both are
        // fire-and-forget and best-effort: RequestDefaultAgent fails when
        // another agent (e.g. bluetoothctl, gnome-bluetooth) already holds the
        // default slot, but pairing still works as a non-default agent.
        PhosphorDBus::Client manager(bus, service, QLatin1String(kManagerPath), &lcBluetoothHost());
        const QDBusObjectPath agentPath{BluetoothAgent::agentPath()};
        manager.fireAndForget(owner, QLatin1String(kAgentManagerIface), QStringLiteral("RegisterAgent"),
                              {QVariant::fromValue(agentPath), QString::fromLatin1(kAgentCapability)},
                              QStringLiteral("RegisterAgent"));
        manager.fireAndForget(owner, QLatin1String(kAgentManagerIface), QStringLiteral("RequestDefaultAgent"),
                              {QVariant::fromValue(agentPath)}, QStringLiteral("RequestDefaultAgent"));
    }
};

BluetoothHost::BluetoothHost(QObject* parent)
    : BluetoothHost(QDBusConnection::systemBus(), QLatin1String(kService), parent)
{
}

BluetoothHost::BluetoothHost(QDBusConnection connection, QString service, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(std::move(connection)))
{
    d->owner = this;
    d->service = std::move(service);

    if (!d->bus.isConnected()) {
        qCWarning(lcBluetoothHost) << "bus unavailable; BluetoothHost inert for" << d->service;
        return;
    }
    d->start();
}

BluetoothHost::~BluetoothHost() = default;

QList<BluetoothAdapter*> BluetoothHost::adapters() const
{
    return d->adapters;
}

QList<BluetoothDevice*> BluetoothHost::devices() const
{
    return d->devices;
}

int BluetoothHost::adapterCount() const
{
    return static_cast<int>(d->adapters.size());
}

int BluetoothHost::deviceCount() const
{
    return static_cast<int>(d->devices.size());
}

BluetoothAgent* BluetoothHost::agent() const
{
    return d->agent;
}

BluetoothAdapter* BluetoothHost::adapterAt(int index) const
{
    if (index < 0 || index >= d->adapters.size())
        return nullptr;
    return d->adapters.at(index);
}

BluetoothDevice* BluetoothHost::deviceAt(int index) const
{
    if (index < 0 || index >= d->devices.size())
        return nullptr;
    return d->devices.at(index);
}

} // namespace PhosphorServiceBluetooth
