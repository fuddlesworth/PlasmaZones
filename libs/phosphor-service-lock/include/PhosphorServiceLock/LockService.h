// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceLock/phosphorservicelock_export.h>

#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServiceLock {

/**
 * @brief Session-lock + authentication host for Phosphor-based desktop shells.
 *
 * Authenticates the session user through PAM and coordinates the
 * `ext-session-lock-v1` lock state with the compositor through `phosphor-wayland`'s
 * `SessionLock` client. It is the policy / state-machine layer over the raw
 * client and the authentication backend; it composes them rather than binding
 * the protocol or talking to PAM in its public surface, so the host is a clean
 * Qt/QML type with no Wayland or PAM types leaking out.
 *
 * Flow: `lock()` asks the compositor to lock (state `Locking`); the compositor
 * confirms (`Locked`). `unlock(password)` authenticates the session user
 * (`Authenticating`); on success the lock is released (`Unlocked`, `unlocked()`),
 * on failure the state returns to `Locked` and `authenticationFailed()` fires.
 *
 * Lock *surfaces* (the per-output graphics shown while locked) are a shell
 * concern wired in a later phase; this service owns authentication and the lock
 * lifecycle, not rendering.
 */
class PHOSPHORSERVICELOCK_EXPORT LockService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ isSupported CONSTANT)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool locked READ isLocked NOTIFY stateChanged)

public:
    /// The lock lifecycle.
    enum class State {
        Unlocked, ///< No lock; the session is usable.
        Locking, ///< lock() issued; awaiting the compositor's reply.
        Locked, ///< The compositor locked the session; awaiting authentication.
        Authenticating, ///< An unlock() attempt is verifying credentials.
    };
    Q_ENUM(State)

    explicit LockService(QObject* parent = nullptr);
    ~LockService() override;

    /// Whether the compositor advertises the `ext-session-lock-v1` protocol this
    /// service builds on. When false the service constructs but cannot lock.
    [[nodiscard]] bool isSupported() const;

    /// The current lock state.
    [[nodiscard]] State state() const;

    /// True while the session is locked (whether idle or authenticating).
    [[nodiscard]] bool isLocked() const;

    /// Request that the compositor lock the session. A no-op unless currently
    /// `Unlocked`.
    Q_INVOKABLE void lock();

    /// Attempt to unlock by authenticating the session user with @p password. A
    /// no-op unless currently `Locked`. On success the session unlocks; on
    /// failure `authenticationFailed()` fires and the state returns to `Locked`.
    Q_INVOKABLE void unlock(const QString& password);

Q_SIGNALS:
    /// `state()` changed.
    void stateChanged();
    /// An `unlock()` attempt was rejected; @p reason is a human-readable,
    /// non-sensitive explanation. The session stays locked.
    void authenticationFailed(const QString& reason);
    /// The session was successfully unlocked after authentication.
    void unlocked();

private:
    Q_DISABLE_COPY_MOVE(LockService)

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceLock
