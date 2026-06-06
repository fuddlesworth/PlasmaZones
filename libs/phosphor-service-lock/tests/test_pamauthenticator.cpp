// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Unit test for the PAM-backed authenticator. The deterministic, credential-free
// contract is exercised here: configured service name, fast-fail on bad input,
// the one-transaction-at-a-time guard, and that a definitely-nonexistent user is
// rejected (never accepted) through the real off-thread PAM path. Authenticating
// a VALID user needs real credentials and is exercised by the CLI demo.

#include <PhosphorServiceLock/PamAuthenticator.h>

#include <QSignalSpy>
#include <QTest>

using namespace PhosphorServiceLock;

class PamAuthenticatorTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void exposesConfiguredService();
    void emptyUsernameFailsImmediately();
    void emptyPasswordStillReachesPam();
    void busyRejectsConcurrentCall();
    void nonexistentUserIsRejected();
};

void PamAuthenticatorTest::exposesConfiguredService()
{
    PamAuthenticator defaulted;
    QCOMPARE(defaulted.service(), QStringLiteral("login")); // documented default

    PamAuthenticator custom(QStringLiteral("phosphor-lock"));
    QCOMPARE(custom.service(), QStringLiteral("phosphor-lock"));
}

void PamAuthenticatorTest::emptyUsernameFailsImmediately()
{
    PamAuthenticator auth;
    QSignalSpy okSpy(&auth, &IAuthenticator::succeeded);
    QSignalSpy failSpy(&auth, &IAuthenticator::failed);

    // No user: rejected synchronously, no PAM transaction started.
    auth.authenticate(QString(), QStringLiteral("irrelevant"));
    QCOMPARE(failSpy.count(), 1);
    QCOMPARE(okSpy.count(), 0);
}

void PamAuthenticatorTest::emptyPasswordStillReachesPam()
{
    PamAuthenticator auth;
    QSignalSpy okSpy(&auth, &IAuthenticator::succeeded);
    QSignalSpy failSpy(&auth, &IAuthenticator::failed);

    // A non-empty username with an empty password is NOT short-circuited (only an
    // empty username is): it reaches the real PAM stack and is rejected there.
    auth.authenticate(QStringLiteral("phosphor-no-such-user-1c4e"), QString());
    QVERIFY(failSpy.wait(20000));
    QCOMPARE(okSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
    QVERIFY(!failSpy.at(0).at(0).toString().isEmpty());
}

void PamAuthenticatorTest::busyRejectsConcurrentCall()
{
    PamAuthenticator auth;
    QSignalSpy failSpy(&auth, &IAuthenticator::failed);

    // The first call starts a real (bogus-user) transaction; the second, issued
    // before it completes, must fail fast rather than run two PAM stacks at once.
    // QtConcurrent::run marks the future Running synchronously at setFuture(), so
    // the busy guard (isRunning()) sees it on the immediately-following second
    // call regardless of how fast the worker runs; PAM's faildelay only widens
    // the window further.
    const QString user = QStringLiteral("phosphor-no-such-user-2f1a9c");
    auth.authenticate(user, QStringLiteral("x"));

    // The second call is rejected synchronously (the worker is still running, held
    // by PAM's faildelay), so exactly one new failure appears immediately and it
    // carries the in-progress reason.
    QCOMPARE(failSpy.count(), 0);
    auth.authenticate(user, QStringLiteral("y"));
    QCOMPARE(failSpy.count(), 1);
    QCOMPARE(failSpy.at(0).at(0).toString(), QStringLiteral("authentication already in progress"));

    // Drain the async result of the first call so the watcher is idle at teardown.
    QTRY_VERIFY_WITH_TIMEOUT(failSpy.count() >= 2, 20000);
}

void PamAuthenticatorTest::nonexistentUserIsRejected()
{
    PamAuthenticator auth;
    QSignalSpy okSpy(&auth, &IAuthenticator::succeeded);
    QSignalSpy failSpy(&auth, &IAuthenticator::failed);

    // A user that cannot exist is rejected through the real off-thread PAM path:
    // exactly one failed(), never succeeded(), and the event loop is never
    // blocked while PAM (with its faildelay) runs.
    auth.authenticate(QStringLiteral("phosphor-no-such-user-8b3d07e"), QStringLiteral("whatever"));
    QVERIFY(failSpy.wait(20000));
    QCOMPARE(okSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
    QVERIFY(!failSpy.at(0).at(0).toString().isEmpty()); // a non-empty, human-readable reason
}

QTEST_GUILESS_MAIN(PamAuthenticatorTest)
#include "test_pamauthenticator.moc"
