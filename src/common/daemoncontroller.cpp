// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemoncontroller.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QProcess>
#include <QTimer>
#include "../../src/core/constants.h"
#include "../../src/core/logging.h"

namespace PlasmaZones {

DaemonController::DaemonController(QObject* parent)
    : QObject(parent)
{
    // Set up daemon status polling (fallback for edge cases the watcher might miss)
    m_checkTimer = new QTimer(this);
    m_checkTimer->setInterval(KCMConstants::DaemonStatusPollIntervalMs);
    connect(m_checkTimer, &QTimer::timeout, this, &DaemonController::checkStatus);
    m_checkTimer->start();

    // Set up D-Bus service watcher for immediate daemon start/stop notification
    m_watcher = new QDBusServiceWatcher(
        QString(DBus::ServiceName), QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);

    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        if (!m_lastState) {
            m_lastState = true;
            Q_EMIT runningChanged();
        }
        // Re-check enabled state so other DaemonController instances stay in sync
        refreshEnabledState();
    });

    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        if (m_lastState) {
            m_lastState = false;
            Q_EMIT runningChanged();
        }
        // Re-check enabled state so other DaemonController instances stay in sync
        refreshEnabledState();
    });

    // Load initial state
    m_lastState = isRunning();
    m_enabled = m_lastState; // Assume enabled if running, will be corrected async
    refreshEnabledState();
}

DaemonController::~DaemonController()
{
    // Kill any running systemctl processes before destruction to prevent
    // QProcess::finished callbacks from firing with a dangling 'this'.
    const auto children = findChildren<QProcess*>();
    for (auto* proc : children) {
        proc->disconnect();
        if (proc->state() != QProcess::NotRunning) {
            proc->kill();
            proc->waitForFinished(500);
        }
    }
}

bool DaemonController::isRunning() const
{
    auto* iface = QDBusConnection::sessionBus().interface();
    if (!iface) {
        return false;
    }
    return iface->isServiceRegistered(QString(DBus::ServiceName));
}

bool DaemonController::isEnabled() const
{
    return m_enabled;
}

void DaemonController::setEnabled(bool enabled)
{
    // Always perform start/stop — the toggle tracks running state which
    // can diverge from the systemd enabled state (e.g. manually started
    // while the service is disabled).
    setAutostart(enabled);

    if (enabled) {
        startDaemon();
    } else {
        stopDaemon();
    }

    if (m_enabled != enabled) {
        m_enabled = enabled;
        Q_EMIT enabledChanged();
    }
}

void DaemonController::checkStatus()
{
    bool currentState = isRunning();
    if (currentState != m_lastState) {
        m_lastState = currentState;
        Q_EMIT runningChanged();
    }
}

void DaemonController::refreshEnabledState()
{
    runSystemctl(
        {QStringLiteral("--user"), QStringLiteral("is-enabled"), QLatin1String(KCMConstants::SystemdServiceName)},
        [this](bool /*success*/, const QString& output) {
            // systemctl is-enabled returns: enabled, enabled-runtime, static,
            // linked, indirect, disabled, masked, not-found, etc.
            bool enabled = output.startsWith(QLatin1String("enabled"));
            if (m_enabled != enabled) {
                m_enabled = enabled;
                Q_EMIT enabledChanged();
            }
        });
}

void DaemonController::setAutostart(bool enabled)
{
    QString action = enabled ? QStringLiteral("enable") : QStringLiteral("disable");
    runSystemctl({QStringLiteral("--user"), action, QLatin1String(KCMConstants::SystemdServiceName)},
                 [this](bool success, const QString& /*output*/) {
                     if (success) {
                         refreshEnabledState();
                     }
                 });
}

void DaemonController::startDaemon()
{
    if (isRunning()) {
        return;
    }
    // Clear any stuck failed-state before issuing start. This is needed when
    // the user has run plasmazonesd manually (e.g. from a terminal for
    // logging), which causes systemd's own managed instance to lose the
    // D-Bus name race and exit; Restart=on-failure then retries until the
    // unit's StartLimitBurst is exhausted, leaving it wedged in "failed"
    // until a session restart or manual `systemctl reset-failed`. Without
    // this, toggling PlasmaZones back on from the settings app silently
    // does nothing, which was reported in discussion #271.
    //
    // reset-failed is a no-op on units not in failed state, so it's safe
    // to issue unconditionally. Chain the start via callback so it only
    // runs after reset completes — otherwise the two systemctl processes
    // race and the start can still arrive at a failed unit.
    runSystemctl(
        {QStringLiteral("--user"), QStringLiteral("reset-failed"), QLatin1String(KCMConstants::SystemdServiceName)},
        [this](bool /*success*/, const QString& /*output*/) {
            // Ignore reset-failed's exit code: the unit may not
            // exist yet (service file freshly installed, user's
            // systemd hasn't scanned) which is fine — start will
            // handle that error path via its own warning.
            //
            // Re-check isRunning() here: the D-Bus service watcher
            // may have brought the daemon up between the entry
            // check and this callback (e.g. a parallel toggle from
            // another settings window, or systemd's own restart
            // path firing concurrently). Issuing `start` against
            // an already-active unit is harmless but emits a
            // "Unit is already active" warning; skipping is cleaner.
            if (isRunning()) {
                return;
            }
            runSystemctl(
                {QStringLiteral("--user"), QStringLiteral("start"), QLatin1String(KCMConstants::SystemdServiceName)});
        });
}

void DaemonController::stopDaemon()
{
    if (!isRunning()) {
        return;
    }
    runSystemctl({QStringLiteral("--user"), QStringLiteral("stop"), QLatin1String(KCMConstants::SystemdServiceName)});
}

void DaemonController::runSystemctl(const QStringList& args, SystemctlCallback callback)
{
    auto* proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [proc, callback, args](int exitCode, QProcess::ExitStatus status) {
        bool success = (status == QProcess::NormalExit && exitCode == 0);
        QString output = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        if (!success) {
            QString errorOutput = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            qCWarning(lcKcm) << "systemctl" << args << "failed:" << errorOutput;
        }
        if (callback) {
            callback(success, output);
        }
        proc->deleteLater();
    });
    proc->start(QStringLiteral("systemctl"), args);
}

} // namespace PlasmaZones
