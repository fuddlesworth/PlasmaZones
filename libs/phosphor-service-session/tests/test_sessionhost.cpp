// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// SessionHost behaviour against an in-process fake logind Manager + Session on
// the session bus, so the capability parsing, action routing, inhibitor calls,
// and the PrepareForSleep / session-signal surfacing are exercised
// deterministically with no real org.freedesktop.login1 and no destructive
// side effects.

#include <PhosphorServiceSession/SessionHost.h>

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QSignalSpy>
#include <QTest>

#include <fcntl.h>
#include <unistd.h>

using namespace PhosphorServiceSession;

namespace {
// Unique to this suite: a different executable (e.g. the brightness smoke test)
// may register its own fake logind on the shared session bus, so a generic name
// would collide under `ctest -j` and make one suite QSKIP its whole body.
constexpr auto kService = "org.phosphor.test.session.Logind";
constexpr auto kManagerPath = "/org/freedesktop/login1";
constexpr auto kSessionPath = "/org/phosphor/test/session1";

using Availability = SessionHost::Availability;
} // namespace

// Base for the fake D-Bus objects: unregisters its own object path in the
// destructor, while the object is still alive (before ~QObject), so a stack-local
// fake cannot leave QtDBus holding a dangling pointer between end-of-scope and
// the test's cleanup().
class FakeDBusObject : public QObject
{
public:
    ~FakeDBusObject() override
    {
        if (!m_path.isEmpty())
            m_connection.unregisterObject(m_path);
    }

    void boundTo(const QDBusConnection& connection, const QString& path)
    {
        m_connection = connection;
        m_path = path;
    }

private:
    QDBusConnection m_connection = QDBusConnection::sessionBus();
    QString m_path;
};

// Fake logind Manager: configurable capability strings, recorded actions /
// inhibitor requests, and an emittable PrepareForSleep signal.
class FakeManager : public FakeDBusObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.login1.Manager")

public:
    ~FakeManager() override
    {
        if (sleepInhibitorReadEnd >= 0)
            ::close(sleepInhibitorReadEnd);
    }

    // Read end of the pipe whose write end was handed to the host as the sleep
    // delay inhibitor. -1 until the host has taken that inhibitor; once the host
    // releases (closes) the write end, this read end reports EOF.
    int sleepInhibitorReadEnd = -1;

    QString powerOff = QStringLiteral("yes");
    QString reboot = QStringLiteral("yes");
    QString halt = QStringLiteral("challenge");
    QString suspend = QStringLiteral("yes");
    QString hibernate = QStringLiteral("na");
    QString hybridSleep = QStringLiteral("no");
    QString suspendThenHibernate = QStringLiteral("yes");

    QStringList actions; // e.g. "Suspend(1)"
    QList<QPair<QString, QString>> inhibits; // (what, mode)
    bool lockSessionsCalled = false;
    QString lastResolveMethod; // which resolve call the host used: GetSession / GetSessionByPID

    void emitPrepareForSleep(bool beforeSleep)
    {
        Q_EMIT PrepareForSleep(beforeSleep);
    }

public Q_SLOTS:
    QString CanPowerOff()
    {
        return powerOff;
    }
    QString CanReboot()
    {
        return reboot;
    }
    QString CanHalt()
    {
        return halt;
    }
    QString CanSuspend()
    {
        return suspend;
    }
    QString CanHibernate()
    {
        return hibernate;
    }
    QString CanHybridSleep()
    {
        return hybridSleep;
    }
    QString CanSuspendThenHibernate()
    {
        return suspendThenHibernate;
    }

    QDBusObjectPath GetSessionByPID(uint)
    {
        lastResolveMethod = QStringLiteral("GetSessionByPID");
        return QDBusObjectPath(QLatin1String(kSessionPath));
    }
    QDBusObjectPath GetSession(const QString&)
    {
        lastResolveMethod = QStringLiteral("GetSession");
        return QDBusObjectPath(QLatin1String(kSessionPath));
    }

    void PowerOff(bool interactive)
    {
        record(QStringLiteral("PowerOff"), interactive);
    }
    void Reboot(bool interactive)
    {
        record(QStringLiteral("Reboot"), interactive);
    }
    void Halt(bool interactive)
    {
        record(QStringLiteral("Halt"), interactive);
    }
    void Suspend(bool interactive)
    {
        record(QStringLiteral("Suspend"), interactive);
    }
    void Hibernate(bool interactive)
    {
        record(QStringLiteral("Hibernate"), interactive);
    }
    void HybridSleep(bool interactive)
    {
        record(QStringLiteral("HybridSleep"), interactive);
    }
    void SuspendThenHibernate(bool interactive)
    {
        record(QStringLiteral("SuspendThenHibernate"), interactive);
    }
    void LockSessions()
    {
        lockSessionsCalled = true;
    }

    QDBusUnixFileDescriptor Inhibit(const QString& what, const QString&, const QString&, const QString& mode)
    {
        inhibits.append({what, mode});
        // For the sleep delay inhibitor, hand the host the write end of a pipe so
        // the test can observe the host releasing it: closing the host's dup
        // makes the retained read end report EOF. Other inhibitors get an inert
        // fd (/dev/null), which is harmless and always present.
        if (what.contains(QLatin1String("sleep")) && mode == QLatin1String("delay")) {
            int fds[2];
            if (::pipe(fds) == 0) {
                ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
                ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
                ::fcntl(fds[0], F_SETFL, O_NONBLOCK); // read end never blocks fdAtEof()
                if (sleepInhibitorReadEnd >= 0)
                    ::close(sleepInhibitorReadEnd); // a re-take replaces the prior pipe
                sleepInhibitorReadEnd = fds[0];
                QDBusUnixFileDescriptor wrapped(fds[1]); // dups the write end
                ::close(fds[1]);
                return wrapped;
            }
        }
        const int fd = ::open("/dev/null", O_RDONLY);
        QDBusUnixFileDescriptor wrapped(fd); // dups
        if (fd >= 0)
            ::close(fd);
        return wrapped;
    }

Q_SIGNALS:
    void PrepareForSleep(bool beforeSleep);

private:
    void record(const QString& name, bool interactive)
    {
        actions.append(name + QLatin1Char('(') + (interactive ? QLatin1Char('1') : QLatin1Char('0'))
                       + QLatin1Char(')'));
    }
};

// Fake Session exposing the Lock / Unlock / Terminate methods we call.
class FakeSessionMethods : public FakeDBusObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.login1.Session")

public:
    QStringList calls;

public Q_SLOTS:
    void Lock()
    {
        calls.append(QStringLiteral("Lock"));
    }
    void Unlock()
    {
        calls.append(QStringLiteral("Unlock"));
    }
    void Terminate()
    {
        calls.append(QStringLiteral("Terminate"));
    }
};

// Fake Session exposing the Lock / Unlock signals logind sends to request a
// lock. (A separate class from FakeSessionMethods: a single QObject cannot carry
// both a Lock slot and a Lock signal.)
class FakeSessionSignals : public FakeDBusObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.login1.Session")

public:
    void emitLock()
    {
        Q_EMIT Lock();
    }
    void emitUnlock()
    {
        Q_EMIT Unlock();
    }

Q_SIGNALS:
    void Lock();
    void Unlock();
};

class TestSessionHost : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Capture the ambient XDG_SESSION_ID so the per-test pinning below (which
        // selects the session-resolution branch) can be restored at the end.
        m_xdgWasSet = qEnvironmentVariableIsSet("XDG_SESSION_ID");
        m_xdgOriginal = qEnvironmentVariable("XDG_SESSION_ID");
    }

    void cleanupTestCase()
    {
        if (m_xdgWasSet)
            qputenv("XDG_SESSION_ID", m_xdgOriginal.toLocal8Bit());
        else
            qunsetenv("XDG_SESSION_ID");
    }

    void init()
    {
        // Pin the resolution branch: with XDG_SESSION_ID set, the host resolves
        // via GetSession deterministically on any host (a bare CI runner has no
        // XDG_SESSION_ID and would otherwise take the GetSessionByPID branch).
        // The dedicated GetSessionByPID test unsets this itself.
        qputenv("XDG_SESSION_ID", "phosphor-test-session");
        m_bus = QDBusConnection::sessionBus();
        if (!m_bus.isConnected())
            QSKIP("no session bus available");
        if (!m_bus.registerService(QLatin1String(kService)))
            QSKIP("could not own the test service name");
    }

    void cleanup()
    {
        // The fakes unregister their own object paths in their destructors (which
        // run at end-of-test scope, while the objects are still alive), so here
        // we only need to drop the well-known service name.
        m_bus.unregisterService(QLatin1String(kService));
    }

    // Each Can* string maps to the matching Availability; an unrecognised string
    // maps to Unknown (never a spurious Yes).
    void capabilitiesReflectLogind()
    {
        FakeManager manager;
        manager.powerOff = QStringLiteral("yes");
        manager.reboot = QStringLiteral("no");
        manager.halt = QStringLiteral("challenge");
        manager.suspend = QStringLiteral("na");
        manager.hibernate = QStringLiteral("yes");
        manager.hybridSleep = QStringLiteral("gibberish"); // unrecognised -> Unknown
        manager.suspendThenHibernate = QStringLiteral("challenge");
        registerManager(manager);

        SessionHost host(m_bus, QLatin1String(kService));
        QTRY_COMPARE(host.canSuspendThenHibernate(), Availability::Challenge);
        QCOMPARE(host.canPowerOff(), Availability::Yes);
        QCOMPARE(host.canReboot(), Availability::No);
        QCOMPARE(host.canHalt(), Availability::Challenge);
        QCOMPARE(host.canSuspend(), Availability::NotApplicable);
        QCOMPARE(host.canHibernate(), Availability::Yes);
        QCOMPARE(host.canHybridSleep(), Availability::Unknown);
    }

    // An available action issues the matching Manager call carrying the
    // interactive flag; flipping interactive flips the marshalled bool.
    void actionsRouteWithInteractiveFlag()
    {
        FakeManager manager;
        manager.suspend = QStringLiteral("yes");
        registerManager(manager);

        SessionHost host(m_bus, QLatin1String(kService));
        QTRY_COMPARE(host.canSuspend(), Availability::Yes);

        host.suspend(); // interactive defaults true
        QTRY_VERIFY(manager.actions.contains(QStringLiteral("Suspend(1)")));

        host.setInteractive(false);
        host.suspend();
        QTRY_VERIFY(manager.actions.contains(QStringLiteral("Suspend(0)")));
    }

    // Every power action routes to its matching Manager method when actionable,
    // guarding against a copy-paste error in any one method name / label in the
    // SessionHost action table.
    void everyActionRoutesToItsManagerMethod()
    {
        FakeManager manager;
        manager.powerOff = QStringLiteral("yes");
        manager.reboot = QStringLiteral("yes");
        manager.halt = QStringLiteral("yes");
        manager.suspend = QStringLiteral("yes");
        manager.hibernate = QStringLiteral("yes");
        manager.hybridSleep = QStringLiteral("yes");
        manager.suspendThenHibernate = QStringLiteral("yes");
        registerManager(manager);

        SessionHost host(m_bus, QLatin1String(kService));
        QTRY_COMPARE(host.canSuspendThenHibernate(), Availability::Yes);

        host.powerOff();
        host.reboot();
        host.halt();
        host.suspend();
        host.hibernate();
        host.hybridSleep();
        host.suspendThenHibernate();

        for (const QString& method :
             {QStringLiteral("PowerOff"), QStringLiteral("Reboot"), QStringLiteral("Halt"), QStringLiteral("Suspend"),
              QStringLiteral("Hibernate"), QStringLiteral("HybridSleep"), QStringLiteral("SuspendThenHibernate")}) {
            QTRY_VERIFY(manager.actions.contains(method + QStringLiteral("(1)")));
        }
    }

    // A challenge capability is actionable (permitted, may need auth); the call
    // is still issued.
    void challengeActionIsIssued()
    {
        FakeManager manager;
        manager.reboot = QStringLiteral("challenge");
        registerManager(manager);

        SessionHost host(m_bus, QLatin1String(kService));
        QTRY_COMPARE(host.canReboot(), Availability::Challenge);
        host.reboot();
        QTRY_VERIFY(manager.actions.contains(QStringLiteral("Reboot(1)")));
    }

    // An unavailable action (No / NotApplicable) is refused: no Manager call is
    // issued at all.
    void unavailableActionIsRefused()
    {
        FakeManager manager;
        manager.hibernate = QStringLiteral("no");
        registerManager(manager);

        SessionHost host(m_bus, QLatin1String(kService));
        QTRY_COMPARE(host.canHibernate(), Availability::No);
        host.hibernate();
        // Give any (erroneously-issued) call time to arrive, then assert none did.
        QTest::qWait(100);
        QVERIFY(!manager.actions.contains(QStringLiteral("Hibernate(1)")));
        QVERIFY(!manager.actions.contains(QStringLiteral("Hibernate(0)")));
    }

    // Both inhibitors are taken at construction with the correct what / mode: a
    // delay lock on "sleep" and a block lock on the handle-* keys.
    void inhibitorsTakenWithCorrectWhatAndMode()
    {
        FakeManager manager;
        registerManager(manager);

        SessionHost host(m_bus, QLatin1String(kService));
        QTRY_VERIFY(manager.inhibits.size() >= 2);

        const auto has = [&](const QString& whatContains, const QString& mode) {
            for (const auto& inhibit : manager.inhibits) {
                if (inhibit.first.contains(whatContains) && inhibit.second == mode)
                    return true;
            }
            return false;
        };
        QVERIFY(has(QStringLiteral("sleep"), QStringLiteral("delay")));
        QVERIFY(has(QStringLiteral("handle-power-key"), QStringLiteral("block")));
        QVERIFY(has(QStringLiteral("handle-lid-switch"), QStringLiteral("block")));
    }

    // A logind PrepareForSleep(true) over the bus drives the handshake:
    // prepareForSleep(true) AND aboutToSleep fire; the resume edge fires
    // prepareForSleep(false) with no further aboutToSleep, and re-arms the delay
    // inhibitor so the next suspend edge is still guarded.
    void prepareForSleepDrivesHandshakeOverBus()
    {
        FakeManager manager;
        registerManager(manager);

        SessionHost host(m_bus, QLatin1String(kService));
        QSignalSpy prep(&host, &SessionHost::prepareForSleep);
        QSignalSpy about(&host, &SessionHost::aboutToSleep);

        // The handshake runs only while the host holds the sleep delay inhibitor
        // (taken asynchronously at construction). Re-emit PrepareForSleep(true)
        // until aboutToSleep surfaces rather than racing the async Inhibit reply
        // with a fixed sleep; once the inhibitor is held, aboutToSleep fires.
        QTRY_VERIFY([&] {
            manager.emitPrepareForSleep(true);
            return about.count() >= 1;
        }());
        QVERIFY(prep.count() >= 1);
        QVERIFY(prep.first().at(0).toBool()); // the first edge was before-sleep

        const int sleepDelayBefore = countInhibits(manager, QStringLiteral("sleep"), QStringLiteral("delay"));
        const int aboutAfterSleep = about.count();
        prep.clear();
        host.allowSleep();

        manager.emitPrepareForSleep(false);
        // The resume edge passes through as prepareForSleep(false)...
        QTRY_VERIFY(!prep.isEmpty() && !prep.last().at(0).toBool());
        QCOMPARE(about.count(), aboutAfterSleep); // ...with no new aboutToSleep...
        // ...and the host re-arms the delay inhibitor for the next suspend edge.
        QTRY_VERIFY(countInhibits(manager, QStringLiteral("sleep"), QStringLiteral("delay")) > sleepDelayBefore);
    }

    // The core lock-before-sleep guarantee: allowSleep() must actually release
    // the sleep delay inhibitor (close its fd). The fake hands the host the write
    // end of a pipe; releasing it closes the host's dup, which the retained read
    // end observes as EOF. Without the release logind would suspend only after
    // its InhibitDelayMaxSec timeout, defeating the handshake.
    void sleepInhibitorReleasedOnAllowSleep()
    {
        FakeManager manager;
        registerManager(manager);

        SessionHost host(m_bus, QLatin1String(kService));
        QSignalSpy about(&host, &SessionHost::aboutToSleep);

        // aboutToSleep fires only while the host holds the sleep inhibitor, so it
        // doubles as proof the host has taken (and is holding) the pipe write end.
        QTRY_VERIFY([&] {
            manager.emitPrepareForSleep(true);
            return about.count() >= 1;
        }());
        QVERIFY(manager.sleepInhibitorReadEnd >= 0);
        QVERIFY(!fdAtEof(manager.sleepInhibitorReadEnd)); // still held: not yet EOF

        host.allowSleep();
        QTRY_VERIFY(fdAtEof(manager.sleepInhibitorReadEnd)); // released: write end closed
    }

    // lock() resolves this session and calls Session.Lock; terminateSession()
    // calls Session.Terminate.
    void lockAndTerminateRouteToSession()
    {
        FakeManager manager;
        registerManager(manager);
        FakeSessionMethods session;
        QVERIFY(m_bus.registerObject(QLatin1String(kSessionPath), &session, QDBusConnection::ExportAllSlots));
        session.boundTo(m_bus, QLatin1String(kSessionPath));

        SessionHost host(m_bus, QLatin1String(kService));
        // Drive lock() until the async session resolution (GetSession /
        // GetSessionByPID) lands and it routes to Session.Lock; before that it
        // takes the LockSessions() fallback. Retrying the call is deterministic
        // where a fixed sleep would flake on a loaded CI runner; the extra
        // fallback calls are harmless.
        QTRY_VERIFY([&] {
            host.lock();
            return session.calls.contains(QStringLiteral("Lock"));
        }());

        // The session is now resolved, so terminateSession() routes to
        // Session.Terminate.
        host.terminateSession();
        QTRY_VERIFY(session.calls.contains(QStringLiteral("Terminate")));
    }

    // terminateSession() must refuse (no-op) while this session is unresolved:
    // firing the resolved session's Terminate is destructive, so an empty session
    // path must never fall through to a blind terminate. Called before the async
    // GetSession / GetSessionByPID resolution can land (no event-loop spin), so
    // the path is still empty.
    void terminateRefusedWhenSessionUnresolved()
    {
        FakeManager manager;
        registerManager(manager);
        FakeSessionMethods session;
        QVERIFY(m_bus.registerObject(QLatin1String(kSessionPath), &session, QDBusConnection::ExportAllSlots));
        session.boundTo(m_bus, QLatin1String(kSessionPath));

        SessionHost host(m_bus, QLatin1String(kService));
        host.terminateSession(); // session path not yet resolved -> must refuse
        QTest::qWait(100); // give any (erroneously-issued) Terminate time to arrive
        QVERIFY(!session.calls.contains(QStringLiteral("Terminate")));
    }

    // lock() falls back to Manager.LockSessions() when this session has not been
    // resolved yet (called before the async GetSession / GetSessionByPID reply
    // lands, so the session path is still empty). The only positive coverage of
    // the fallback.
    void lockFallsBackToLockSessionsWhenUnresolved()
    {
        FakeManager manager;
        registerManager(manager);

        SessionHost host(m_bus, QLatin1String(kService));
        host.lock(); // session path not yet resolved -> LockSessions() fallback
        QTRY_VERIFY(manager.lockSessionsCalled);
    }

    // Resolution prefers GetSession(XDG_SESSION_ID) when the env var is set (init()
    // sets it), rather than GetSessionByPID. Pins the branch deterministically so
    // coverage does not depend on the host's ambient XDG_SESSION_ID.
    void sessionResolvesViaGetSessionWhenXdgSet()
    {
        FakeManager manager;
        registerManager(manager);

        SessionHost host(m_bus, QLatin1String(kService));
        QTRY_COMPARE(manager.lastResolveMethod, QStringLiteral("GetSession"));
    }

    // Resolution falls back to GetSessionByPID when XDG_SESSION_ID is unset. The
    // complementary branch to the test above; together they cover both resolution
    // paths on any host.
    void sessionResolvesViaGetSessionByPidWhenXdgUnset()
    {
        qunsetenv("XDG_SESSION_ID"); // cleanupTestCase() restores the original
        FakeManager manager;
        registerManager(manager);

        SessionHost host(m_bus, QLatin1String(kService));
        QTRY_COMPARE(manager.lastResolveMethod, QStringLiteral("GetSessionByPID"));
    }

    // A logind Session Lock / Unlock signal surfaces as lockRequested() /
    // unlockRequested() over the real bus subscription.
    void sessionSignalsSurfaceOverBus()
    {
        FakeManager manager;
        registerManager(manager);
        FakeSessionSignals session;
        QVERIFY(m_bus.registerObject(QLatin1String(kSessionPath), &session, QDBusConnection::ExportAllSignals));
        session.boundTo(m_bus, QLatin1String(kSessionPath));

        SessionHost host(m_bus, QLatin1String(kService));
        QSignalSpy lockSpy(&host, &SessionHost::lockRequested);
        QSignalSpy unlockSpy(&host, &SessionHost::unlockRequested);

        // The host subscribes to the session Lock/Unlock signals in its
        // GetSession reply handler. Re-emit until the subscription is live
        // (proven by the first surfaced lockRequested), rather than guessing the
        // round-trip with a fixed sleep that can flake under CI load.
        QTRY_VERIFY([&] {
            session.emitLock();
            return lockSpy.count() >= 1;
        }());
        QCOMPARE(unlockSpy.count(), 0); // a Lock signal does not surface as unlock

        session.emitUnlock();
        QTRY_VERIFY(unlockSpy.count() >= 1);
    }

private:
    void registerManager(FakeManager& manager)
    {
        QVERIFY(m_bus.registerObject(QLatin1String(kManagerPath), &manager,
                                     QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
        manager.boundTo(m_bus, QLatin1String(kManagerPath));
    }

    // Count the inhibitor requests the host issued matching @p whatContains / @p mode.
    static int countInhibits(const FakeManager& manager, const QString& whatContains, const QString& mode)
    {
        int n = 0;
        for (const auto& inhibit : manager.inhibits) {
            if (inhibit.first.contains(whatContains) && inhibit.second == mode)
                ++n;
        }
        return n;
    }

    // True once every write end of the pipe behind @p fd is closed (read returns
    // EOF). The read end is O_NONBLOCK, so this never blocks while the write end
    // is still open (read returns -1/EAGAIN, i.e. not EOF).
    static bool fdAtEof(int fd)
    {
        char buf;
        return ::read(fd, &buf, sizeof(buf)) == 0;
    }

    QDBusConnection m_bus = QDBusConnection::sessionBus();
    bool m_xdgWasSet = false;
    QString m_xdgOriginal;
};

QTEST_GUILESS_MAIN(TestSessionHost)
#include "test_sessionhost.moc"
