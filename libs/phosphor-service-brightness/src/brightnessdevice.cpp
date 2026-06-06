// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBrightness/BrightnessDevice.h>

#include <PhosphorDBus/Client.h>

#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcBrightnessDevice, "phosphor.service.brightness.device")

namespace {
constexpr auto kSessionIface = "org.freedesktop.login1.Session";

// Read a single integer from a sysfs attribute file. Returns fallback when the
// file is missing or unparseable (a malformed entry must not crash the lib).
int readSysfsInt(const QString& path, int fallback)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fallback;
    bool ok = false;
    const int value = file.readLine().trimmed().toInt(&ok);
    return ok ? value : fallback;
}
} // namespace

namespace PhosphorServiceBrightness {

class BrightnessDevice::Private
{
public:
    BrightnessDevice* owner = nullptr;
    QDBusConnection bus;
    QString service;
    QString sessionPath;
    Kind kind = Display;
    QString id;
    QString name;
    QString sysfsDir;

    int brightness = 0;
    int maxBrightness = 0;

    // Sysfs (Display / Keyboard) only.
    QFileSystemWatcher watcher;
    // External-display (DDC/CI) only: routes a raw set to the host's worker.
    std::function<void(int)> externalSetter;

    explicit Private(QDBusConnection connection)
        : bus(std::move(connection))
    {
    }

    [[nodiscard]] QString brightnessPath() const
    {
        return QDir(sysfsDir).filePath(QStringLiteral("brightness"));
    }

    // logind's SetBrightness subsystem string: backlight devices live under
    // /sys/class/backlight, keyboard backlights under /sys/class/leds.
    [[nodiscard]] QString subsystem() const
    {
        return kind == Keyboard ? QStringLiteral("leds") : QStringLiteral("backlight");
    }

    void readMax()
    {
        // max_brightness is the upper bound of the range; the live value is read
        // separately from the writable brightness attribute (see readBrightness).
        maxBrightness = readSysfsInt(QDir(sysfsDir).filePath(QStringLiteral("max_brightness")), 0);
    }

    void readBrightness()
    {
        const int previous = brightness;
        brightness = readSysfsInt(brightnessPath(), brightness);
        if (brightness != previous)
            Q_EMIT owner->brightnessChanged();
    }
};

BrightnessDevice::BrightnessDevice(QDBusConnection connection, QString service, QString sessionPath, Kind kind,
                                   QString name, QString sysfsDir, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(std::move(connection)))
{
    d->owner = this;
    d->service = std::move(service);
    d->sessionPath = std::move(sessionPath);
    d->kind = kind;
    d->name = std::move(name);
    // The sysfs device name is already unique, so it doubles as the id.
    d->id = d->name;
    d->sysfsDir = std::move(sysfsDir);

    d->readMax();
    d->brightness = readSysfsInt(d->brightnessPath(), 0);

    // Watch the brightness attribute so hardware keys / other apps are tracked
    // live. QFileSystemWatcher drops the path on some change events, so the
    // handler re-adds it after re-reading.
    if (d->watcher.addPath(d->brightnessPath())) {
        connect(&d->watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
            d->readBrightness();
            if (!d->watcher.files().contains(path) && !d->watcher.addPath(path))
                qCDebug(lcBrightnessDevice) << "lost brightness watch for" << d->name << "; external changes untracked";
        });
    } else {
        qCDebug(lcBrightnessDevice) << "could not watch brightness for" << d->name << "; external changes untracked";
    }
}

BrightnessDevice::BrightnessDevice(QString id, QString name, int brightness, int maxBrightness,
                                   std::function<void(int)> setter, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(QDBusConnection(QString())))
{
    d->owner = this;
    d->kind = ExternalDisplay;
    d->id = std::move(id);
    d->name = std::move(name);
    d->maxBrightness = maxBrightness;
    // Clamp the initial probe value into range, in case the DDC read reported a
    // transiently inconsistent current/max pair (matches applyExternalValue).
    d->brightness = maxBrightness > 0 ? std::clamp(brightness, 0, maxBrightness) : brightness;
    d->externalSetter = std::move(setter);
    // No sysfs watcher: DDC/CI has no change notification; the host pushes
    // read-backs via applyExternalValue.
}

BrightnessDevice::~BrightnessDevice() = default;

QString BrightnessDevice::id() const
{
    return d->id;
}
QString BrightnessDevice::name() const
{
    return d->name;
}
BrightnessDevice::Kind BrightnessDevice::kind() const
{
    return d->kind;
}
int BrightnessDevice::brightness() const
{
    return d->brightness;
}
int BrightnessDevice::maxBrightness() const
{
    return d->maxBrightness;
}
qreal BrightnessDevice::percentage() const
{
    if (d->maxBrightness <= 0)
        return 0.0;
    // Clamp to honor the documented [0.0, 1.0] contract for every source: a
    // sysfs read is not clamped into the cached value, so a (kernel-level)
    // brightness above max_brightness must not leak a percentage above 1.0.
    return std::clamp(static_cast<qreal>(d->brightness) / d->maxBrightness, 0.0, 1.0);
}

void BrightnessDevice::setBrightness(int value)
{
    if (d->maxBrightness <= 0)
        return;
    const int clamped = std::clamp(value, 0, d->maxBrightness);
    if (d->kind == ExternalDisplay) {
        // DDC/CI write: hand the raw value to the host's off-thread worker. No
        // optimistic update; the cached value moves on the worker's read-back.
        if (d->externalSetter)
            d->externalSetter(clamped);
        return;
    }
    if (d->sessionPath.isEmpty() || !d->bus.isConnected()) {
        qCDebug(lcBrightnessDevice) << "no logind session; brightness write inert for" << d->name;
        return;
    }
    // Fire-and-forget; the cached value moves when the watcher re-reads the
    // sysfs attribute, never optimistically.
    PhosphorDBus::Client session(d->bus, d->service, d->sessionPath, &lcBrightnessDevice());
    session.fireAndForget(this, QLatin1String(kSessionIface), QStringLiteral("SetBrightness"),
                          {d->subsystem(), d->name, static_cast<uint>(clamped)}, QStringLiteral("SetBrightness"));
}

void BrightnessDevice::setPercentage(qreal percentage)
{
    // Reject a non-finite percentage (NaN / inf): std::clamp would pass NaN
    // through and qRound(NaN) is undefined. This is a public Q_INVOKABLE, so a
    // QML caller can reach it too.
    if (d->maxBrightness <= 0 || !std::isfinite(percentage))
        return;
    const qreal clamped = std::clamp(percentage, 0.0, 1.0);
    // qRound(qreal) already yields an int; the clamped fraction times the int
    // range can never exceed maxBrightness, so no extra cast is needed.
    setBrightness(qRound(clamped * d->maxBrightness));
}

void BrightnessDevice::refresh()
{
    if (d->kind != ExternalDisplay)
        d->readBrightness();
}

void BrightnessDevice::applyExternalValue(int brightness)
{
    // A zero/invalid range has no controllable value; drop the read-back rather
    // than cache an unclamped value (matches the setBrightness / setPercentage
    // guards).
    if (d->maxBrightness <= 0)
        return;
    const int clamped = std::clamp(brightness, 0, d->maxBrightness);
    if (clamped == d->brightness)
        return;
    d->brightness = clamped;
    Q_EMIT brightnessChanged();
}

void BrightnessDevice::setSessionPath(const QString& sessionPath)
{
    d->sessionPath = sessionPath;
}

} // namespace PhosphorServiceBrightness
