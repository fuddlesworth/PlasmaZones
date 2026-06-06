// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceSession/phosphorservicesession_export.h>

#include <QObject>
#include <QString>

#include <memory>

class QDBusConnection;

namespace PhosphorServiceSession {

/**
 * @brief logind session manager: capability queries, session/power actions,
 *        inhibitor locks, and the sleep + session signals.
 *
 * Talks to `org.freedesktop.login1.Manager` on the system bus. The capability
 * queries (`CanSuspend` and siblings) are read up front and cached; the actions
 * are capability-gated; the inhibitor locks let the shell lock before suspend
 * and own the power / lid keys. Inert when logind is unreachable (capabilities
 * Unknown, actions no-op with a warning, inhibitors not taken).
 *
 * The `(connection, service)` are injectable so the whole surface is testable
 * against a fake logind Manager with no real system bus and no daemon.
 *
 * The capability, action, inhibitor, and signal surface is filled in across
 * milestones 2-5; this is the skeleton (construction + the logind connection).
 */
class PHOSPHORSERVICESESSION_EXPORT SessionHost : public QObject
{
    Q_OBJECT

public:
    /// Production wiring: the system bus and `org.freedesktop.login1`.
    explicit SessionHost(QObject* parent = nullptr);

    /// Injectable wiring for tests / advanced consumers. @p connection and
    /// @p service select the bus and the logind well-known name; the fake-logind
    /// unit tests inject a session-bus Manager so no real logind is required.
    SessionHost(QDBusConnection connection, QString service, QObject* parent = nullptr);

    ~SessionHost() override;

private:
    Q_DISABLE_COPY_MOVE(SessionHost)
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceSession
