// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceBrightness/phosphorservicebrightness_export.h>

#include <QList>
#include <QObject>
#include <QString>

#include <memory>

class QDBusConnection;

namespace PhosphorServiceBrightness {

class BrightnessDevice;

/**
 * @brief Enumerates the brightness-controllable backlights under a sysfs root.
 *
 * Scans `<root>/class/backlight` (display panels) and the `kbd_backlight`
 * entries under `<root>/class/leds` (keyboard backlights); every other
 * `/sys/class/leds` entry is an indicator, not a brightness control, and is
 * skipped. The device set is enumerated once at construction
 * (backlights do not hotplug); per-device values update live via each
 * device's file watcher.
 *
 * Writes route through logind, so the host resolves the session object path
 * (asynchronously: `GetSession` for `XDG_SESSION_ID`, falling back to
 * `GetSessionByPID` when that is unset; or injected) and hands it to the
 * devices. Inert when the sysfs root is absent (empty list, no crash);
 * read-only when no logind session is bound.
 *
 * The sysfs root and the `(connection, service, sessionPath)` are injectable
 * so the whole read + write surface is testable against a fake sysfs tree and
 * a fake logind with no root and no daemon.
 */
class PHOSPHORSERVICEBRIGHTNESS_EXPORT BrightnessHost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY deviceCountChanged)

public:
    /// Production wiring: the real sysfs root (`/sys`), the system bus, and the
    /// logind session resolved from the calling process.
    explicit BrightnessHost(QObject* parent = nullptr);

    /// Injectable wiring for tests / advanced consumers. A non-empty
    /// @p sessionPath is used as-is; an empty one is resolved asynchronously
    /// from `XDG_SESSION_ID` (or the calling process via `GetSessionByPID` when
    /// that is unset), when @p connection is connected.
    BrightnessHost(QString sysfsRoot, QDBusConnection connection, QString service, QString sessionPath,
                   QObject* parent = nullptr);

    ~BrightnessHost() override;

    [[nodiscard]] QList<BrightnessDevice*> devices() const;
    [[nodiscard]] int deviceCount() const;
    [[nodiscard]] Q_INVOKABLE PhosphorServiceBrightness::BrightnessDevice* deviceAt(int index) const;

Q_SIGNALS:
    /// Sysfs devices are added synchronously at construction; external displays
    /// (DDC/CI) arrive asynchronously after the worker enumerates them.
    void deviceAdded(PhosphorServiceBrightness::BrightnessDevice* device);
    /// The counterpart to deviceAdded, for dynamic sources that can drop a
    /// device at runtime. The current sources are stable for the host's lifetime
    /// (sysfs is enumerated once; DDC/CI enumeration is one-shot), so the host
    /// does not emit this today; consumers (e.g. BrightnessDeviceModel) handle
    /// it so a future hot-removal source needs no consumer changes.
    void deviceRemoved(PhosphorServiceBrightness::BrightnessDevice* device);
    void deviceCountChanged();

private:
    Q_DISABLE_COPY_MOVE(BrightnessHost)
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceBrightness
