// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QDBusServiceWatcher>
#include <functional>

class QTimer;
class QProcess;

namespace PlasmaZones {

class KCMPlasmaZones;

/**
 * @brief Manages the PlasmaZones daemon lifecycle (start/stop/enable/disable)
 *
 * Handles systemd service management, D-Bus service watching, and periodic
 * daemon status polling. Uses the back-pointer pattern to signal the KCM.
 */
class DaemonController : public QObject
{
    Q_OBJECT

public:
    explicit DaemonController(KCMPlasmaZones* kcm, QObject* parent = nullptr);

    bool isRunning() const;
    bool isEnabled() const;
    void setEnabled(bool enabled);

Q_SIGNALS:
    void runningChanged();
    void enabledChanged();

private:
    void checkStatus();
    void refreshEnabledState();
    void setAutostart(bool enabled);
    void startDaemon();
    void stopDaemon();

    using SystemctlCallback = std::function<void(bool success, const QString& output)>;
    void runSystemctl(const QStringList& args, SystemctlCallback callback = nullptr);

    KCMPlasmaZones* m_kcm = nullptr;
    bool m_enabled = true;
    bool m_lastState = false;
    QTimer* m_checkTimer = nullptr;
    QDBusServiceWatcher* m_watcher = nullptr;
};

} // namespace PlasmaZones
