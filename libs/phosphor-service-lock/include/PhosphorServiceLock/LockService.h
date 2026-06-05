// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceLock/phosphorservicelock_export.h>

#include <QObject>

namespace PhosphorServiceLock {

/**
 * @brief Session-lock + authentication host for Phosphor-based desktop shells.
 *
 * Authenticates the user through PAM and coordinates the `ext-session-lock-v1`
 * lock state with the compositor through `phosphor-wayland`'s `SessionLock`
 * client. It is the policy / state-machine layer over the raw client and the
 * authentication backend; it composes them rather than binding the protocol or
 * talking to PAM in its public surface, so the host is a clean Qt/QML type with
 * no Wayland or PAM types leaking out.
 *
 * Phase 2.9 milestone 2 is the skeleton: the host constructs and reports whether
 * the compositor advertises the session-lock protocol this service needs. The
 * PAM authenticator, the lock state machine, and the full QML facade arrive in
 * the following milestones.
 */
class PHOSPHORSERVICELOCK_EXPORT LockService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ isSupported CONSTANT)

public:
    explicit LockService(QObject* parent = nullptr);

    /// Whether the compositor advertises the `ext-session-lock-v1` protocol this
    /// service builds on. When false the service constructs but cannot lock.
    [[nodiscard]] bool isSupported() const;

private:
    Q_DISABLE_COPY_MOVE(LockService)
};

} // namespace PhosphorServiceLock
