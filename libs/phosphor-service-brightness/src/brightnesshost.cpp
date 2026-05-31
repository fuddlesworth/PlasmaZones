// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBrightness/BrightnessHost.h>

#include <PhosphorServiceBrightness/BrightnessDevice.h>

#include <PhosphorDBus/Client.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcBrightnessHost, "phosphor.service.brightness.host")

namespace {
constexpr auto kLogindService = "org.freedesktop.login1";
constexpr auto kManagerPath = "/org/freedesktop/login1";
constexpr auto kManagerIface = "org.freedesktop.login1.Manager";
constexpr auto kKbdBacklightFunction = "kbd_backlight";
} // namespace

namespace PhosphorServiceBrightness {

class BrightnessHost::Private
{
public:
    BrightnessHost* owner = nullptr;
    QString sysfsRoot;
    QDBusConnection bus;
    QString service;
    QString sessionPath;

    QList<BrightnessDevice*> devices;

    explicit Private(QDBusConnection connection)
        : bus(std::move(connection))
    {
    }

    void enumerate()
    {
        // Display panels: every entry under <root>/class/backlight.
        const QDir backlightDir(QDir(sysfsRoot).filePath(QStringLiteral("class/backlight")));
        const auto backlights = backlightDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& entry : backlights)
            addDevice(BrightnessDevice::Display, entry, backlightDir.filePath(entry));

        // Keyboard backlights: only the *::kbd_backlight entries under
        // <root>/class/leds. The led name is "device:color:function"; the
        // function segment (after the last ':') distinguishes a keyboard
        // backlight from indicator LEDs (capslock, power, RGB, ...), which are
        // not brightness controls and are skipped.
        const QDir ledsDir(QDir(sysfsRoot).filePath(QStringLiteral("class/leds")));
        const auto leds = ledsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& entry : leds) {
            if (entry.section(QLatin1Char(':'), -1) == QLatin1String(kKbdBacklightFunction))
                addDevice(BrightnessDevice::Keyboard, entry, ledsDir.filePath(entry));
        }
    }

    void addDevice(BrightnessDevice::Kind kind, const QString& name, const QString& sysfsDir)
    {
        devices.append(new BrightnessDevice(bus, service, sessionPath, kind, name, sysfsDir, owner));
    }

    // Resolve the caller's logind session object path, then propagate it to the
    // already-enumerated devices so their SetBrightness writes can target it.
    void resolveSession()
    {
        PhosphorDBus::Client manager(bus, service, QLatin1String(kManagerPath), &lcBrightnessHost());
        auto* watcher = new QDBusPendingCallWatcher(
            manager.asyncCall(QLatin1String(kManagerIface), QStringLiteral("GetSessionByPID"),
                              {static_cast<uint>(QCoreApplication::applicationPid())}),
            owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QDBusObjectPath> reply = *call;
            if (reply.isError()) {
                qCDebug(lcBrightnessHost)
                    << "GetSessionByPID failed; brightness writes inert:" << reply.error().message();
                return;
            }
            sessionPath = reply.value().path();
            for (auto* device : std::as_const(devices))
                device->setSessionPath(sessionPath);
        });
    }
};

BrightnessHost::BrightnessHost(QObject* parent)
    : BrightnessHost(QStringLiteral("/sys"), QDBusConnection::systemBus(), QLatin1String(kLogindService), QString(),
                     parent)
{
}

BrightnessHost::BrightnessHost(QString sysfsRoot, QDBusConnection connection, QString service, QString sessionPath,
                               QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(std::move(connection)))
{
    d->owner = this;
    d->sysfsRoot = std::move(sysfsRoot);
    d->service = std::move(service);
    d->sessionPath = std::move(sessionPath);

    d->enumerate();

    // Resolve the session only when one wasn't supplied; an injected path is
    // used as-is (tests, or a consumer that already knows its session).
    if (d->sessionPath.isEmpty() && d->bus.isConnected())
        d->resolveSession();
}

BrightnessHost::~BrightnessHost() = default;

QList<BrightnessDevice*> BrightnessHost::devices() const
{
    return d->devices;
}

int BrightnessHost::deviceCount() const
{
    return static_cast<int>(d->devices.size());
}

BrightnessDevice* BrightnessHost::deviceAt(int index) const
{
    if (index < 0 || index >= d->devices.size())
        return nullptr;
    return d->devices.at(index);
}

} // namespace PhosphorServiceBrightness
