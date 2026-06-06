// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceSession/phosphorservicesession_export.h>

#include <QObject>
#include <QString>

#include <memory>

class QDBusConnection;

namespace PhosphorServiceSession {

/**
 * @brief logind session manager: capability queries, session/power actions,
 *        inhibitor locks, and the sleep + session signals.
 *
 * Talks to `org.freedesktop.login1.Manager` on the system bus. The capability
 * queries (`CanSuspend` and siblings) are read up front and cached; the actions
 * are capability-gated; the inhibitor locks let the shell lock before suspend
 * and own the power / lid keys. Inert when logind is unreachable (capabilities
 * Unknown, actions no-op with a warning, inhibitors not taken).
 *
 * The `(connection, service)` are injectable so the whole surface is testable
 * against a fake logind Manager with no real system bus and no daemon.
 *
 * Beyond the capability queries it issues the capability-gated session / power
 * actions, holds the logind delay (`sleep`) and block (handle-keys) inhibitors,
 * runs the `PrepareForSleep` lock-before-sleep handshake, and surfaces the
 * session `Lock` / `Unlock` signals.
 */
class PHOSPHORSERVICESESSION_EXPORT SessionHost : public QObject
{
    Q_OBJECT

public:
    /// logind capability state, parsed from the `Can*` query strings.
    enum class Availability {
        Unknown, ///< logind unreachable, or the query errored / has not returned yet.
        Yes, ///< permitted.
        No, ///< not permitted (hardware or configuration does not support it).
        NotApplicable, ///< logind `"na"`: not available (e.g. hibernate with no swap).
        Challenge, ///< permitted but needs polkit authorization.
    };
    Q_ENUM(Availability)

    Q_PROPERTY(Availability canPowerOff READ canPowerOff NOTIFY capabilitiesChanged)
    Q_PROPERTY(Availability canReboot READ canReboot NOTIFY capabilitiesChanged)
    Q_PROPERTY(Availability canHalt READ canHalt NOTIFY capabilitiesChanged)
    Q_PROPERTY(Availability canSuspend READ canSuspend NOTIFY capabilitiesChanged)
    Q_PROPERTY(Availability canHibernate READ canHibernate NOTIFY capabilitiesChanged)
    Q_PROPERTY(Availability canHybridSleep READ canHybridSleep NOTIFY capabilitiesChanged)
    Q_PROPERTY(Availability canSuspendThenHibernate READ canSuspendThenHibernate NOTIFY capabilitiesChanged)

    /// Whether action calls pass logind `interactive=true`, routing an action
    /// that needs authorization to an in-session polkit agent (we ship one in
    /// 2.6) for a native prompt. Defaults true on the QML host; the CLI sets it
    /// false (no agent in a dev shell, so logind just returns an auth error).
    Q_PROPERTY(bool interactive READ interactive WRITE setInteractive NOTIFY interactiveChanged)

    /// Production wiring: the system bus and `org.freedesktop.login1`.
    explicit SessionHost(QObject* parent = nullptr);

    /// Injectable wiring for tests / advanced consumers. @p connection and
    /// @p service select the bus and the logind well-known name; the fake-logind
    /// unit tests inject a session-bus Manager so no real logind is required.
    SessionHost(QDBusConnection connection, QString service, QObject* parent = nullptr);

    ~SessionHost() override;

    [[nodiscard]] Availability canPowerOff() const;
    [[nodiscard]] Availability canReboot() const;
    [[nodiscard]] Availability canHalt() const;
    [[nodiscard]] Availability canSuspend() const;
    [[nodiscard]] Availability canHibernate() const;
    [[nodiscard]] Availability canHybridSleep() const;
    [[nodiscard]] Availability canSuspendThenHibernate() const;

    /// Re-read every capability from logind. Called once at construction and
    /// exposed so the shell can refresh before opening a power menu, since the
    /// answers move with inhibitor locks, lid state, and swap availability.
    Q_INVOKABLE void refreshCapabilities();

    [[nodiscard]] bool interactive() const;
    void setInteractive(bool interactive);

    /// Lock this session (resolved via `XDG_SESSION_ID` / `GetSession`, falling
    /// back to `GetSessionByPID`): logind emits its `Lock` signal, which the
    /// shell routes to the lock surface (2.9). Falls back to
    /// `Manager.LockSessions()` when this session cannot be resolved.
    Q_INVOKABLE void lock();

    /// End this session. Emits logoutRequested() so the shell / compositor can
    /// exit gracefully; terminateSession() is the logind fallback (the resolved
    /// session's `Terminate`) for contexts with no graceful owner.
    Q_INVOKABLE void logout();
    Q_INVOKABLE void terminateSession();

    /// Capability-gated power actions. Each is a no-op (with a warning) when its
    /// capability is not `Yes` or `Challenge`, and otherwise issues the logind
    /// `Manager` call with the current `interactive` flag.
    Q_INVOKABLE void suspend();
    Q_INVOKABLE void hibernate();
    Q_INVOKABLE void hybridSleep();
    Q_INVOKABLE void suspendThenHibernate();
    Q_INVOKABLE void reboot();
    Q_INVOKABLE void powerOff();
    Q_INVOKABLE void halt();

    /// Drop the sleep delay-inhibitor so a pending suspend proceeds. The shell
    /// calls this once the lock surface (2.9) is confirmed up, completing the
    /// lock-before-sleep handshake. A safety timeout drops it anyway if the
    /// shell never confirms, so a missing lock cannot wedge suspend.
    Q_INVOKABLE void allowSleep();

Q_SIGNALS:
    /// Emitted whenever any capability value actually changes (each async
    /// `Can*` reply that differs from the cached value fires this once).
    void capabilitiesChanged();
    void interactiveChanged();
    /// Emitted by logout() so the shell / compositor can end the session
    /// gracefully (close clients, save state) rather than via a blunt logind
    /// terminate.
    void logoutRequested();

    /// Raw logind PrepareForSleep passthrough: @p beforeSleep is true just
    /// before the system sleeps and false just after it resumes.
    void prepareForSleep(bool beforeSleep);

    /// Emitted on PrepareForSleep(true) while the delay inhibitor is held: the
    /// shell should lock now, then call allowSleep() once the surface is up.
    void aboutToSleep();

    /// The logind session was asked to lock / unlock (e.g. `loginctl
    /// lock-session`, a hardware lock key routed through logind, or our own
    /// lock()). The shell routes lockRequested() to 2.9's lock surface.
    void lockRequested();
    void unlockRequested();

private Q_SLOTS:
    // logind signal delivery points (QtDBus signal subscription needs
    // string-named slots, so these are not lambdas).
    void onPrepareForSleep(bool beforeSleep);
    void onSessionLock();
    void onSessionUnlock();

private:
    Q_DISABLE_COPY_MOVE(SessionHost)
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceSession
