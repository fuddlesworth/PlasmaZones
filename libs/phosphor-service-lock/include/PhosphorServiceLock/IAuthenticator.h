// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceLock/phosphorservicelock_export.h>

#include <QObject>
#include <QString>

namespace PhosphorServiceLock {

/**
 * @brief The credential-verification surface a lock UI (and the lock state
 *        machine) drives.
 *
 * Verifies a password for a user and reports the outcome asynchronously. The
 * production implementation is PamAuthenticator; tests and the state machine can
 * substitute a fake, so the lock policy is exercisable without a real PAM stack
 * or valid credentials.
 */
class PHOSPHORSERVICELOCK_EXPORT IAuthenticator : public QObject
{
    Q_OBJECT

public:
    explicit IAuthenticator(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~IAuthenticator() override;

    /// Asynchronously verify @p password for @p username.
    ///
    /// Contract: every authenticate() MUST eventually emit exactly one
    /// `succeeded` or `failed`. The check runs off the GUI thread so a slow
    /// authentication stack never blocks the event loop, and the result signal
    /// is delivered on the GUI thread. Implementations should reject (as a
    /// `failed`) a call made while a previous one is still outstanding rather
    /// than run two transactions concurrently.
    virtual void authenticate(const QString& username, const QString& password) = 0;

Q_SIGNALS:
    /// The credentials were accepted.
    void succeeded();
    /// The credentials were rejected (or the attempt could not complete);
    /// @p reason is a human-readable, non-sensitive explanation.
    void failed(const QString& reason);
};

} // namespace PhosphorServiceLock
