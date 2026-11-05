// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemoncontroller.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QProcess>
#include <QTimer>
#include "core/types/constants.h"
#include "core/platform/logging.h"
#include <PhosphorProtocol/ServiceConstants.h>

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
        QString(PhosphorProtocol::Service::Name), QDBusConnection::sessionBus(),
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

    // Load initial state. The header default for m_enabled is false; the
    // async refreshEnabledState() below is the sole source of truth and
    // will emit enabledChanged() iff the unit's actual on-disk state
    // differs. No optimistic guess from isRunning() — a running daemon
    // that was manually started against a disabled/masked unit is a real
    // configuration and "running" does not imply "enabled".
    m_lastState = isRunning();
    refreshEnabledState();
}

DaemonController::~DaemonController()
{
    // Tear down any running systemctl processes before destruction. The Qt
    // connect() calls use `this` as receiver context, so QObject teardown
    // already disconnects the lambdas — the explicit disconnect() here is
    // belt-and-suspenders. The kill() + waitForFinished() pair stops any
    // process that would otherwise outlive the QObject parent and reparent
    // itself to the event loop's deletion queue.
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
    return iface->isServiceRegistered(QString(PhosphorProtocol::Service::Name));
}

bool DaemonController::isEnabled() const
{
    return m_enabled;
}

bool DaemonController::isBusy() const
{
    return m_chainInFlight;
}

void DaemonController::setChainInFlight(bool inFlight)
{
    if (m_chainInFlight == inFlight) {
        return;
    }
    m_chainInFlight = inFlight;
    Q_EMIT busyChanged();
}

void DaemonController::setEnabled(bool enabled)
{
    // setAutostart does unmask+enable / disable+mask (see the rationale
    // there). Both sides chain the side-effect through onComplete so the
    // systemctl invocations run strictly sequenced and we never race:
    //   enable:  unmask → enable → start  (start needs the unit unmasked)
    //   disable: disable → mask → stop    (stop happens AFTER mask so a
    //                                      concurrent D-Bus activation
    //                                      during the shutdown window
    //                                      hits a masked unit and is
    //                                      refused — closes the
    //                                      re-activation race)
    //
    // No optimistic m_enabled flip here: refreshEnabledState() in
    // setAutostart's tail reads the unit's real on-disk state and emits
    // enabledChanged() iff it actually changed. Flipping the property
    // synchronously before systemctl confirms would lie to QML for the
    // length of the chain (and stick a wrong value if the chain failed).
    //
    // Re-entrancy guard: dropping the optimistic flip means QML can re-fire
    // setEnabled() while the previous chain is still in flight. Running
    // parallel systemctl invocations on the same unit leaves the final
    // state ordering-dependent. Drop overlapping requests and let the
    // first chain land — refreshEnabledState() will then push the real
    // state to QML, and the user can retry if it still doesn't match.
    if (m_chainInFlight) {
        // qCWarning rather than qCInfo: a dropped toggle is a user-visible
        // regression (the QML switch flicks back), not a debug curiosity,
        // and it is the symptom anyone investigating a "toggle did nothing"
        // bug needs to find in logs. QML binds the toggle's `enabled` to
        // `!busy` so the UI path is unreachable; defense-in-depth for
        // programmatic callers (D-Bus, test harness).
        qCWarning(lcKcm) << "setEnabled(" << enabled << ") dropped — previous chain still in flight";
        return;
    }
    setChainInFlight(true);
    if (enabled) {
        setAutostart(true, [this]() {
            startDaemon();
        });
    } else {
        setAutostart(false, [this]() {
            stopDaemon();
        });
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

void DaemonController::setAutostart(bool enabled, std::function<void()> onComplete)
{
    // Disable-only (the historical behavior) is insufficient: the D-Bus
    // service file `org.plasmazones.service` carries `SystemdService=plasmazones.service`,
    // so any client D-Bus call to org.plasmazones (e.g. the KWin effect's
    // per-window-move snapping query) triggers systemd auto-activation
    // regardless of whether the unit is enabled at login. The user toggled
    // the daemon off and expected it to stay off; instead it silently
    // respawned on the first window move (discussion #461 item 4,
    // discussion #497).
    //
    // mask makes systemd refuse activation outright — the D-Bus client gets
    // an activation failure and PlasmaZones stays off until the user
    // re-enables. unmask+enable on the way back in mirrors that: a previously
    // masked unit must be unmasked before enable can take effect. The
    // disable+mask pair on the way out leaves both the autostart symlink
    // gone (clean state) and activation blocked (effective state).
    const QString primaryAction = enabled ? QStringLiteral("unmask") : QStringLiteral("disable");
    const QString followupAction = enabled ? QStringLiteral("enable") : QStringLiteral("mask");
    const QLatin1String unit(KCMConstants::SystemdServiceName);
    runSystemctl(
        {QStringLiteral("--user"), primaryAction, unit},
        // mutable so the inner `std::move(onComplete)` actually moves —
        // without it the outer capture is implicit-const and std::move
        // silently degrades to a copy.
        [this, primaryAction, followupAction, unit,
         onComplete = std::move(onComplete)](bool success, const QString& /*output*/) mutable {
            // Don't abort on primaryAction failure: unmask on an un-masked
            // unit and disable on an already-disabled unit are no-ops
            // (and systemctl reports them as failures with a benign
            // "Unit not loaded" warning). We chain unconditionally so
            // the second action always runs; refreshEnabledState then
            // reconciles whatever the final on-disk state is.
            //
            // Log the failure at debug level so an operator investigating
            // "the toggle didn't take" can distinguish "already in the
            // target state" (expected) from a real systemctl error like
            // "Permission denied" or "Failed to connect to bus" (which
            // will also fail the followup, and that failure surfaces as
            // a qCWarning below).
            if (!success) {
                qCDebug(lcKcm) << "systemctl --user" << primaryAction
                               << "reported failure (often a no-op when the unit is already in the target state); "
                                  "chaining followup anyway";
            }
            runSystemctl(
                {QStringLiteral("--user"), followupAction, unit},
                [this, followupAction, onComplete = std::move(onComplete)](bool success, const QString& /*output*/) {
                    // Always refresh against on-disk state — even
                    // on a failed followup, QML must reflect the
                    // unit's actual is-enabled value so the user
                    // isn't stranded on a stale toggle position.
                    // runSystemctl already logs the stderr for
                    // failed invocations; the followup is the
                    // load-bearing step (mask / enable) so a
                    // failure here means the toggle did NOT
                    // take full effect.
                    if (!success) {
                        qCWarning(lcKcm) << "systemctl --user" << followupAction
                                         << "failed — toggle may not have taken full effect; QML will reflect "
                                            "actual on-disk state after refresh";
                    }
                    refreshEnabledState();
                    // startDaemon() / stopDaemon() each issue
                    // their own runSystemctl call (`start` / `stop`)
                    // that is independent of the autostart chain
                    // this guard is scoped to (`unmask`+`enable` /
                    // `disable`+`mask`). Clearing first releases
                    // the guard the moment the chain it covers
                    // has finished — the tail action's systemctl
                    // does not need to run under it.
                    setChainInFlight(false);
                    // Invoke the caller's tail action AFTER the
                    // followup action lands so a `start` issued
                    // here never races a still-pending unmask
                    // (and on disable: stop runs only after mask
                    // completes, so the daemon dies into a
                    // masked unit and can't be re-activated).
                    if (onComplete) {
                        onComplete();
                    }
                });
        });
}

void DaemonController::startDaemon()
{
    if (isRunning()) {
        return;
    }
    const QLatin1String unit(KCMConstants::SystemdServiceName);
    // Defensive unmask. In the setEnabled(true) flow setAutostart's
    // chain runs unmask+enable BEFORE invoking onComplete=startDaemon,
    // so by the time we reach here the unit is guaranteed unmasked
    // and this systemctl call is a cheap no-op. Keeping it makes
    // startDaemon safe to call OUTSIDE that orchestrated flow too —
    // direct D-Bus invocation, test harness, or a future code path —
    // without depending on the orchestrator's chain order. unmask is
    // idempotent on an already-unmasked unit.
    runSystemctl({QStringLiteral("--user"), QStringLiteral("unmask"), unit},
                 [this, unit](bool /*success*/, const QString& /*output*/) {
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
                     runSystemctl({QStringLiteral("--user"), QStringLiteral("reset-failed"), unit},
                                  [this, unit](bool /*success*/, const QString& /*output*/) {
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
                                      runSystemctl({QStringLiteral("--user"), QStringLiteral("start"), unit});
                                  });
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
    // Shared latch: exactly one of {finished, errorOccurred(FailedToStart)}
    // is allowed to deliver the callback. Qt6's QProcess only emits
    // `finished` when the process actually ran — a spawn failure (systemctl
    // not on PATH, non-systemd init system, exec denied, etc.) ONLY emits
    // `errorOccurred(FailedToStart)`. Without an errorOccurred handler the
    // callback would never fire, the caller's onComplete chain would never
    // run, and m_chainInFlight (set in setEnabled) would stay true forever
    // — locking the QML daemon toggle permanently dead. Both branches
    // converge on the same callback/cleanup sequence so callers see exactly
    // one terminal event regardless of how the process ended.
    auto handled = std::make_shared<bool>(false);
    connect(proc, &QProcess::finished, this,
            [proc, callback, args, handled](int exitCode, QProcess::ExitStatus status) {
                if (*handled) {
                    return;
                }
                *handled = true;
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
    connect(proc, &QProcess::errorOccurred, this, [proc, callback, args, handled](QProcess::ProcessError err) {
        // Only FailedToStart needs explicit handling — every other error
        // value (Crashed / Timedout / WriteError / ReadError / UnknownError)
        // is followed by `finished`, which the lambda above already covers.
        if (err != QProcess::FailedToStart || *handled) {
            return;
        }
        *handled = true;
        qCWarning(lcKcm) << "systemctl" << args << "failed to start:" << proc->errorString();
        if (callback) {
            callback(false, QString());
        }
        proc->deleteLater();
    });
    proc->start(QStringLiteral("systemctl"), args);
}

} // namespace PlasmaZones
