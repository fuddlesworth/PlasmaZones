// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "lockstatemachine.h"

#include "isessionlock.h"

#include <PhosphorServiceLock/IAuthenticator.h>

#include <utility>

namespace PhosphorServiceLock {

LockStateMachine::LockStateMachine(ISessionLock* lock, IAuthenticator* authenticator, QString username, QObject* parent)
    : QObject(parent)
    , m_lock(lock)
    , m_authenticator(authenticator)
    , m_username(std::move(username))
{
    connect(m_lock, &ISessionLock::locked, this, &LockStateMachine::onLocked);
    connect(m_lock, &ISessionLock::finished, this, &LockStateMachine::onFinished);
    connect(m_authenticator, &IAuthenticator::succeeded, this, &LockStateMachine::onAuthSucceeded);
    connect(m_authenticator, &IAuthenticator::failed, this, &LockStateMachine::onAuthFailed);
}

LockService::State LockStateMachine::state() const
{
    return m_state;
}

void LockStateMachine::setState(LockService::State state)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged();
}

void LockStateMachine::requestLock()
{
    if (m_state != LockService::State::Unlocked)
        return;
    setState(LockService::State::Locking);
    m_lock->lock();
}

void LockStateMachine::onLocked()
{
    // The compositor granted the lock we requested.
    if (m_state == LockService::State::Locking)
        setState(LockService::State::Locked);
}

void LockStateMachine::onFinished()
{
    // The compositor refused the lock (while Locking) or ended an active one
    // (while Locked / Authenticating). Either way the lock object is gone and we
    // are back to an unlocked session; a fresh requestLock() is needed to retry.
    if (m_state != LockService::State::Unlocked)
        setState(LockService::State::Unlocked);
}

void LockStateMachine::authenticate(const QString& password)
{
    // Only meaningful from a settled Locked state: not before the compositor has
    // confirmed the lock, and not while another attempt is already in flight.
    if (m_state != LockService::State::Locked)
        return;
    setState(LockService::State::Authenticating);
    m_authenticator->authenticate(m_username, password);
}

void LockStateMachine::onAuthSucceeded()
{
    if (m_state != LockService::State::Authenticating)
        return;
    // Release the compositor lock and return to a usable session.
    m_lock->unlockAndDestroy();
    setState(LockService::State::Unlocked);
    Q_EMIT unlocked();
}

void LockStateMachine::onAuthFailed(const QString& reason)
{
    if (m_state != LockService::State::Authenticating)
        return;
    // Wrong password (or the attempt could not complete): stay locked, let the
    // user try again.
    setState(LockService::State::Locked);
    Q_EMIT authenticationFailed(reason);
}

} // namespace PhosphorServiceLock
