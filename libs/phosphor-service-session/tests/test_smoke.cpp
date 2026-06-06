// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSession/PhosphorServiceSession.h>

#include <QDBusConnection>
#include <QSignalSpy>
#include <QTest>

using namespace PhosphorServiceSession;

class SmokeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // registerQmlTypes() is std::call_once-guarded, so a hot-reloading shell can
    // call it from every fresh QQmlEngine without Qt's duplicate-registration
    // warning. Calling it twice must be a no-op, not a crash.
    void registrationIsIdempotent()
    {
        registerQmlTypes();
        registerQmlTypes();
    }

    // Constructing the host must not crash or block. The default (production)
    // ctor talks to the real system-bus logind: on a host with no logind it is
    // fully inert, and on a host with one it briefly takes and releases the real
    // inhibitors during the object's lifetime (harmless, released on destruction)
    // -- either way construction must not crash. The DI ctor against the session
    // bus with a bogus service name is unconditionally inert (no Manager bound).
    void constructsInertWithoutLogind()
    {
        SessionHost production;
        Q_UNUSED(production)

        SessionHost injected(QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.login1.invalid.test"));
        Q_UNUSED(injected)
    }

    // With no logind Manager bound, every capability stays Unknown: the Can*
    // queries either are not issued (disconnected bus) or error out, and an
    // errored / absent reply maps to Unknown. The host must never report a
    // spurious Yes when it cannot reach logind.
    void capabilitiesDefaultUnknownWithoutLogind()
    {
        SessionHost host(QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.login1.invalid.test"));
        QCOMPARE(host.canPowerOff(), SessionHost::Availability::Unknown);
        QCOMPARE(host.canReboot(), SessionHost::Availability::Unknown);
        QCOMPARE(host.canHalt(), SessionHost::Availability::Unknown);
        QCOMPARE(host.canSuspend(), SessionHost::Availability::Unknown);
        QCOMPARE(host.canHibernate(), SessionHost::Availability::Unknown);
        QCOMPARE(host.canHybridSleep(), SessionHost::Availability::Unknown);
        QCOMPARE(host.canSuspendThenHibernate(), SessionHost::Availability::Unknown);

        // Pump the event loop so any in-flight async Can* replies (errors,
        // since the service name is bogus) are delivered: they must resolve to
        // Unknown, not crash or flip to a real availability.
        QTest::qWait(50);
        QCOMPARE(host.canSuspend(), SessionHost::Availability::Unknown);
    }

    // Power actions on an inert host (capabilities Unknown) are refused without
    // crashing: the capability gate blocks them before any call is issued. lock()
    // with no resolved session takes the LockSessions() fallback, harmless
    // against a bogus service.
    void inertActionsAreNoOps()
    {
        SessionHost host(QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.login1.invalid.test"));
        host.suspend();
        host.hibernate();
        host.hybridSleep();
        host.suspendThenHibernate();
        host.reboot();
        host.powerOff();
        host.halt();
        host.lock();
        host.terminateSession();
        QTest::qWait(20);
    }

    // logout() is a pure signal: it emits logoutRequested() for the shell to act
    // on, with no logind dependency, so it works even when logind is absent.
    void logoutEmitsRequest()
    {
        SessionHost host(QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.login1.invalid.test"));
        QSignalSpy spy(&host, &SessionHost::logoutRequested);
        host.logout();
        QCOMPARE(spy.count(), 1);
    }

    // interactive defaults true (the QML host routes auth through our polkit
    // agent) and emits interactiveChanged only on an actual flip.
    void interactiveDefaultsTrueAndIsEdgeTriggered()
    {
        SessionHost host(QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.login1.invalid.test"));
        QCOMPARE(host.interactive(), true);
        QSignalSpy spy(&host, &SessionHost::interactiveChanged);
        host.setInteractive(true);
        QCOMPARE(spy.count(), 0);
        host.setInteractive(false);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(host.interactive(), false);
    }

    // The lock-before-sleep handshake signal logic on an inert host (no logind
    // bound, so no delay inhibitor is held), driven by invoking the
    // PrepareForSleep delivery slot directly. A before-sleep edge passes through
    // as prepareForSleep(true) but does NOT raise aboutToSleep: with no inhibitor
    // held logind is not blocked on us, so there is nothing to hand off to the
    // lock surface and arming a handshake would promise a guarantee we cannot
    // keep. The resume edge passes through as prepareForSleep(false). allowSleep()
    // is a safe no-op with no fd held. (The held-inhibitor handshake, including
    // aboutToSleep + the fd release, is covered against a fake logind in
    // test_sessionhost.)
    void prepareForSleepWithoutHeldInhibitorSkipsHandshake()
    {
        SessionHost host(QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.login1.invalid.test"));
        QSignalSpy prep(&host, &SessionHost::prepareForSleep);
        QSignalSpy about(&host, &SessionHost::aboutToSleep);

        QVERIFY(QMetaObject::invokeMethod(&host, "onPrepareForSleep", Qt::DirectConnection, Q_ARG(bool, true)));
        QCOMPARE(prep.count(), 1);
        QCOMPARE(prep.takeFirst().at(0).toBool(), true);
        QCOMPARE(about.count(), 0); // no inhibitor held -> no handshake

        host.allowSleep(); // safe no-op with nothing held

        QVERIFY(QMetaObject::invokeMethod(&host, "onPrepareForSleep", Qt::DirectConnection, Q_ARG(bool, false)));
        QCOMPARE(prep.count(), 1);
        QCOMPARE(prep.takeFirst().at(0).toBool(), false);
        QCOMPARE(about.count(), 0);
    }

    // A logind session Lock / Unlock signal surfaces as lockRequested() /
    // unlockRequested(), driven without a real session by invoking the delivery
    // slots directly. The shell routes lockRequested() to 2.9's lock surface.
    void sessionLockSignalsSurface()
    {
        SessionHost host(QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.login1.invalid.test"));
        QSignalSpy lockSpy(&host, &SessionHost::lockRequested);
        QSignalSpy unlockSpy(&host, &SessionHost::unlockRequested);

        QVERIFY(QMetaObject::invokeMethod(&host, "onSessionLock", Qt::DirectConnection));
        QCOMPARE(lockSpy.count(), 1);
        QCOMPARE(unlockSpy.count(), 0);

        QVERIFY(QMetaObject::invokeMethod(&host, "onSessionUnlock", Qt::DirectConnection));
        QCOMPARE(unlockSpy.count(), 1);
        QCOMPARE(lockSpy.count(), 1);
    }
};

QTEST_GUILESS_MAIN(SmokeTest)

#include "test_smoke.moc"
