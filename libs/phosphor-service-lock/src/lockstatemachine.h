// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) lock state machine. It coordinates a session lock
// (ISessionLock) and an authenticator (IAuthenticator) behind seams, so the
// whole lock policy (request -> locked -> authenticate -> unlock, with
// auth-failure retry and compositor-driven teardown) is unit-tested with fakes
// and no live compositor or PAM stack. LockService composes the real backends
// over it.

#include <PhosphorServiceLock/LockService.h>

#include <QObject>
#include <QString>

namespace PhosphorServiceLock {

class ISessionLock;
class IAuthenticator;

class LockStateMachine : public QObject
{
    Q_OBJECT

public:
    /// @p lock and @p authenticator are borrowed (not owned); they must outlive
    /// this machine. @p username is the user authenticated on an unlock attempt.
    LockStateMachine(ISessionLock* lock, IAuthenticator* authenticator, QString username, QObject* parent = nullptr);

    [[nodiscard]] LockService::State state() const;

    /// Request a lock. A no-op unless currently Unlocked.
    void requestLock();
    /// Verify @p password for the session user. A no-op unless currently Locked.
    void authenticate(const QString& password);

Q_SIGNALS:
    void stateChanged();
    void authenticationFailed(const QString& reason);
    void unlocked();

private:
    void setState(LockService::State state);
    void onLocked();
    void onFinished();
    void onAuthSucceeded();
    void onAuthFailed(const QString& reason);

    ISessionLock* m_lock;
    IAuthenticator* m_authenticator;
    QString m_username;
    LockService::State m_state = LockService::State::Unlocked;
};

} // namespace PhosphorServiceLock
