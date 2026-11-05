// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-service-bluetooth-cli: headless acceptance harness + worked
// example for the phosphor-service-bluetooth library. Drives the lib
// directly against a live org.bluez (no IPC), the same pattern as
// examples/phosphor-service-network-cli.
//
// Everything in the lib is async (D-Bus round trips with no blocking), so
// each command constructs a BluetoothHost and then pumps the event loop for
// a bounded settle window before reading state. The waits are deliberately
// generous rather than signal-precise: this is a developer harness, not a
// latency-sensitive tool. The `pair` command registers as the interactive
// agent responder and reads confirmations / passkeys from stdin.

#include <PhosphorServiceBluetooth/BluetoothAdapter.h>
#include <PhosphorServiceBluetooth/BluetoothAgent.h>
#include <PhosphorServiceBluetooth/BluetoothDevice.h>
#include <PhosphorServiceBluetooth/BluetoothHost.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

using namespace PhosphorServiceBluetooth;

namespace {

QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}
QTextStream& err()
{
    static QTextStream s(stderr);
    return s;
}

// Run the event loop for `ms` so the lib's async D-Bus replies can land.
void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QString prompt(const QString& label)
{
    out() << label;
    out().flush();
    QTextStream in(stdin);
    return in.readLine();
}

BluetoothAdapter* firstAdapter(const BluetoothHost& host)
{
    return host.adapterCount() > 0 ? host.adapterAt(0) : nullptr;
}

BluetoothDevice* findDevice(const BluetoothHost& host, const QString& address)
{
    const auto devices = host.devices();
    for (auto* device : devices) {
        if (device->address().compare(address, Qt::CaseInsensitive) == 0)
            return device;
    }
    return nullptr;
}

QString yesNo(bool value)
{
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

void printAdapter(BluetoothAdapter* adapter)
{
    out() << adapter->dbusPath() << "\n"
          << "  address:      " << adapter->address() << "\n"
          << "  name:         " << adapter->name() << "\n"
          << "  powered:      " << yesNo(adapter->powered()) << "\n"
          << "  discoverable: " << yesNo(adapter->discoverable()) << "\n"
          << "  discovering:  " << yesNo(adapter->discovering()) << "\n";
}

void printDevice(BluetoothDevice* device)
{
    const QString label = device->alias().isEmpty() ? device->name() : device->alias();
    out() << device->address() << "\t" << (label.isEmpty() ? QStringLiteral("(unknown)") : label) << "\n"
          << "  paired:    " << yesNo(device->paired()) << "\n"
          << "  connected: " << yesNo(device->connected()) << "\n"
          << "  trusted:   " << yesNo(device->trusted()) << "\n"
          << "  rssi:      " << device->rssi() << "\n";
}

// Wire the agent so a pairing handshake can be answered from stdin. Returns
// nothing; the connections live for the lifetime of `host`.
void wireAgent(BluetoothHost& host)
{
    BluetoothAgent* agent = host.agent();
    if (!agent)
        return;
    QObject::connect(
        agent, &BluetoothAgent::confirmationRequested, [agent](const QString& device, quint32 passkey, quint64 id) {
            const QString answer = prompt(QStringLiteral("Confirm passkey %1 for %2? [y/N] ").arg(passkey).arg(device));
            agent->respondConfirmation(id, answer.trimmed().compare(QStringLiteral("y"), Qt::CaseInsensitive) == 0);
        });
    QObject::connect(agent, &BluetoothAgent::passkeyRequested, [agent](const QString& device, quint64 id) {
        bool ok = false;
        const quint32 passkey = prompt(QStringLiteral("Enter passkey for %1: ").arg(device)).toUInt(&ok);
        if (ok)
            agent->respondPasskey(id, passkey);
        else
            agent->rejectRequest(id);
    });
    QObject::connect(agent, &BluetoothAgent::pinCodeRequested, [agent](const QString& device, quint64 id) {
        agent->respondPinCode(id, prompt(QStringLiteral("Enter PIN for %1: ").arg(device)));
    });
    QObject::connect(agent, &BluetoothAgent::authorizationRequested, [agent](const QString& device, quint64 id) {
        const QString answer = prompt(QStringLiteral("Authorize pairing with %1? [y/N] ").arg(device));
        agent->respondConfirmation(id, answer.trimmed().compare(QStringLiteral("y"), Qt::CaseInsensitive) == 0);
    });
    QObject::connect(
        agent, &BluetoothAgent::serviceAuthorizationRequested,
        [agent](const QString& device, const QString& uuid, quint64 id) {
            const QString answer = prompt(QStringLiteral("Authorize service %1 on %2? [y/N] ").arg(uuid, device));
            agent->respondConfirmation(id, answer.trimmed().compare(QStringLiteral("y"), Qt::CaseInsensitive) == 0);
        });
    QObject::connect(agent, &BluetoothAgent::passkeyDisplayed,
                     [](const QString& device, quint32 passkey, quint16 entered) {
                         out() << "Passkey for " << device << ": " << passkey << " (entered " << entered << ")\n";
                         out().flush();
                     });
    QObject::connect(agent, &BluetoothAgent::pinCodeDisplayed, [](const QString& device, const QString& pin) {
        out() << "PIN for " << device << ": " << pin << "\n";
        out().flush();
    });
}

int cmdStatus()
{
    BluetoothHost host;
    pump(1500);
    out() << "adapters: " << host.adapterCount() << "  devices: " << host.deviceCount() << "\n";
    const auto adapters = host.adapters();
    for (auto* adapter : adapters)
        printAdapter(adapter);
    out().flush();
    return 0;
}

int cmdListAdapters()
{
    BluetoothHost host;
    pump(1500);
    if (host.adapterCount() == 0) {
        err() << "no Bluetooth adapters found\n";
        err().flush();
        return 1;
    }
    const auto adapters = host.adapters();
    for (auto* adapter : adapters)
        printAdapter(adapter);
    out().flush();
    return 0;
}

int cmdListDevices(const QString& adapterFilter)
{
    BluetoothHost host;
    pump(1500);
    const auto devices = host.devices();
    int shown = 0;
    for (auto* device : devices) {
        // Accept a full adapter path or a bare adapter name (e.g. "hci0").
        // Anchor the suffix on '/' so "hci1" does not match "hci11".
        if (!adapterFilter.isEmpty() && device->adapter() != adapterFilter
            && !device->adapter().endsWith(QLatin1Char('/') + adapterFilter)) {
            continue;
        }
        printDevice(device);
        ++shown;
    }
    if (shown == 0)
        out() << "(no devices)\n";
    out().flush();
    return 0;
}

int cmdPower(const QString& state)
{
    const bool on = state.compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0;
    const bool off = state.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0;
    if (!on && !off) {
        err() << "power expects 'on' or 'off'\n";
        err().flush();
        return 64;
    }
    BluetoothHost host;
    pump(1500);
    BluetoothAdapter* adapter = firstAdapter(host);
    if (!adapter) {
        err() << "no Bluetooth adapters found\n";
        err().flush();
        return 1;
    }
    adapter->setPowered(on);
    pump(1500);
    out() << adapter->dbusPath() << " powered: " << yesNo(adapter->powered()) << "\n";
    out().flush();
    return 0;
}

int cmdScan(int seconds)
{
    BluetoothHost host;
    pump(1500);
    BluetoothAdapter* adapter = firstAdapter(host);
    if (!adapter) {
        err() << "no Bluetooth adapters found\n";
        err().flush();
        return 1;
    }
    adapter->startDiscovery();
    out() << "scanning for " << seconds << "s...\n";
    out().flush();
    pump(seconds * 1000);
    adapter->stopDiscovery();
    const auto devices = host.devices();
    for (auto* device : devices)
        printDevice(device);
    if (devices.isEmpty())
        out() << "(no devices found)\n";
    out().flush();
    return 0;
}

int cmdPair(const QString& address)
{
    BluetoothHost host;
    wireAgent(host);
    pump(1500);
    BluetoothDevice* device = findDevice(host, address);
    if (!device) {
        err() << "no device with address '" << address << "' (run 'scan' first)\n";
        err().flush();
        return 1;
    }
    device->pair();
    pump(20000); // pairing involves user interaction + the handshake
    out() << device->address() << " paired: " << yesNo(device->paired())
          << "  connected: " << yesNo(device->connected()) << "\n";
    out().flush();
    return device->paired() ? 0 : 1;
}

int cmdDeviceAction(const QString& address, const QString& action)
{
    BluetoothHost host;
    pump(1500);
    BluetoothDevice* device = findDevice(host, address);
    if (!device) {
        err() << "no device with address '" << address << "'\n";
        err().flush();
        return 1;
    }
    if (action == QLatin1String("connect")) {
        device->connectDevice();
        pump(6000);
        out() << device->address() << " connected: " << yesNo(device->connected()) << "\n";
        out().flush();
        return device->connected() ? 0 : 1;
    }
    if (action == QLatin1String("disconnect")) {
        device->disconnectDevice();
        pump(4000);
        out() << device->address() << " connected: " << yesNo(device->connected()) << "\n";
        out().flush();
        return 0;
    }
    if (action == QLatin1String("trust") || action == QLatin1String("untrust")) {
        device->setTrusted(action == QLatin1String("trust"));
        pump(1500);
        out() << device->address() << " trusted: " << yesNo(device->trusted()) << "\n";
        out().flush();
        return 0;
    }
    if (action == QLatin1String("remove")) {
        BluetoothAdapter* adapter = firstAdapter(host);
        if (!adapter) {
            err() << "no adapter to remove the device from\n";
            err().flush();
            return 1;
        }
        adapter->removeDevice(device->dbusPath());
        pump(2000);
        out() << "removed " << address << "\n";
        out().flush();
        return 0;
    }
    return 1;
}

// Exit codes follow the sysexits convention the network/pipewire CLIs use:
// 0 ok, 64 usage error, 1 runtime failure.
int usage()
{
    err() << "usage: phosphor-service-bluetooth-cli <command> [args]\n"
          << "  status                     adapter + device summary\n"
          << "  list-adapters              all BlueZ adapters\n"
          << "  list-devices [adapter]     known devices (optionally one adapter)\n"
          << "  power <on|off>             power the first adapter on/off\n"
          << "  scan [seconds]             discover nearby devices (default 8s)\n"
          << "  pair <address>             pair the device (interactive agent)\n"
          << "  connect <address>          connect a paired device\n"
          << "  disconnect <address>       disconnect a device\n"
          << "  trust <address>            mark a device trusted\n"
          << "  untrust <address>          mark a device untrusted\n"
          << "  remove <address>           forget a device\n";
    err().flush();
    return 64;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2)
        return usage();

    const QString command = args.at(1);
    if (command == QLatin1String("status"))
        return cmdStatus();
    if (command == QLatin1String("list-adapters"))
        return cmdListAdapters();
    if (command == QLatin1String("list-devices"))
        return cmdListDevices(args.size() > 2 ? args.at(2) : QString());
    if (command == QLatin1String("power")) {
        if (args.size() < 3)
            return usage();
        return cmdPower(args.at(2));
    }
    if (command == QLatin1String("scan")) {
        int seconds = 8;
        if (args.size() > 2) {
            bool ok = false;
            seconds = args.at(2).toInt(&ok);
            // Cap at an hour: keeps `seconds * 1000` well inside int range in
            // pump(), and an unbounded scan makes no sense for a demo harness.
            if (!ok || seconds <= 0 || seconds > 3600)
                return usage();
        }
        return cmdScan(seconds);
    }
    if (command == QLatin1String("pair")) {
        if (args.size() < 3)
            return usage();
        return cmdPair(args.at(2));
    }
    if (command == QLatin1String("connect") || command == QLatin1String("disconnect")
        || command == QLatin1String("trust") || command == QLatin1String("untrust")
        || command == QLatin1String("remove")) {
        if (args.size() < 3)
            return usage();
        return cmdDeviceAction(args.at(2), command);
    }
    return usage();
}
