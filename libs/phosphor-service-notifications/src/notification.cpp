// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNotifications/Notification.h>

namespace PhosphorServiceNotifications {

// A Notification is constructed and mutated exclusively by NotificationServer
// (its parent and friend); the field writes and the changed() emit live in the
// server's ingest path. This translation unit exists so the QObject has an
// out-of-line definition and the moc output has a home.
Notification::Notification(uint id, QObject* parent)
    : QObject(parent)
    , m_id(id)
{
}

} // namespace PhosphorServiceNotifications
