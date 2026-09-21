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
#include <QDBusServiceWatcher>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusMessage>
#include <QHash>

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
    std::shared_ptr<BluetoothAgent> agent;

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
        const auto battery = interfaces.constFind(QStringLiteral("org.bluez.Battery1"));
        if (battery != interfaces.cend() && indexOfDevice(path) >= 0)
            devices.at(indexOfDevice(path))->applyBattery(battery.value());
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
        if (interfaces.contains(QStringLiteral("org.bluez.Battery1")) && indexOfDevice(path) >= 0)
            devices.at(indexOfDevice(path))->applyBattery({});
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

    void observe()
    {
        delete objectManager;
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
    }

    void registerAgent()
    {
        // BlueZ permits one agent per bus client. All shell hosts share it;
        // opening a detail panel must not lose pairing to the bar's host.
        static QHash<QString, std::weak_ptr<BluetoothAgent>> agents;
        const auto key = bus.name() + QLatin1Char(':') + service;
        agent = agents.value(key).lock();
        if (agent)
            return;
        auto connection = bus;
        const auto destination = service;
        auto exported = std::make_shared<bool>(false);
        agent = std::shared_ptr<BluetoothAgent>(
            new BluetoothAgent, [connection, destination, exported](BluetoothAgent* instance) mutable {
                if (!*exported) {
                    delete instance;
                    return;
                }
                auto message = QDBusMessage::createMethodCall(destination, QLatin1String(kManagerPath),
                                                              QLatin1String(kAgentManagerIface),
                                                              QStringLiteral("UnregisterAgent"));
                message << QVariant::fromValue(QDBusObjectPath(BluetoothAgent::agentPath()));
                connection.asyncCall(message);
                connection.unregisterObject(BluetoothAgent::agentPath());
                delete instance;
            });
        if (!bus.registerObject(BluetoothAgent::agentPath(), agent.get(), QDBusConnection::ExportAllSlots)) {
            qCWarning(lcBluetoothHost) << "failed to export the pairing agent";
            agent.reset();
            return;
        }
        *exported = true;
        agents.insert(key, agent);
        auto* current = agent.get();
        const auto generation = std::make_shared<quint64>(0);
        const auto registerCurrent = [connection, destination, current, generation] {
            const auto requestGeneration = ++*generation;
            current->setAvailable(false);
            auto* watcher = new QDBusPendingCallWatcher(
                PhosphorDBus::Client(connection, destination, QLatin1String(kManagerPath), &lcBluetoothHost())
                    .asyncCall(QLatin1String(kAgentManagerIface), QStringLiteral("RegisterAgent"),
                               {QVariant::fromValue(QDBusObjectPath(BluetoothAgent::agentPath())),
                                QString::fromLatin1(kAgentCapability)}),
                current);
            QObject::connect(watcher, &QDBusPendingCallWatcher::finished, current,
                             [current, generation, requestGeneration](QDBusPendingCallWatcher* call) {
                                 call->deleteLater();
                                 if (requestGeneration != *generation)
                                     return;
                                 const QDBusPendingReply<> reply = *call;
                                 current->setAvailable(!reply.isError()
                                                       || reply.error().name()
                                                           == QLatin1String("org.bluez.Error.AlreadyExists"));
                             });
        };
        // Only our own pairing requests use this agent; another desktop's
        // default agent continues handling its incoming requests.
        auto* watcher = new QDBusServiceWatcher(service, bus, QDBusServiceWatcher::WatchForOwnerChange, current);
        QObject::connect(
            watcher, &QDBusServiceWatcher::serviceOwnerChanged, current,
            [current, registerCurrent, generation](const QString&, const QString&, const QString& newOwner) {
                ++*generation;
                current->Cancel();
                current->setAvailable(false);
                if (!newOwner.isEmpty())
                    registerCurrent();
            });
        registerCurrent();
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
    d->registerAgent();
    d->observe();
    auto* watcher = new QDBusServiceWatcher(d->service, d->bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString&, const QString&, const QString& newOwner) {
                delete d->objectManager;
                d->objectManager = nullptr;
                while (!d->devices.isEmpty())
                    d->removeDevice(d->devices.constFirst()->dbusPath());
                while (!d->adapters.isEmpty())
                    d->removeAdapter(d->adapters.constFirst()->dbusPath());
                if (!newOwner.isEmpty())
                    d->observe();
            });
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
    return d->agent.get();
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

void BluetoothHost::refresh()
{
    if (d->bus.isConnected())
        d->observe();
}

} // namespace PhosphorServiceBluetooth
