// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) seam for verifying a user's credentials. Production
// wraps PAM (PamAuthenticator); tests substitute a fake that resolves a result
// directly. Keeping the lock state machine behind this interface is what lets it
// be unit-tested without a real PAM stack or valid credentials.

#include <QObject>
#include <QString>

namespace PhosphorServiceLock {

/// The authentication surface the lock state machine drives: it verifies a
/// password for a user and reports the outcome asynchronously.
class IAuthenticator : public QObject
{
    Q_OBJECT

public:
    explicit IAuthenticator(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    // Out-of-line (defined in iauthenticator.cpp) so it anchors the vtable and
    // gives AUTOMOC a translation unit for the Q_OBJECT metaobject.
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
