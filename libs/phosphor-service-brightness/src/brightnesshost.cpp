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
#include <QHash>
#include <QLoggingCategory>

#ifdef PHOSPHORSERVICEBRIGHTNESS_HAVE_DDCUTIL
#include "ddccontroller.h"

#include <QThread>
#endif

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

#ifdef PHOSPHORSERVICEBRIGHTNESS_HAVE_DDCUTIL
    QThread* ddcThread = nullptr;
    DdcController* ddc = nullptr;
    QHash<QString, BrightnessDevice*> ddcDevices;
#endif

    explicit Private(QDBusConnection connection)
        : bus(std::move(connection))
    {
    }

    ~Private()
    {
#ifdef PHOSPHORSERVICEBRIGHTNESS_HAVE_DDCUTIL
        if (ddcThread) {
            // Stop the worker; the controller is deleted via the thread's
            // finished -> deleteLater. wait() guarantees the thread is gone
            // before this Private (and the host's device children) tear down.
            ddcThread->quit();
            ddcThread->wait();
            delete ddcThread;
        }
#endif
    }

    void appendDevice(BrightnessDevice* device)
    {
        devices.append(device);
        Q_EMIT owner->deviceAdded(device);
        Q_EMIT owner->deviceCountChanged();
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
        // not brightness controls and are skipped. The colon-delimited form is
        // required, so a bare "kbd_backlight" with no device prefix is rejected.
        const QDir ledsDir(QDir(sysfsRoot).filePath(QStringLiteral("class/leds")));
        const auto leds = ledsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& entry : leds) {
            if (entry.contains(QLatin1Char(':'))
                && entry.section(QLatin1Char(':'), -1) == QLatin1String(kKbdBacklightFunction))
                addDevice(BrightnessDevice::Keyboard, entry, ledsDir.filePath(entry));
        }
    }

    void addDevice(BrightnessDevice::Kind kind, const QString& name, const QString& sysfsDir)
    {
        appendDevice(new BrightnessDevice(bus, service, sessionPath, kind, name, sysfsDir, owner));
    }

#ifdef PHOSPHORSERVICEBRIGHTNESS_HAVE_DDCUTIL
    // Start the off-thread DDC/CI worker and enumerate external displays. They
    // surface asynchronously via deviceAdded as the worker reports them.
    void startDdc()
    {
        ddcThread = new QThread;
        ddc = new DdcController; // no parent; owned by the worker thread
        ddc->moveToThread(ddcThread);
        QObject::connect(ddcThread, &QThread::finished, ddc, &QObject::deleteLater);

        // Both handlers below use `owner` (the host, GUI thread) as the context
        // object, so they run on the GUI thread: ddcDevices is touched only
        // there, never from the worker, and needs no synchronization.
        QObject::connect(ddc, &DdcController::displayFound, owner,
                         [this](const QString& id, const QString& name, int brightness, int maxValue) {
                             if (ddcDevices.contains(id))
                                 return;
                             DdcController* controller = ddc;
                             auto* device = new BrightnessDevice(
                                 id, name, brightness, maxValue,
                                 [controller, id](int value) {
                                     QMetaObject::invokeMethod(controller, "setBrightness", Qt::QueuedConnection,
                                                               Q_ARG(QString, id), Q_ARG(int, value));
                                 },
                                 owner);
                             ddcDevices.insert(id, device);
                             appendDevice(device);
                         });
        QObject::connect(ddc, &DdcController::brightnessRead, owner, [this](const QString& id, int brightness) {
            if (auto* device = ddcDevices.value(id))
                device->applyExternalValue(brightness);
            else
                qCDebug(lcBrightnessHost) << "DDC read-back for unknown display dropped:" << id;
        });

        ddcThread->start();
        QMetaObject::invokeMethod(ddc, "enumerate", Qt::QueuedConnection);
    }
#endif

    // True if any enumerated device writes through logind (the sysfs-backed
    // Display / Keyboard backlights). External-display (DDC/CI) devices write
    // over I2C and need no session, so a box with only those (or none) never
    // resolves a session.
    [[nodiscard]] bool needsLogindSession() const
    {
        for (auto* device : devices) {
            if (device->kind() == BrightnessDevice::Display || device->kind() == BrightnessDevice::Keyboard)
                return true;
        }
        return false;
    }

    // Resolve the user's active *graphical* session object path, then propagate
    // it to the devices so their SetBrightness writes can target it. The
    // graphical session is named by XDG_SESSION_ID; resolving by it (rather
    // than the caller's PID) is correct even when this process runs outside the
    // session's cgroup (a --user service, a multiplexer, ssh). GetSessionByPID
    // is the fallback when XDG_SESSION_ID is unset.
    void resolveSession()
    {
        PhosphorDBus::Client manager(bus, service, QLatin1String(kManagerPath), &lcBrightnessHost());
        const QString xdgSessionId = qEnvironmentVariable("XDG_SESSION_ID");
        const QDBusPendingCall pending = xdgSessionId.isEmpty()
            ? manager.asyncCall(QLatin1String(kManagerIface), QStringLiteral("GetSessionByPID"),
                                {static_cast<uint>(QCoreApplication::applicationPid())})
            : manager.asyncCall(QLatin1String(kManagerIface), QStringLiteral("GetSession"), {xdgSessionId});
        auto* watcher = new QDBusPendingCallWatcher(pending, owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QDBusObjectPath> reply = *call;
            if (reply.isError()) {
                qCDebug(lcBrightnessHost)
                    << "logind session lookup failed; brightness writes inert:" << reply.error().message();
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
#ifdef PHOSPHORSERVICEBRIGHTNESS_HAVE_DDCUTIL
    // Production wiring only: probe real external displays. The injectable
    // ctor (tests / consumers passing a fake sysfs root) does NOT auto-start
    // DDC, so tests never touch real I2C / the tester's monitors.
    d->startDdc();
#endif
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

    // Resolve the session only when one wasn't supplied AND a device actually
    // needs it (a sysfs backlight). An injected path is used as-is (tests, or a
    // consumer that already knows its session); a box with no logind-written
    // devices issues no lookup.
    if (d->sessionPath.isEmpty() && d->bus.isConnected() && d->needsLogindSession())
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
