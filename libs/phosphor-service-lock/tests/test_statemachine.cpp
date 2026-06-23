// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Unit test for the lock state machine. It drives the machine with a fake
// session lock and a fake authenticator that deliver the locked / finished /
// succeeded / failed edges directly, so the whole policy (request -> locked ->
// authenticate -> unlock, with auth-failure retry, the locked-only authenticate
// guard, and compositor-driven teardown) is exercised deterministically with no
// live compositor and no PAM stack.

#include "lockstatemachine.h"

#include "isessionlock.h"

#include <PhosphorServiceLock/IAuthenticator.h>

#include <QSignalSpy>
#include <QTest>

using namespace PhosphorServiceLock;
using State = LockService::State;

namespace {

// QCOMPARE on the scoped Q_ENUM State would instantiate
// QMetaEnum::fromType<LockService::State>, which needs LockService's metaobject
// (not linked into this seam-only test). Compare the underlying values instead.
int n(State s)
{
    return static_cast<int>(s);
}

class FakeSessionLock : public ISessionLock
{
public:
    void lock() override
    {
        ++lockCalls;
    }
    void unlockAndDestroy() override
    {
        ++unlockCalls;
        m_locked = false;
    }
    [[nodiscard]] bool isLocked() const override
    {
        return m_locked;
    }

    // Test drivers simulating the compositor.
    void grant()
    {
        m_locked = true;
        Q_EMIT locked();
    }
    void end()
    {
        m_locked = false;
        Q_EMIT finished();
    }

    int lockCalls = 0;
    int unlockCalls = 0;

private:
    bool m_locked = false;
};

class FakeAuthenticator : public IAuthenticator
{
public:
    void authenticate(const QString& username, const QString& password) override
    {
        ++calls;
        lastUser = username;
        lastPassword = password;
    }

    // Test drivers resolving the outstanding attempt.
    void resolveSuccess()
    {
        Q_EMIT succeeded();
    }
    void resolveFailure(const QString& reason = QStringLiteral("incorrect password"))
    {
        Q_EMIT failed(reason);
    }

    int calls = 0;
    QString lastUser;
    QString lastPassword;
};

} // namespace

class LockStateMachineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void startsUnlocked();
    void requestLockReachesLocked();
    void lockRefusalReturnsToUnlocked();
    void authenticateIgnoredUntilLocked();
    void successfulAuthUnlocks();
    void failedAuthReturnsToLocked();
    void compositorEndWhileLockedUnlocks();
    void compositorEndWhileAuthenticatingUnlocks();
    void staleAuthResultAfterResetIsIgnored();
    void concurrentAuthenticateIgnored();
};

void LockStateMachineTest::startsUnlocked()
{
    FakeSessionLock lock;
    FakeAuthenticator auth;
    LockStateMachine machine(&lock, &auth, QStringLiteral("testuser"));
    QCOMPARE(n(machine.state()), n(State::Unlocked));
}

void LockStateMachineTest::requestLockReachesLocked()
{
    FakeSessionLock lock;
    FakeAuthenticator auth;
    LockStateMachine machine(&lock, &auth, QStringLiteral("testuser"));
    QSignalSpy stateSpy(&machine, &LockStateMachine::stateChanged);

    machine.requestLock();
    QCOMPARE(n(machine.state()), n(State::Locking));
    QCOMPARE(lock.lockCalls, 1);

    lock.grant();
    QCOMPARE(n(machine.state()), n(State::Locked));
    QCOMPARE(stateSpy.count(), 2); // Unlocked->Locking->Locked

    // A second requestLock while locked is a no-op.
    machine.requestLock();
    QCOMPARE(lock.lockCalls, 1);
    QCOMPARE(stateSpy.count(), 2);
}

void LockStateMachineTest::lockRefusalReturnsToUnlocked()
{
    FakeSessionLock lock;
    FakeAuthenticator auth;
    LockStateMachine machine(&lock, &auth, QStringLiteral("testuser"));

    machine.requestLock();
    QCOMPARE(n(machine.state()), n(State::Locking));
    // The compositor refuses (another locker owns the session).
    lock.end();
    QCOMPARE(n(machine.state()), n(State::Unlocked));
}

void LockStateMachineTest::authenticateIgnoredUntilLocked()
{
    FakeSessionLock lock;
    FakeAuthenticator auth;
    LockStateMachine machine(&lock, &auth, QStringLiteral("testuser"));

    // Unlocked: nothing to authenticate against.
    machine.authenticate(QStringLiteral("pw"));
    QCOMPARE(auth.calls, 0);
    QCOMPARE(n(machine.state()), n(State::Unlocked));

    // Locking (awaiting the compositor): still too early.
    machine.requestLock();
    machine.authenticate(QStringLiteral("pw"));
    QCOMPARE(auth.calls, 0);
    QCOMPARE(n(machine.state()), n(State::Locking));
}

void LockStateMachineTest::successfulAuthUnlocks()
{
    FakeSessionLock lock;
    FakeAuthenticator auth;
    LockStateMachine machine(&lock, &auth, QStringLiteral("testuser"));
    QSignalSpy unlockedSpy(&machine, &LockStateMachine::unlocked);

    machine.requestLock();
    lock.grant();
    QCOMPARE(n(machine.state()), n(State::Locked));

    machine.authenticate(QStringLiteral("hunter2"));
    QCOMPARE(n(machine.state()), n(State::Authenticating));
    QCOMPARE(auth.calls, 1);
    QCOMPARE(auth.lastUser, QStringLiteral("testuser")); // the session user, not the password
    QCOMPARE(auth.lastPassword, QStringLiteral("hunter2"));

    auth.resolveSuccess();
    QCOMPARE(n(machine.state()), n(State::Unlocked));
    QCOMPARE(lock.unlockCalls, 1); // the compositor lock was released
    QCOMPARE(unlockedSpy.count(), 1);
}

void LockStateMachineTest::failedAuthReturnsToLocked()
{
    FakeSessionLock lock;
    FakeAuthenticator auth;
    LockStateMachine machine(&lock, &auth, QStringLiteral("testuser"));
    QSignalSpy failSpy(&machine, &LockStateMachine::authenticationFailed);
    QSignalSpy unlockedSpy(&machine, &LockStateMachine::unlocked);

    machine.requestLock();
    lock.grant();
    machine.authenticate(QStringLiteral("wrong"));
    QCOMPARE(n(machine.state()), n(State::Authenticating));

    auth.resolveFailure(QStringLiteral("incorrect password"));
    QCOMPARE(n(machine.state()), n(State::Locked)); // stays locked, ready to retry
    QCOMPARE(lock.unlockCalls, 0); // the session was NOT unlocked
    QCOMPARE(unlockedSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
    QCOMPARE(failSpy.at(0).at(0).toString(), QStringLiteral("incorrect password"));

    // A retry after failure is accepted.
    machine.authenticate(QStringLiteral("hunter2"));
    QCOMPARE(n(machine.state()), n(State::Authenticating));
    QCOMPARE(auth.calls, 2);
}

void LockStateMachineTest::compositorEndWhileLockedUnlocks()
{
    FakeSessionLock lock;
    FakeAuthenticator auth;
    LockStateMachine machine(&lock, &auth, QStringLiteral("testuser"));

    machine.requestLock();
    lock.grant();
    QCOMPARE(n(machine.state()), n(State::Locked));

    // The compositor ends the lock itself (its own secure recovery path).
    lock.end();
    QCOMPARE(n(machine.state()), n(State::Unlocked));
}

void LockStateMachineTest::concurrentAuthenticateIgnored()
{
    FakeSessionLock lock;
    FakeAuthenticator auth;
    LockStateMachine machine(&lock, &auth, QStringLiteral("testuser"));

    machine.requestLock();
    lock.grant();
    machine.authenticate(QStringLiteral("first"));
    QCOMPARE(n(machine.state()), n(State::Authenticating));
    QCOMPARE(auth.calls, 1);

    QSignalSpy stateSpy(&machine, &LockStateMachine::stateChanged);
    // A second attempt while one is outstanding must not start another, nor emit
    // a spurious state change.
    machine.authenticate(QStringLiteral("second"));
    QCOMPARE(auth.calls, 1);
    QCOMPARE(n(machine.state()), n(State::Authenticating));
    QCOMPARE(stateSpy.count(), 0);
}

void LockStateMachineTest::compositorEndWhileAuthenticatingUnlocks()
{
    FakeSessionLock lock;
    FakeAuthenticator auth;
    LockStateMachine machine(&lock, &auth, QStringLiteral("testuser"));

    machine.requestLock();
    lock.grant();
    machine.authenticate(QStringLiteral("pw"));
    QCOMPARE(n(machine.state()), n(State::Authenticating));

    // The compositor ends the lock mid-authentication (its own recovery path):
    // the session returns to Unlocked even with an attempt outstanding.
    lock.end();
    QCOMPARE(n(machine.state()), n(State::Unlocked));
    QCOMPARE(lock.unlockCalls, 0); // finished() tore the lock down; we don't unlock_and_destroy
}

void LockStateMachineTest::staleAuthResultAfterResetIsIgnored()
{
    FakeSessionLock lock;
    FakeAuthenticator auth;
    LockStateMachine machine(&lock, &auth, QStringLiteral("testuser"));
    QSignalSpy unlockedSpy(&machine, &LockStateMachine::unlocked);

    machine.requestLock();
    lock.grant();
    machine.authenticate(QStringLiteral("pw"));
    QCOMPARE(n(machine.state()), n(State::Authenticating));

    // The compositor ends the lock first, resetting to Unlocked.
    lock.end();
    QCOMPARE(n(machine.state()), n(State::Unlocked));

    // A now-stale success from the in-flight attempt must NOT unlock a session
    // that is already unlocked: the Authenticating guard drops it.
    auth.resolveSuccess();
    QCOMPARE(n(machine.state()), n(State::Unlocked));
    QCOMPARE(lock.unlockCalls, 0);
    QCOMPARE(unlockedSpy.count(), 0);
}

QTEST_GUILESS_MAIN(LockStateMachineTest)
#include "test_statemachine.moc"
