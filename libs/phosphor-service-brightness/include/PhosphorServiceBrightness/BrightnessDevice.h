// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceBrightness/phosphorservicebrightness_export.h>

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QDBusConnection;

namespace PhosphorServiceBrightness {

/**
 * @brief One brightness-controllable backlight: a display panel (under
 * `/sys/class/backlight`) or a keyboard backlight (a `kbd_backlight` entry
 * under `/sys/class/leds`).
 *
 * The current value is read from the sysfs `brightness` attribute (with
 * `max_brightness` for the range); external changes (hardware keys, another
 * app) are tracked live via a file watcher on the `brightness` attribute.
 * Writes go through logind's
 * `org.freedesktop.login1.Session.SetBrightness(subsystem, name, value)` so no
 * root or udev rule is needed; the cached value is not updated optimistically
 * (it moves when the watcher re-reads), and the write no-ops when no logind
 * session is bound. Owned by `BrightnessHost`; never constructed from QML.
 */
class PHOSPHORSERVICEBRIGHTNESS_EXPORT BrightnessDevice : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(Kind kind READ kind CONSTANT)
    Q_PROPERTY(int brightness READ brightness NOTIFY brightnessChanged)
    Q_PROPERTY(int maxBrightness READ maxBrightness CONSTANT)
    Q_PROPERTY(qreal percentage READ percentage NOTIFY brightnessChanged)

public:
    enum Kind {
        Display, ///< Internal panel backlight (/sys/class/backlight).
        Keyboard, ///< Keyboard backlight (*::kbd_backlight under /sys/class/leds).
        ExternalDisplay, ///< External monitor over DDC/CI (libddcutil); writes do not use logind.
    };
    Q_ENUM(Kind)

    /// Sysfs-backed device (Display / Keyboard): reads sysfs, writes via logind.
    /// The sysfs device name (e.g. `intel_backlight`) is both the human-readable
    /// `name` and the stable unique `id`.
    BrightnessDevice(QDBusConnection connection, QString service, QString sessionPath, Kind kind, QString name,
                     QString sysfsDir, QObject* parent = nullptr);

    /// External-display (DDC/CI) device. `id` is a stable unique key (the I2C
    /// bus, e.g. `i2c-7`); `name` is the monitor's model string, which is NOT
    /// unique (identical monitors share it). The range comes from the initial
    /// DDC probe; `setter` is invoked with the requested raw value
    /// (BrightnessHost routes it to its off-thread DDC worker), and read-backs
    /// are pushed via applyExternalValue. Keeps libddcutil / threading out of
    /// this class.
    BrightnessDevice(QString id, QString name, int brightness, int maxBrightness, std::function<void(int)> setter,
                     QObject* parent = nullptr);

    ~BrightnessDevice() override;

    /// Stable unique identifier for addressing this device (sysfs name for
    /// backlights, I2C bus for external displays). Unlike `name`, never collides.
    [[nodiscard]] QString id() const;
    [[nodiscard]] QString name() const;
    [[nodiscard]] Kind kind() const;
    [[nodiscard]] int brightness() const;
    [[nodiscard]] int maxBrightness() const;
    /// brightness / maxBrightness in [0.0, 1.0]; 0.0 when maxBrightness is 0.
    [[nodiscard]] qreal percentage() const;

    /// Set the raw brightness (clamped to [0, maxBrightness]) via logind.
    Q_INVOKABLE void setBrightness(int value);
    /// Set brightness as a fraction in [0.0, 1.0] of maxBrightness, via logind.
    Q_INVOKABLE void setPercentage(qreal percentage);

    /// Re-read the sysfs attributes now. Called by the watcher on external
    /// changes; also useful for an on-demand refresh. (Sysfs devices only;
    /// external-display values are pushed via applyExternalValue.)
    void refresh();

    /// Push a DDC read-back value for an external-display device. BrightnessHost
    /// calls this from its DDC worker's read signal; updates the cached
    /// brightness, emitting brightnessChanged on a real change.
    void applyExternalValue(int brightness);

    /// Logind session object path used for the SetBrightness write. Set by
    /// BrightnessHost once it resolves the session (the write is inert until
    /// then, or permanently if no session is bound).
    void setSessionPath(const QString& sessionPath);

Q_SIGNALS:
    void brightnessChanged();

private:
    Q_DISABLE_COPY_MOVE(BrightnessDevice)
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceBrightness
