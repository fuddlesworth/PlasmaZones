// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Milestone-1 smoke test for phosphor-service-polkit. Pins the plumbing
// contract: QML-registration idempotency, the default object path, and inert
// construction. registerAgent() is exercised against a deliberately bogus
// session id so the test can never register as the real session's agent (which
// would intercept the tester's authentications); it must fail and leave the
// object inert. The request decode + PAM session paths need a live polkitd and
// are exercised manually via the CLI demo in milestone 6.

#include <PhosphorServicePolkit/PolkitAgent.h>
#include <PhosphorServicePolkit/QmlRegistration.h>

#include <QString>
#include <QTest>

#include <memory>

using namespace PhosphorServicePolkit;

class PolkitSmokeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void registerQmlTypesIsIdempotent();
    void defaultObjectPath();
    void constructsInert();
    void registerWithBogusSessionStaysInert();

private:
    // A session id that cannot resolve to a real logind session, so
    // registerListener can only fail. Guarantees the test never becomes the
    // tester's authentication agent.
    static QString bogusSession()
    {
        return QStringLiteral("phosphor-test-nonexistent-session");
    }
};

void PolkitSmokeTest::registerQmlTypesIsIdempotent()
{
    // The std::call_once guard must make a second call a no-op (no crash, no
    // duplicate-registration fault). A hot-reloading shell relies on this.
    registerQmlTypes();
    registerQmlTypes();
}

void PolkitSmokeTest::defaultObjectPath()
{
    QCOMPARE(PolkitAgent::defaultObjectPath(), QStringLiteral("/org/phosphor/PolicyKit1/AuthenticationAgent"));
}

void PolkitSmokeTest::constructsInert()
{
    // A freshly constructed agent has not registered yet.
    PolkitAgent agent(bogusSession(), PolkitAgent::defaultObjectPath());
    QVERIFY(!agent.registered());
}

void PolkitSmokeTest::registerWithBogusSessionStaysInert()
{
    PolkitAgent agent(bogusSession(), PolkitAgent::defaultObjectPath());
    // Registering for a session that does not exist cannot succeed; the agent
    // reports failure and stays inert rather than crashing.
    const bool ok = agent.registerAgent();
    QVERIFY(!ok);
    QVERIFY(!agent.registered());
}

QTEST_GUILESS_MAIN(PolkitSmokeTest)
#include "test_smoke.moc"
