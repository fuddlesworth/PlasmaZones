// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smoke test for phosphor-service-polkit. Pins the plumbing contract:
// QML-registration idempotency, the default object path, and inert
// construction. registerAgent() is exercised against a deliberately bogus
// session id so the test can never register as the real session's agent (which
// would intercept the tester's authentications); it must fail and leave the
// object inert. The pure request decode is unit-tested in test_decode.cpp; the
// full PAM session lifecycle needs a live polkitd and is exercised via the CLI
// demo against pkexec.

#include <PhosphorServicePolkit/AuthRequest.h>
#include <PhosphorServicePolkit/PolkitAgent.h>
#include <PhosphorServicePolkit/QmlRegistration.h>

#include <QSignalSpy>
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
    void activeRequestStartsNull();
    void cancelWithNoRequestIsNoop();
    void authenticateWithNoRequestIsNoop();
    void respondWithNoSessionIsNoop();

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

void PolkitSmokeTest::activeRequestStartsNull()
{
    // No authentication is in flight until polkit calls initiateAuthentication.
    PolkitAgent agent(bogusSession(), PolkitAgent::defaultObjectPath());
    QVERIFY(agent.activeRequest() == nullptr);
}

void PolkitSmokeTest::cancelWithNoRequestIsNoop()
{
    // Declining when nothing is active is a safe no-op: no crash, no signal, the
    // active request stays null. (The decode of a real request into an
    // AuthRequest needs a live polkitd calling back, and is exercised through
    // the CLI demo against pkexec in milestone 6.)
    PolkitAgent agent(bogusSession(), PolkitAgent::defaultObjectPath());
    QSignalSpy cancelledSpy(&agent, &PolkitAgent::authenticationCancelled);
    agent.cancel();
    QCOMPARE(cancelledSpy.count(), 0);
    QVERIFY(agent.activeRequest() == nullptr);
}

void PolkitSmokeTest::authenticateWithNoRequestIsNoop()
{
    // Starting a PAM conversation with nothing to authenticate is a safe no-op.
    // The real authenticate -> prompt -> respond -> completed flow needs a live
    // polkitd + PAM and is exercised via the CLI demo against pkexec (milestone 6).
    PolkitAgent agent(bogusSession(), PolkitAgent::defaultObjectPath());
    QSignalSpy completedSpy(&agent, &PolkitAgent::authenticationCompleted);
    agent.authenticate();
    QCOMPARE(completedSpy.count(), 0);
    QVERIFY(agent.activeRequest() == nullptr);
}

void PolkitSmokeTest::respondWithNoSessionIsNoop()
{
    // Answering when no conversation is running must not crash or emit anything.
    PolkitAgent agent(bogusSession(), PolkitAgent::defaultObjectPath());
    QSignalSpy errorSpy(&agent, &PolkitAgent::authenticationError);
    agent.respond(QStringLiteral("not-a-real-password"));
    QCOMPARE(errorSpy.count(), 0);
}

QTEST_GUILESS_MAIN(PolkitSmokeTest)
#include "test_smoke.moc"
