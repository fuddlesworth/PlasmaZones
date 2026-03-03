// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemoncontroller.h"
#include "kcm_plasmazones.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QProcess>
#include <QTimer>
#include "../src/core/constants.h"
#include "../src/core/logging.h"

namespace PlasmaZones {

DaemonController::DaemonController(KCMPlasmaZones* kcm, QObject* parent)
    : QObject(parent)
    , m_kcm(kcm)
{
    // Set up daemon status polling (fallback for edge cases the watcher might miss)
    m_checkTimer = new QTimer(this);
    m_checkTimer->setInterval(KCMConstants::DaemonStatusPollIntervalMs);
    connect(m_checkTimer, &QTimer::timeout, this, &DaemonController::checkStatus);
    m_checkTimer->start();

    // Set up D-Bus service watcher for immediate daemon start/stop notification
    m_watcher = new QDBusServiceWatcher(
        QString(DBus::ServiceName),
        QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
        this);

    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        if (!m_lastState) {
            m_lastState = true;
            Q_EMIT runningChanged();
        }
    });

    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        if (m_lastState) {
            m_lastState = false;
            Q_EMIT runningChanged();
        }
    });

    // Load initial state
    m_lastState = isRunning();
    m_enabled = m_lastState; // Assume enabled if running, will be corrected async
    refreshEnabledState();
}

bool DaemonController::isRunning() const
{
    return QDBusConnection::sessionBus().interface()->isServiceRegistered(QString(DBus::ServiceName));
}

bool DaemonController::isEnabled() const
{
    return m_enabled;
}

void DaemonController::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;

    // Update systemd service enabled state
    setAutostart(enabled);

    // Start or stop daemon immediately
    if (enabled) {
        startDaemon();
    } else {
        stopDaemon();
    }

    Q_EMIT enabledChanged();
}

void DaemonController::checkStatus()
{
    bool currentState = isRunning();
    if (currentState != m_lastState) {
        m_lastState = currentState;
        Q_EMIT runningChanged();
        // Note: When daemon starts, we rely on the daemonReady D-Bus signal
        // to trigger loadLayouts(). No polling needed here.
    }
}

void DaemonController::refreshEnabledState()
{
    runSystemctl(
        {QStringLiteral("--user"), QStringLiteral("is-enabled"), QLatin1String(KCMConstants::SystemdServiceName)},
        [this](bool /*success*/, const QString& output) {
            bool enabled = (output == QLatin1String("enabled"));
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
    runSystemctl({QStringLiteral("--user"), QStringLiteral("start"), QLatin1String(KCMConstants::SystemdServiceName)});
    // Layouts will be loaded when daemonReady D-Bus signal is received
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
