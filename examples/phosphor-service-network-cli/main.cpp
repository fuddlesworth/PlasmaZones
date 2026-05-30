// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-service-network-cli: headless acceptance harness + worked
// example for the phosphor-service-network library. Drives the lib
// directly against a live org.freedesktop.NetworkManager (no IPC), the
// same pattern as examples/phosphor-service-pipewire-cli.
//
// Everything in the lib is async (D-Bus round trips with no blocking),
// so each command constructs the relevant host/model and then pumps the
// event loop for a bounded settle window before reading state. The waits
// are deliberately generous rather than signal-precise: this is a
// developer harness, not a latency-sensitive tool.

#include <PhosphorServiceNetwork/AccessPoint.h>
#include <PhosphorServiceNetwork/AccessPointModel.h>
#include <PhosphorServiceNetwork/NetworkConnectionModel.h>
#include <PhosphorServiceNetwork/NetworkDevice.h>
#include <PhosphorServiceNetwork/NetworkHost.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

using namespace PhosphorServiceNetwork;

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

// WPA-PSK accepts an 8-63 character ASCII passphrase or a 64-character hex
// pre-shared key. Mirrors NetworkHost's boundary so the CLI rejects the same
// inputs the library would silently drop.
bool isValidWpaPassphrase(const QString& passphrase)
{
    const auto length = passphrase.size();
    if (length >= 8 && length <= 63)
        return true;
    if (length != 64)
        return false;
    for (const QChar ch : passphrase) {
        const bool isHexDigit = (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
            || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) || (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'));
        if (!isHexDigit)
            return false;
    }
    return true;
}

// Run the event loop for `ms` so the lib's async D-Bus replies can land.
void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

NetworkDevice* firstWifiDevice(const NetworkHost& host)
{
    const auto devices = host.devices();
    for (auto* device : devices) {
        if (device->deviceType() == NetworkDevice::Wifi)
            return device;
    }
    return nullptr;
}

QString deviceTypeName(NetworkDevice::DeviceType type)
{
    switch (type) {
    case NetworkDevice::Ethernet:
        return QStringLiteral("ethernet");
    case NetworkDevice::Wifi:
        return QStringLiteral("wifi");
    case NetworkDevice::Bluetooth:
        return QStringLiteral("bluetooth");
    case NetworkDevice::Modem:
        return QStringLiteral("modem");
    case NetworkDevice::Bridge:
        return QStringLiteral("bridge");
    case NetworkDevice::Tun:
        return QStringLiteral("tun");
    case NetworkDevice::Wireguard:
        return QStringLiteral("wireguard");
    case NetworkDevice::Unknown:
        return QStringLiteral("unknown");
    default:
        return QStringLiteral("type(%1)").arg(static_cast<int>(type));
    }
}

QString stateName(NetworkDevice::DeviceState state)
{
    switch (state) {
    case NetworkDevice::Unmanaged:
        return QStringLiteral("unmanaged");
    case NetworkDevice::Unavailable:
        return QStringLiteral("unavailable");
    case NetworkDevice::Disconnected:
        return QStringLiteral("disconnected");
    case NetworkDevice::Prepare:
    case NetworkDevice::Config:
    case NetworkDevice::NeedAuth:
    case NetworkDevice::IpConfig:
    case NetworkDevice::IpCheck:
    case NetworkDevice::Secondaries:
        return QStringLiteral("connecting");
    case NetworkDevice::Activated:
        return QStringLiteral("activated");
    case NetworkDevice::Deactivating:
        return QStringLiteral("deactivating");
    case NetworkDevice::Failed:
        return QStringLiteral("failed");
    case NetworkDevice::UnknownState:
        return QStringLiteral("unknown");
    default:
        return QStringLiteral("state(%1)").arg(static_cast<int>(state));
    }
}

QString connectivityName(NetworkHost::Connectivity connectivity)
{
    switch (connectivity) {
    case NetworkHost::NoConnectivity:
        return QStringLiteral("none");
    case NetworkHost::Portal:
        return QStringLiteral("portal");
    case NetworkHost::Limited:
        return QStringLiteral("limited");
    case NetworkHost::Full:
        return QStringLiteral("full");
    case NetworkHost::UnknownConnectivity:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

// Exit codes follow the sysexits convention the pipewire CLI uses: 0 ok,
// 64 usage error, 1 runtime failure.
int usage()
{
    err() << "usage: phosphor-service-network-cli <command> [args]\n"
          << "  status                       manager connectivity + radio state\n"
          << "  list-devices                 all NetworkManager devices\n"
          << "  list-connections             saved connection profiles\n"
          << "  list-aps                     access points on the first Wi-Fi device\n"
          << "  scan                         trigger a Wi-Fi scan, then list access points\n"
          << "  connect <ssid> [passphrase]  connect the first Wi-Fi device to <ssid>\n";
    err().flush();
    return 64;
}

int cmdStatus()
{
    NetworkHost host;
    pump(1200);
    out() << "networking:   " << (host.networkingEnabled() ? "enabled" : "disabled") << "\n";
    out() << "wifi radio:   " << (host.wirelessEnabled() ? "enabled" : "disabled") << "\n";
    out() << "connectivity: " << connectivityName(host.connectivity()) << "\n";
    out() << "primary type: "
          << (host.primaryConnectionType().isEmpty() ? QStringLiteral("(none)") : host.primaryConnectionType()) << "\n";
    out() << "devices:      " << host.deviceCount() << "\n";
    out().flush();
    return 0;
}

int cmdListDevices()
{
    NetworkHost host;
    pump(1500);
    const auto devices = host.devices();
    if (devices.isEmpty()) {
        out() << "(no devices)\n";
        out().flush();
        return 0;
    }
    for (auto* device : devices) {
        out() << device->interfaceName() << "\t" << deviceTypeName(device->deviceType()) << "\t"
              << stateName(device->state()) << (device->managed() ? "" : "\t(unmanaged)") << "\n";
    }
    out().flush();
    return 0;
}

int cmdListConnections()
{
    NetworkConnectionModel model;
    pump(1500);
    if (model.rowCount() == 0) {
        out() << "(no saved connections)\n";
        out().flush();
        return 0;
    }
    for (int i = 0; i < model.rowCount(); ++i) {
        const auto idx = model.index(i);
        out() << model.data(idx, NetworkConnectionModel::IdRole).toString() << "\t"
              << model.data(idx, NetworkConnectionModel::ConnectionTypeRole).toString() << "\t"
              << model.data(idx, NetworkConnectionModel::UuidRole).toString() << "\n";
    }
    out().flush();
    return 0;
}

void printAccessPoints(AccessPointModel& model)
{
    if (model.rowCount() == 0) {
        out() << "(no access points)\n";
        out().flush();
        return;
    }
    for (int i = 0; i < model.rowCount(); ++i) {
        const auto idx = model.index(i);
        const int strength = model.data(idx, AccessPointModel::StrengthRole).toInt();
        const QString security = model.data(idx, AccessPointModel::SecurityRole).toString();
        const QString ssid = model.data(idx, AccessPointModel::SsidRole).toString();
        out() << QStringLiteral("%1%").arg(strength, 3) << "\t"
              << (security.isEmpty() ? QStringLiteral("open") : security) << "\t"
              << (ssid.isEmpty() ? QStringLiteral("(hidden)") : ssid) << "\n";
    }
    out().flush();
}

int cmdListOrScanAps(bool triggerScan)
{
    NetworkHost host;
    pump(1500);
    auto* wifi = firstWifiDevice(host);
    if (!wifi) {
        err() << "no Wi-Fi device found\n";
        err().flush();
        return 1;
    }
    AccessPointModel model;
    model.setDevice(wifi);
    if (triggerScan) {
        host.scanWifi();
        pump(4000); // a fresh scan takes a couple of seconds to complete
    } else {
        pump(1500);
    }
    printAccessPoints(model);
    return 0;
}

int cmdConnect(const QString& ssid, const QString& passphrase)
{
    // Mirror the library's WPA-PSK boundary (an 8-63 character passphrase or
    // a 64-character hex PSK). connectToAccessPoint silently drops an
    // invalid secret, so reject it here with a clear message rather than
    // pump for 4s and falsely report "activation requested".
    if (!passphrase.isEmpty() && !isValidWpaPassphrase(passphrase)) {
        err() << "passphrase must be 8-63 characters (or a 64-character hex PSK)\n";
        err().flush();
        return 64;
    }
    NetworkHost host;
    pump(1500);
    auto* wifi = firstWifiDevice(host);
    if (!wifi) {
        err() << "no Wi-Fi device found\n";
        err().flush();
        return 1;
    }
    AccessPointModel model;
    model.setDevice(wifi);
    host.scanWifi();
    pump(4000);

    // Pick the strongest access point advertising the requested SSID.
    AccessPoint* match = nullptr;
    for (int i = 0; i < model.rowCount(); ++i) {
        auto* ap =
            qobject_cast<AccessPoint*>(model.data(model.index(i), AccessPointModel::AccessPointRole).value<QObject*>());
        if (ap && ap->ssid() == ssid && (!match || ap->strength() > match->strength()))
            match = ap;
    }
    if (!match) {
        err() << "no access point with SSID '" << ssid << "' in range\n";
        err().flush();
        return 1;
    }

    host.connectToAccessPoint(wifi, match, passphrase);
    pump(4000); // give NetworkManager time to add + activate the profile
    const NetworkDevice::DeviceState state = wifi->state();
    out() << "activation requested for '" << ssid
          << "' (security: " << (match->secured() ? match->security() : QStringLiteral("open")) << ")\n";
    out() << "device state: " << stateName(state) << "\n";
    out().flush();
    // connectToAccessPoint is fire-and-forget, so surface a non-zero exit
    // when the device settled into a terminal failure/auth state. This lets
    // callers scripting against the exit code detect a rejected profile
    // (wrong passphrase, auth failure) rather than always reporting success.
    if (state == NetworkDevice::Failed || state == NetworkDevice::NeedAuth)
        return 1;
    return 0;
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
    if (command == QLatin1String("list-devices"))
        return cmdListDevices();
    if (command == QLatin1String("list-connections"))
        return cmdListConnections();
    if (command == QLatin1String("list-aps"))
        return cmdListOrScanAps(/*triggerScan=*/false);
    if (command == QLatin1String("scan"))
        return cmdListOrScanAps(/*triggerScan=*/true);
    if (command == QLatin1String("connect")) {
        if (args.size() < 3)
            return usage();
        return cmdConnect(args.at(2), args.size() > 3 ? args.at(3) : QString());
    }
    return usage();
}
