// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceLock/LockService.h>

#include "lockstatemachine.h"
#include "waylandsessionlock.h"

#include <PhosphorServiceLock/PamAuthenticator.h>
#include <PhosphorWayland/SessionLock.h>

#include <utility>

#include <pwd.h>
#include <unistd.h>

namespace PhosphorServiceLock {

namespace {
// The session user authenticated on an unlock attempt: the owner of this
// process. Resolved from the real uid, falling back to $USER.
QString currentUser()
{
    if (const struct passwd* pw = getpwuid(getuid()); pw && pw->pw_name)
        return QString::fromLocal8Bit(pw->pw_name);
    return qEnvironmentVariable("USER");
}
} // namespace

class LockService::Private
{
public:
    explicit Private(QString username)
        : machine(&sessionLock, &authenticator, std::move(username))
    {
    }

    // Declaration order matters: the state machine borrows the two backends, so
    // they must be constructed before (and destroyed after) it.
    WaylandSessionLock sessionLock;
    PamAuthenticator authenticator;
    LockStateMachine machine;
};

LockService::LockService(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(currentUser()))
{
    connect(&d->machine, &LockStateMachine::stateChanged, this, &LockService::stateChanged);
    connect(&d->machine, &LockStateMachine::authenticationFailed, this, &LockService::authenticationFailed);
    connect(&d->machine, &LockStateMachine::unlocked, this, &LockService::unlocked);
}

LockService::~LockService() = default;

bool LockService::isSupported() const
{
    // The compositor must advertise ext-session-lock-v1 (surfaced by
    // PhosphorWayland::SessionLock) for the service to lock the session.
    return PhosphorWayland::SessionLock::isSupported();
}

LockService::State LockService::state() const
{
    return d->machine.state();
}

bool LockService::isLocked() const
{
    return state() == State::Locked || state() == State::Authenticating;
}

void LockService::lock()
{
    d->machine.requestLock();
}

void LockService::unlock(const QString& password)
{
    d->machine.authenticate(password);
}

} // namespace PhosphorServiceLock
