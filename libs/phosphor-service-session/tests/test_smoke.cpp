// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSession/PhosphorServiceSession.h>

#include <QDBusConnection>
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

    // Constructing the host with no live logind must not crash or block: the
    // service loads inert.
    void constructsInertWithoutLogind()
    {
        SessionHost production;
        Q_UNUSED(production)

        // The DI ctor against the session bus is the fake-logind test seam; with
        // no Manager bound it is equally inert.
        SessionHost injected(QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.login1"));
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
};

QTEST_GUILESS_MAIN(SmokeTest)

#include "test_smoke.moc"
