// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QDBusServiceWatcher>
#include <functional>

class QTimer;
class QProcess;

namespace PlasmaZones {

namespace KCMConstants {
constexpr int DaemonStatusPollIntervalMs = 2000;
constexpr const char* SystemdServiceName = "plasmazones.service";
}

/**
 * @brief Manages the PlasmaZones daemon lifecycle (start/stop/enable/disable)
 *
 * Handles systemd service management, D-Bus service watching, and periodic
 * daemon status polling. Standalone — no dependency on KCM.
 */
class DaemonController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)

public:
    explicit DaemonController(QObject* parent = nullptr);
    ~DaemonController() override;

    bool isRunning() const;
    bool isEnabled() const;
    /// True while an enable/disable chain is in flight. QML binds the
    /// daemon-toggle's `enabled` to `!busy` so a rapid re-click cannot
    /// dispatch a parallel systemctl chain on the same unit, and so the
    /// user gets visible feedback that the previous click is still
    /// landing rather than the switch silently flicking back.
    bool isBusy() const;
    Q_INVOKABLE void setEnabled(bool enabled);

    Q_INVOKABLE void startDaemon();
    Q_INVOKABLE void stopDaemon();

Q_SIGNALS:
    void runningChanged();
    void enabledChanged();
    void busyChanged();

private:
    void checkStatus();
    void refreshEnabledState();
    void setAutostart(bool enabled, std::function<void()> onComplete = nullptr);
    void setChainInFlight(bool inFlight);

    using SystemctlCallback = std::function<void(bool success, const QString& output)>;
    void runSystemctl(const QStringList& args, SystemctlCallback callback = nullptr);

    // m_enabled tracks the systemd "is-enabled" state of plasmazones.service
    // (the on-disk autostart symlink + masked-or-not). m_lastState tracks
    // D-Bus presence — whether the daemon is currently registered on the
    // session bus. They intentionally diverge: a manually-started daemon
    // against a disabled or masked unit is a real configuration, and the
    // UI surfaces "enabled" and "running" as independent signals.
    bool m_enabled = false;
    bool m_lastState = false;
    // Set while an async enable/disable chain is in flight so a rapid
    // QML re-click can't fire a parallel chain on the same systemd unit
    // (parallel systemctl invocations on the same unit have undefined
    // final state). Cleared in the inner-most callback of setAutostart
    // via setChainInFlight(), which also emits busyChanged() so QML can
    // disable the toggle for the chain's duration.
    bool m_chainInFlight = false;
    QTimer* m_checkTimer = nullptr;
    QDBusServiceWatcher* m_watcher = nullptr;
};

} // namespace PlasmaZones
