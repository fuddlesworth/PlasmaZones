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

public:
    explicit DaemonController(QObject* parent = nullptr);
    ~DaemonController() override;

    bool isRunning() const;
    bool isEnabled() const;
    Q_INVOKABLE void setEnabled(bool enabled);

    Q_INVOKABLE void startDaemon();
    Q_INVOKABLE void stopDaemon();

Q_SIGNALS:
    void runningChanged();
    void enabledChanged();

private:
    void checkStatus();
    void refreshEnabledState();
    void setAutostart(bool enabled, std::function<void()> onComplete = nullptr);

    using SystemctlCallback = std::function<void(bool success, const QString& output)>;
    void runSystemctl(const QStringList& args, SystemctlCallback callback = nullptr);

    bool m_enabled = false;
    bool m_lastState = false;
    // Set while an async enable/disable chain is in flight so a rapid
    // QML re-click can't fire a parallel chain on the same systemd unit
    // (parallel systemctl invocations on the same unit have undefined
    // final state). Cleared in the inner-most callback of setAutostart.
    bool m_chainInFlight = false;
    QTimer* m_checkTimer = nullptr;
    QDBusServiceWatcher* m_watcher = nullptr;
};

} // namespace PlasmaZones
