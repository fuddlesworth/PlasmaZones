// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSession/SessionHost.h>

#include <PhosphorDBus/Client.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QLoggingCategory>
#include <QTimer>

#include <utility>

Q_LOGGING_CATEGORY(lcSessionHost, "phosphor.service.session.host")

namespace {
constexpr auto kManagerPath = "/org/freedesktop/login1";
constexpr auto kManagerIface = "org.freedesktop.login1.Manager";
constexpr auto kSessionIface = "org.freedesktop.login1.Session";

// Safety bound for the lock-before-sleep handshake: if the shell does not
// confirm the lock (call allowSleep()) within this window we release the delay
// inhibitor ourselves. Kept conservatively under logind's default
// InhibitDelayMaxSec (5s) so we always release before logind force-suspends.
constexpr int kSleepLockTimeoutMs = 4000;
} // namespace

namespace PhosphorServiceSession {

namespace {
// logind's Can* methods answer with one of these strings; anything else (an
// error, an empty reply, an unknown distro extension) maps to Unknown.
SessionHost::Availability parseAvailability(const QString& value)
{
    if (value == QLatin1String("yes"))
        return SessionHost::Availability::Yes;
    if (value == QLatin1String("no"))
        return SessionHost::Availability::No;
    if (value == QLatin1String("na"))
        return SessionHost::Availability::NotApplicable;
    if (value == QLatin1String("challenge"))
        return SessionHost::Availability::Challenge;
    return SessionHost::Availability::Unknown;
}
} // namespace

class SessionHost::Private
{
public:
    Private(QDBusConnection connection, QString service)
        : bus(std::move(connection))
        , service(std::move(service))
    {
    }

    SessionHost* owner = nullptr;
    QDBusConnection bus;
    QString service;

    // Whether action calls pass logind interactive=true (route auth to our
    // in-session polkit agent). Default true on the host; the CLI flips it off.
    bool interactive = true;

    // This session's logind object path, resolved asynchronously at
    // construction. Empty until resolved (or when resolution fails), which makes
    // lock() fall back to LockSessions() and terminateSession() a no-op.
    QString sessionPath;

    Availability canPowerOff = Availability::Unknown;
    Availability canReboot = Availability::Unknown;
    Availability canHalt = Availability::Unknown;
    Availability canSuspend = Availability::Unknown;
    Availability canHibernate = Availability::Unknown;
    Availability canHybridSleep = Availability::Unknown;
    Availability canSuspendThenHibernate = Availability::Unknown;

    // Inhibitor fds: the delay lock on "sleep" (released across the
    // lock-before-sleep handshake and re-taken on resume) and the block lock on
    // the handle-* keys (held for the host's lifetime). Each QDBusUnixFileDescriptor
    // owns a dup of the fd and closes it when cleared or destroyed, so teardown
    // releases both inhibitors automatically.
    QDBusUnixFileDescriptor sleepInhibitor;
    QDBusUnixFileDescriptor keyInhibitor;
    QTimer sleepTimer; // bounded lock-before-sleep release safety

    // Issue one async Manager.Can* call per capability and fold each reply into
    // the cached value, emitting capabilitiesChanged only on an actual change.
    // Inert when the bus is not connected (no logind): the values stay Unknown
    // and no call is issued.
    void refresh()
    {
        if (!bus.isConnected())
            return;

        PhosphorDBus::Client manager(bus, service, QLatin1String(kManagerPath), &lcSessionHost());

        struct Cap
        {
            const char* method;
            Availability Private::* slot;
        };
        static constexpr Cap caps[] = {
            {"CanPowerOff", &Private::canPowerOff},
            {"CanReboot", &Private::canReboot},
            {"CanHalt", &Private::canHalt},
            {"CanSuspend", &Private::canSuspend},
            {"CanHibernate", &Private::canHibernate},
            {"CanHybridSleep", &Private::canHybridSleep},
            {"CanSuspendThenHibernate", &Private::canSuspendThenHibernate},
        };

        for (const auto& cap : caps) {
            const QDBusPendingCall pending = manager.asyncCall(QLatin1String(kManagerIface), QLatin1String(cap.method));
            auto* watcher = new QDBusPendingCallWatcher(pending, owner);
            const auto slot = cap.slot;
            QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner,
                             [this, slot](QDBusPendingCallWatcher* call) {
                                 call->deleteLater();
                                 const QDBusPendingReply<QString> reply = *call;
                                 const Availability value =
                                     reply.isError() ? Availability::Unknown : parseAvailability(reply.value());
                                 if (this->*slot != value) {
                                     this->*slot = value;
                                     Q_EMIT owner->capabilitiesChanged();
                                 }
                             });
        }
    }

    // Resolve this session's logind object path so lock() / terminateSession()
    // can target it. Prefer the graphical session named by XDG_SESSION_ID;
    // fall back to the caller's PID. Leaves sessionPath empty (the inert /
    // fallback path) on error or a disconnected bus.
    void resolveSession()
    {
        if (!bus.isConnected())
            return;

        PhosphorDBus::Client manager(bus, service, QLatin1String(kManagerPath), &lcSessionHost());
        const QString xdgSessionId = qEnvironmentVariable("XDG_SESSION_ID");
        const QDBusPendingCall pending = xdgSessionId.isEmpty()
            ? manager.asyncCall(QLatin1String(kManagerIface), QStringLiteral("GetSessionByPID"),
                                {static_cast<uint>(QCoreApplication::applicationPid())})
            : manager.asyncCall(QLatin1String(kManagerIface), QStringLiteral("GetSession"), {xdgSessionId});
        auto* watcher = new QDBusPendingCallWatcher(pending, owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QDBusObjectPath> reply = *call;
            if (reply.isError()) {
                qCDebug(lcSessionHost) << "logind session lookup failed; lock falls back to all sessions:"
                                       << reply.error().message();
                return;
            }
            sessionPath = reply.value().path();
            subscribeSessionSignals();
        });
    }

    // Subscribe to the resolved session's Lock / Unlock signals so a lock
    // request routed through logind (loginctl, a hardware lock key, or our own
    // Session.Lock) reaches the shell. Only possible once sessionPath is known,
    // so a lock requested in the brief startup window before resolution lands
    // (milliseconds) is not caught; that is an accepted async tradeoff, not a
    // path we guard, since the subscription inherently needs the resolved path.
    void subscribeSessionSignals()
    {
        if (!bus.isConnected() || sessionPath.isEmpty())
            return;
        if (!bus.connect(service, sessionPath, QLatin1String(kSessionIface), QStringLiteral("Lock"), owner,
                         SLOT(onSessionLock())))
            qCWarning(lcSessionHost) << "failed to subscribe to the logind session Lock signal at" << sessionPath;
        if (!bus.connect(service, sessionPath, QLatin1String(kSessionIface), QStringLiteral("Unlock"), owner,
                         SLOT(onSessionUnlock())))
            qCWarning(lcSessionHost) << "failed to subscribe to the logind session Unlock signal at" << sessionPath;
    }

    // Issue a capability-gated logind Manager action. A no-op (with a warning)
    // when the capability is not Yes or Challenge, so a misbehaving caller
    // cannot fire an unsupported action.
    void powerAction(const char* method, Availability capability, const char* label)
    {
        if (capability != Availability::Yes && capability != Availability::Challenge) {
            qCWarning(lcSessionHost) << label << "refused: logind reports the action is not available";
            return;
        }
        if (!bus.isConnected()) {
            qCWarning(lcSessionHost) << label << "refused: no logind connection";
            return;
        }
        PhosphorDBus::Client manager(bus, service, QLatin1String(kManagerPath), &lcSessionHost());
        manager.fireAndForget(owner, QLatin1String(kManagerIface), QLatin1String(method), {interactive},
                              QLatin1String(label));
    }

    // Take one logind inhibitor and keep its fd alive in @p slot. The reply
    // holds a dup'd fd; copying it into the member keeps the inhibitor active
    // until the member is cleared (allowSleep / re-take) or destroyed.
    void inhibit(const QString& what, const QString& mode, QDBusUnixFileDescriptor Private::* slot, const QString& why)
    {
        if (!bus.isConnected())
            return;
        PhosphorDBus::Client manager(bus, service, QLatin1String(kManagerPath), &lcSessionHost());
        const QDBusPendingCall pending = manager.asyncCall(QLatin1String(kManagerIface), QStringLiteral("Inhibit"),
                                                           {what, QStringLiteral("Phosphor Shell"), why, mode});
        auto* watcher = new QDBusPendingCallWatcher(pending, owner);
        QObject::connect(
            watcher, &QDBusPendingCallWatcher::finished, owner, [this, slot, what](QDBusPendingCallWatcher* call) {
                call->deleteLater();
                const QDBusPendingReply<QDBusUnixFileDescriptor> reply = *call;
                if (reply.isError()) {
                    qCWarning(lcSessionHost) << "logind Inhibit failed for" << what << ":" << reply.error().message();
                    return;
                }
                const QDBusUnixFileDescriptor fd = reply.value();
                if (!fd.isValid()) {
                    // A "successful" reply carrying an invalid fd (a restricted or
                    // misbehaving logind) would store an inert inhibitor that the
                    // handshake later treats as held. Refuse it.
                    qCWarning(lcSessionHost)
                        << "logind Inhibit returned an invalid fd for" << what << "; the inhibitor is not held";
                    return;
                }
                this->*slot = fd;
            });
    }

    // A delay lock on "sleep" so we can lock the session before suspend; it is
    // released across the handshake and re-taken on resume.
    void takeSleepInhibitor()
    {
        inhibit(QStringLiteral("sleep"), QStringLiteral("delay"), &Private::sleepInhibitor,
                QStringLiteral("lock the session before the system sleeps"));
    }

    // A block lock on the handle-* keys so the shell, not logind's defaults,
    // decides what the power / suspend / hibernate / lid keys do. Held for the
    // host's lifetime.
    void takeKeyInhibitor()
    {
        inhibit(QStringLiteral("handle-power-key:handle-suspend-key:handle-hibernate-key:handle-lid-switch"),
                QStringLiteral("block"), &Private::keyInhibitor,
                QStringLiteral("the shell handles the power, suspend, hibernate, and lid keys"));
    }

    void subscribePrepareForSleep()
    {
        if (!bus.isConnected())
            return;
        if (!bus.connect(service, QLatin1String(kManagerPath), QLatin1String(kManagerIface),
                         QStringLiteral("PrepareForSleep"), owner, SLOT(onPrepareForSleep(bool))))
            qCWarning(lcSessionHost)
                << "failed to subscribe to logind PrepareForSleep; the lock-before-sleep handshake will not run";
    }

    void handlePrepareForSleep(bool beforeSleep)
    {
        Q_EMIT owner->prepareForSleep(beforeSleep);
        if (beforeSleep) {
            // The lock-before-sleep handshake only holds while we actually hold
            // the delay inhibitor: only then is logind blocked waiting on us. If
            // a fast suspend -> resume -> suspend cycle outran the async re-take
            // (the resume Inhibit reply has not landed yet) we are not holding
            // it, so logind is NOT waiting; skip the handshake rather than emit
            // aboutToSleep() and arm a timer for an inhibitor we do not have,
            // which would promise a lock guarantee we cannot keep.
            if (!sleepInhibitor.isValid()) {
                qCWarning(lcSessionHost)
                    << "PrepareForSleep without a held sleep inhibitor; skipping the lock-before-sleep handshake";
                return;
            }
            // We hold the delay inhibitor, so logind is waiting on us. Ask the
            // shell to lock and arm the safety timeout: if the shell never calls
            // allowSleep() we release anyway, so a missing lock cannot wedge the
            // suspend past logind's InhibitDelayMaxSec.
            Q_EMIT owner->aboutToSleep();
            sleepTimer.start(kSleepLockTimeoutMs);
        } else {
            // Resumed: re-arm the delay inhibitor for the next sleep. logind
            // pairs every PrepareForSleep(true) with a (false) on resume; guard
            // against a stray/duplicate resume edge re-issuing Inhibit while one
            // is still held.
            sleepTimer.stop();
            if (!sleepInhibitor.isValid())
                takeSleepInhibitor();
        }
    }

    void allowSleep()
    {
        sleepTimer.stop();
        // Closing the fd releases the delay inhibitor, letting suspend proceed.
        sleepInhibitor = QDBusUnixFileDescriptor();
    }
};

SessionHost::SessionHost(QObject* parent)
    : SessionHost(QDBusConnection::systemBus(), QStringLiteral("org.freedesktop.login1"), parent)
{
}

SessionHost::SessionHost(QDBusConnection connection, QString service, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(std::move(connection), std::move(service)))
{
    d->owner = this;

    // The safety timeout that completes the lock-before-sleep handshake if the
    // shell never confirms the lock.
    d->sleepTimer.setSingleShot(true);
    connect(&d->sleepTimer, &QTimer::timeout, this, [this] {
        qCWarning(lcSessionHost) << "lock-before-sleep confirmation timed out; releasing the sleep inhibitor";
        d->allowSleep();
    });

    d->refresh();
    d->resolveSession();
    d->takeKeyInhibitor();
    d->takeSleepInhibitor();
    d->subscribePrepareForSleep();
}

SessionHost::~SessionHost() = default;

SessionHost::Availability SessionHost::canPowerOff() const
{
    return d->canPowerOff;
}

SessionHost::Availability SessionHost::canReboot() const
{
    return d->canReboot;
}

SessionHost::Availability SessionHost::canHalt() const
{
    return d->canHalt;
}

SessionHost::Availability SessionHost::canSuspend() const
{
    return d->canSuspend;
}

SessionHost::Availability SessionHost::canHibernate() const
{
    return d->canHibernate;
}

SessionHost::Availability SessionHost::canHybridSleep() const
{
    return d->canHybridSleep;
}

SessionHost::Availability SessionHost::canSuspendThenHibernate() const
{
    return d->canSuspendThenHibernate;
}

void SessionHost::refreshCapabilities()
{
    d->refresh();
}

bool SessionHost::interactive() const
{
    return d->interactive;
}

void SessionHost::setInteractive(bool interactive)
{
    if (d->interactive == interactive)
        return;
    d->interactive = interactive;
    Q_EMIT interactiveChanged();
}

void SessionHost::lock()
{
    if (!d->bus.isConnected()) {
        qCWarning(lcSessionHost) << "lock refused: no logind connection";
        return;
    }
    if (!d->sessionPath.isEmpty()) {
        PhosphorDBus::Client session(d->bus, d->service, d->sessionPath, &lcSessionHost());
        session.fireAndForget(this, QLatin1String(kSessionIface), QStringLiteral("Lock"), {}, QStringLiteral("Lock"));
        return;
    }
    // This session is not resolved; lock every session of the user instead.
    qCDebug(lcSessionHost) << "session path unresolved; locking all sessions";
    PhosphorDBus::Client manager(d->bus, d->service, QLatin1String(kManagerPath), &lcSessionHost());
    manager.fireAndForget(this, QLatin1String(kManagerIface), QStringLiteral("LockSessions"), {},
                          QStringLiteral("LockSessions"));
}

void SessionHost::logout()
{
    // The compositor owns the graceful end-of-session path (close clients, save
    // state). The shell connects logoutRequested() to its exit; terminateSession()
    // is the logind fallback for contexts with no graceful owner.
    Q_EMIT logoutRequested();
}

void SessionHost::terminateSession()
{
    if (!d->bus.isConnected() || d->sessionPath.isEmpty()) {
        qCWarning(lcSessionHost) << "terminateSession refused: no resolved logind session";
        return;
    }
    PhosphorDBus::Client session(d->bus, d->service, d->sessionPath, &lcSessionHost());
    session.fireAndForget(this, QLatin1String(kSessionIface), QStringLiteral("Terminate"), {},
                          QStringLiteral("Terminate"));
}

void SessionHost::suspend()
{
    d->powerAction("Suspend", d->canSuspend, "Suspend");
}

void SessionHost::hibernate()
{
    d->powerAction("Hibernate", d->canHibernate, "Hibernate");
}

void SessionHost::hybridSleep()
{
    d->powerAction("HybridSleep", d->canHybridSleep, "HybridSleep");
}

void SessionHost::suspendThenHibernate()
{
    d->powerAction("SuspendThenHibernate", d->canSuspendThenHibernate, "SuspendThenHibernate");
}

void SessionHost::reboot()
{
    d->powerAction("Reboot", d->canReboot, "Reboot");
}

void SessionHost::powerOff()
{
    d->powerAction("PowerOff", d->canPowerOff, "PowerOff");
}

void SessionHost::halt()
{
    d->powerAction("Halt", d->canHalt, "Halt");
}

void SessionHost::allowSleep()
{
    d->allowSleep();
}

void SessionHost::onPrepareForSleep(bool beforeSleep)
{
    d->handlePrepareForSleep(beforeSleep);
}

void SessionHost::onSessionLock()
{
    Q_EMIT lockRequested();
}

void SessionHost::onSessionUnlock()
{
    Q_EMIT unlockRequested();
}

} // namespace PhosphorServiceSession
