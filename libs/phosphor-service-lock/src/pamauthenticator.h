// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "iauthenticator.h"

#include <QFutureWatcher>
#include <QString>

namespace PhosphorServiceLock {

/// Result of one PAM transaction, marshalled from the worker thread back to the
/// GUI thread by the QFutureWatcher.
struct PamResult
{
    bool success = false;
    QString reason;
};

/**
 * @brief IAuthenticator backed by PAM (`pam_authenticate` + `pam_acct_mgmt`).
 *
 * Runs the blocking PAM transaction on the global thread pool and delivers the
 * outcome on the GUI thread, so an authentication stack that sleeps (faildelay,
 * a network module) never stalls the event loop. One transaction at a time: a
 * call made while another is outstanding fails fast rather than racing two.
 *
 * The PAM service name (which `/etc/pam.d/<service>` stack validates the
 * password) is configurable; it defaults to `login`, which exists on every
 * system so the demo authenticates out of the box. A shell would point this at
 * its own stack.
 *
 * Threading: construct and call from the GUI thread.
 */
class PamAuthenticator : public IAuthenticator
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PamAuthenticator)

public:
    explicit PamAuthenticator(QString service = QStringLiteral("login"), QObject* parent = nullptr);
    ~PamAuthenticator() override;

    /// The PAM service name (the `/etc/pam.d/<service>` stack) this authenticator
    /// validates against.
    [[nodiscard]] QString service() const;

    void authenticate(const QString& username, const QString& password) override;

private:
    QString m_service;
    QFutureWatcher<PamResult> m_watcher;
};

} // namespace PhosphorServiceLock
