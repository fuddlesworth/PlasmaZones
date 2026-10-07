// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The plasma-workspace.target probe: whether a Plasma session is actually
// running behind this daemon, read from the user-bus systemd and kept live
// through the unit's PropertiesChanged. start() runs the query and stop()
// drops the subscription.

#include "daemon/daemon.h"
#include "helpers.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace PlasmaZones {

void Daemon::queryPlasmaWorkspaceState()
{
    // Query the user-bus systemd for `plasma-workspace.target`'s ActiveState
    // to distinguish a real Plasma session from a phantom plasma-restore session.
    //
    // During user-logout → SDDM handoff, systemd may respawn the daemon into a
    // transient "phantom" state: a stray `kwin_wayland` from a fallback session-
    // restore mechanism briefly publishes a fresh `wayland-N` socket inside the
    // still-dying `user@.service`, and `Restart=on-failure` schedules a daemon
    // retry after Qt's wayland QPA aborts on the vanished `wl_display`. The
    // phantom daemon fires welcome OSDs against an output about to be unbound.
    //
    // Why this signal works: `plasma-workspace.target` is only flipped to `active`
    // by `startplasma-wayland`'s orchestration after SDDM hands off. The phantom
    // has no `startplasma-wayland` leader, so the target stays inactive — a signal
    // the phantom cannot fake. logind's `User.State` stays `active` whenever
    // `user@.service` is up (can't distinguish phantom from real), and the
    // wayland-socket existence probe passes during the phantom.
    //
    // Fail-open on all D-Bus errors: `m_plasmaWorkspaceActive` defaults to `true`,
    // so non-systemd setups and headless tests aren't accidentally silenced.
    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    if (!sessionBus.isConnected()) {
        qCDebug(lcDaemon) << "queryPlasmaWorkspaceState: session bus unavailable, leaving m_plasmaWorkspaceActive=true";
        return;
    }

    QDBusMessage subscribeMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
        QStringLiteral("org.freedesktop.systemd1.Manager"), QStringLiteral("Subscribe"));
    auto* subscribeWatcher = new QDBusPendingCallWatcher(sessionBus.asyncCall(subscribeMsg), this);
    connect(subscribeWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qCDebug(lcDaemon) << "queryPlasmaWorkspaceState: Subscribe failed:" << reply.error().message()
                              << "— PropertiesChanged signals may not arrive";
        }
    });

    QDBusMessage getUnitMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
        QStringLiteral("org.freedesktop.systemd1.Manager"), QStringLiteral("GetUnit"));
    getUnitMsg << QStringLiteral("plasma-workspace.target");
    auto* getUnitWatcher = new QDBusPendingCallWatcher(sessionBus.asyncCall(getUnitMsg), this);
    connect(getUnitWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        // A stop() may have landed between the async call and this reply; do not
        // continue into fetchPlasmaWorkspaceActiveState (which installs the
        // PropertiesChanged subscription) after shutdown began.
        if (m_shuttingDown) {
            return;
        }
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            qCInfo(lcDaemon) << "queryPlasmaWorkspaceState: GetUnit('plasma-workspace.target') failed:"
                             << reply.error().message() << "— leaving fail-open (target not loaded)";
            return;
        }
        m_plasmaWorkspaceTargetPath = reply.value().path();
        if (m_plasmaWorkspaceTargetPath.isEmpty()) {
            return;
        }
        fetchPlasmaWorkspaceActiveState();
    });
}

void Daemon::fetchPlasmaWorkspaceActiveState()
{
    // The unit path the request is for, captured now. A reply landing after a
    // stop() cleared the member, or after a later start() resolved another
    // path, is not this query's: subscribing with an empty path would match
    // PropertiesChanged from every systemd unit, and the next stop's keyed
    // disconnect would never remove it (F320).
    const QString path = m_plasmaWorkspaceTargetPath;
    if (path.isEmpty()) {
        return;
    }
    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"), path,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << QStringLiteral("org.freedesktop.systemd1.Unit") << QStringLiteral("ActiveState");
    auto* watcher = new QDBusPendingCallWatcher(sessionBus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (m_shuttingDown || path != m_plasmaWorkspaceTargetPath) {
            return;
        }
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isError()) {
            qCDebug(lcDaemon) << "queryPlasmaWorkspaceState: ActiveState Get failed:" << reply.error().message();
            return;
        }
        const QString state = reply.value().toString();
        m_plasmaWorkspaceActive = (state == QLatin1String("active"));
        qCInfo(lcDaemon) << "plasma-workspace.target ActiveState at startup:" << state
                         << "plasmaWorkspaceActive=" << m_plasmaWorkspaceActive << "path=" << path;

        QDBusConnection bus = QDBusConnection::sessionBus();
        // Disconnect first: a stop() -> start() cycle re-runs this whole query,
        // and QDBusConnectionPrivate appends identical signal hooks without
        // deduping, so without this the slot would fire once per registration
        // per signal. Harmless (the handler is idempotent) but wasteful.
        bus.disconnect(QStringLiteral("org.freedesktop.systemd1"), path,
                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                       SLOT(onPlasmaWorkspaceTargetPropertiesChanged(QString, QVariantMap, QStringList)));
        const bool ok =
            bus.connect(QStringLiteral("org.freedesktop.systemd1"), path,
                        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                        SLOT(onPlasmaWorkspaceTargetPropertiesChanged(QString, QVariantMap, QStringList)));
        if (!ok) {
            qCWarning(lcDaemon) << "queryPlasmaWorkspaceState: failed to subscribe to Unit PropertiesChanged on"
                                << path;
        }
    });
}

void Daemon::onPlasmaWorkspaceTargetPropertiesChanged(const QString& interfaceName,
                                                      const QVariantMap& changedProperties,
                                                      const QStringList& /*invalidatedProperties*/)
{
    if (interfaceName != QLatin1String("org.freedesktop.systemd1.Unit")) {
        return;
    }
    const auto it = changedProperties.constFind(QStringLiteral("ActiveState"));
    if (it == changedProperties.constEnd()) {
        return;
    }
    const QString state = it->toString();
    const bool nowActive = (state == QLatin1String("active"));
    if (m_plasmaWorkspaceActive != nowActive) {
        qCInfo(lcDaemon) << "plasma-workspace.target state changed:" << state;
    }
    m_plasmaWorkspaceActive = nowActive;
}

} // namespace PlasmaZones
