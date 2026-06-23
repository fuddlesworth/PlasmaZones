// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-service-brightness-cli: headless acceptance harness + worked
// example for the phosphor-service-brightness library. Drives the lib
// directly against real sysfs backlights and logind, the same pattern as the
// sibling phosphor-service-* CLIs.
//
// Sysfs backlights enumerate synchronously, but the logind session and the
// off-thread DDC/CI external-display enumeration both resolve asynchronously
// (bus probing takes seconds), so every command pumps the event loop for a
// generous settle window before reading, and `set` pumps again afterwards so
// the watcher can pick up the new value.

#include <PhosphorServiceBrightness/BrightnessDevice.h>
#include <PhosphorServiceBrightness/BrightnessHost.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <cmath>

using namespace PhosphorServiceBrightness;

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

// Run the event loop for `ms` so the async logind session resolution and the
// off-thread DDC/CI enumeration can settle. DDC enumeration probes I2C buses
// and is slow (seconds), so the settle window is generous.
constexpr int kSettleMs = 3000;

void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QString kindName(BrightnessDevice::Kind kind)
{
    switch (kind) {
    case BrightnessDevice::Keyboard:
        return QStringLiteral("keyboard");
    case BrightnessDevice::ExternalDisplay:
        return QStringLiteral("external");
    case BrightnessDevice::Display:
        break;
    }
    return QStringLiteral("display");
}

// Address devices by their stable unique id, not the display name: identical
// external monitors share a model name, so a name lookup is ambiguous.
BrightnessDevice* findById(const BrightnessHost& host, const QString& id)
{
    const auto devices = host.devices();
    for (auto* device : devices) {
        if (device->id() == id)
            return device;
    }
    return nullptr;
}

void printDevice(BrightnessDevice* device)
{
    out() << device->id() << "\t" << device->name() << "\t(" << kindName(device->kind()) << ")\t"
          << device->brightness() << " / " << device->maxBrightness() << "\t" << qRound(device->percentage() * 100.0)
          << "%\n";
}

int cmdList()
{
    BrightnessHost host;
    pump(kSettleMs);
    const auto devices = host.devices();
    if (devices.isEmpty())
        out() << "(no brightness devices)\n";
    for (auto* device : devices)
        printDevice(device);
    out().flush();
    return 0;
}

int cmdGet(const QString& id)
{
    BrightnessHost host;
    pump(kSettleMs);
    BrightnessDevice* device = findById(host, id);
    if (!device) {
        err() << "no brightness device with id '" << id << "'\n";
        err().flush();
        return 1;
    }
    printDevice(device);
    out().flush();
    return 0;
}

int cmdSet(const QString& id, const QString& value)
{
    BrightnessHost host;
    pump(kSettleMs); // let logind + DDC enumeration settle before the write
    BrightnessDevice* device = findById(host, id);
    if (!device) {
        err() << "no brightness device with id '" << id << "'\n";
        err().flush();
        return 1;
    }

    bool ok = false;
    if (value.endsWith(QLatin1Char('%'))) {
        const double pct = value.left(value.size() - 1).toDouble(&ok);
        // !isfinite rejects "nan" / "inf": NaN passes every range comparison
        // (all false), so without this it would reach setPercentage(NaN).
        if (!ok || !std::isfinite(pct) || pct < 0.0 || pct > 100.0) {
            err() << "percentage must be 0-100\n";
            err().flush();
            return 64;
        }
        device->setPercentage(pct / 100.0);
    } else {
        const int raw = value.toInt(&ok);
        if (!ok || raw < 0) {
            err() << "brightness must be a non-negative integer (or N%)\n";
            err().flush();
            return 64;
        }
        device->setBrightness(raw);
    }

    pump(2000); // give logind/DDC time to apply and read back
    printDevice(device);
    out().flush();
    return 0;
}

// Exit codes follow the sysexits convention the sibling CLIs use: 0 ok,
// 64 usage error, 1 runtime failure.
int usage()
{
    err() << "usage: phosphor-service-brightness-cli <command> [args]\n"
          << "  list                all brightness devices (display + keyboard + external)\n"
          << "  get <id>            show one device's brightness (id is the first list column)\n"
          << "  set <id> <value>    set raw brightness, or a percentage with a trailing %\n";
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
    if (command == QLatin1String("list"))
        return cmdList();
    if (command == QLatin1String("get")) {
        if (args.size() < 3)
            return usage();
        return cmdGet(args.at(2));
    }
    if (command == QLatin1String("set")) {
        if (args.size() < 4)
            return usage();
        return cmdSet(args.at(2), args.at(3));
    }
    return usage();
}
