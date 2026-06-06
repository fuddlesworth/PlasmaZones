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
constexpr auto kService = "org.phosphor.test.Logind";
constexpr auto kManagerPath = "/org/freedesktop/login1";
constexpr auto kSessionPath = "/org/phosphor/test/session1";

using Availability = SessionHost::Availability;
} // namespace

// Fake logind Manager: configurable capability strings, recorded actions /
// inhibitor requests, and an emittable PrepareForSleep signal.
class FakeManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.login1.Manager")

public:
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
        return QDBusObjectPath(QLatin1String(kSessionPath));
    }
    QDBusObjectPath GetSession(const QString&)
    {
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
        // Return a real fd so the host's QDBusUnixFileDescriptor holds a valid
        // handle; /dev/null is harmless and always present.
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
class FakeSessionMethods : public QObject
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
class FakeSessionSignals : public QObject
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
    void init()
    {
        m_bus = QDBusConnection::sessionBus();
        if (!m_bus.isConnected())
            QSKIP("no session bus available");
        if (!m_bus.registerService(QLatin1String(kService)))
            QSKIP("could not own the test service name");
    }

    void cleanup()
    {
        m_bus.unregisterObject(QLatin1String(kManagerPath));
        m_bus.unregisterObject(QLatin1String(kSessionPath));
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
    // prepareForSleep(false) with no further aboutToSleep.
    void prepareForSleepDrivesHandshakeOverBus()
    {
        FakeManager manager;
        registerManager(manager);

        SessionHost host(m_bus, QLatin1String(kService));
        QSignalSpy prep(&host, &SessionHost::prepareForSleep);
        QSignalSpy about(&host, &SessionHost::aboutToSleep);

        manager.emitPrepareForSleep(true);
        QTRY_COMPARE(about.count(), 1);
        QCOMPARE(prep.count(), 1);
        QCOMPARE(prep.takeFirst().at(0).toBool(), true);

        host.allowSleep();

        manager.emitPrepareForSleep(false);
        QTRY_COMPARE(prep.count(), 1);
        QCOMPARE(prep.takeFirst().at(0).toBool(), false);
        QCOMPARE(about.count(), 1); // no new aboutToSleep on resume
    }

    // lock() resolves this session and calls Session.Lock; terminateSession()
    // calls Session.Terminate.
    void lockAndTerminateRouteToSession()
    {
        FakeManager manager;
        registerManager(manager);
        FakeSessionMethods session;
        QVERIFY(m_bus.registerObject(QLatin1String(kSessionPath), &session, QDBusConnection::ExportAllSlots));

        SessionHost host(m_bus, QLatin1String(kService));
        // Drive lock() until the async session resolution (GetSessionByPID) lands
        // and it routes to Session.Lock; before that it takes the LockSessions()
        // fallback. Retrying the call is deterministic where a fixed sleep would
        // flake on a loaded CI runner; the extra fallback calls are harmless.
        QTRY_VERIFY([&] {
            host.lock();
            return session.calls.contains(QStringLiteral("Lock"));
        }());

        // The session is now resolved, so terminateSession() routes to
        // Session.Terminate.
        host.terminateSession();
        QTRY_VERIFY(session.calls.contains(QStringLiteral("Terminate")));
    }

    // A logind Session Lock / Unlock signal surfaces as lockRequested() /
    // unlockRequested() over the real bus subscription.
    void sessionSignalsSurfaceOverBus()
    {
        FakeManager manager;
        registerManager(manager);
        FakeSessionSignals session;
        QVERIFY(m_bus.registerObject(QLatin1String(kSessionPath), &session, QDBusConnection::ExportAllSignals));

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
    }

    QDBusConnection m_bus = QDBusConnection::sessionBus();
};

QTEST_GUILESS_MAIN(TestSessionHost)
#include "test_sessionhost.moc"
