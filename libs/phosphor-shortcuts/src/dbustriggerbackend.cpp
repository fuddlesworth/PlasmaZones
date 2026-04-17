// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "backends.h"
#include "shortcutslogging.h"

#include <QDBusConnection>

namespace Phosphor::Shortcuts {

DBusTriggerBackend::DBusTriggerBackend(QObject* parent)
    : IBackend(parent)
{
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/Phosphor/Shortcuts"), this,
                                                 QDBusConnection::ExportScriptableSlots);
    qCInfo(lcPhosphorShortcuts) << "DBusTrigger backend active — bind shortcuts via:"
                                << "dbus-send --session --dest=<your-service> /org/Phosphor/Shortcuts"
                                << "org.Phosphor.Shortcuts.TriggerAction string:\"<id>\"";
}

void DBusTriggerBackend::registerShortcut(const QString& id, const QKeySequence& /*preferredTrigger*/,
                                          const QString& description)
{
    m_descriptions.insert(id, description);
}

void DBusTriggerBackend::updateShortcut(const QString& /*id*/, const QKeySequence& /*newTrigger*/)
{
    // DBusTrigger ignores key sequences — bindings live compositor-side.
}

void DBusTriggerBackend::unregisterShortcut(const QString& id)
{
    m_descriptions.remove(id);
}

void DBusTriggerBackend::flush()
{
    Q_EMIT ready();
}

void DBusTriggerBackend::TriggerAction(const QString& id)
{
    if (!m_descriptions.contains(id)) {
        qCWarning(lcPhosphorShortcuts) << "TriggerAction: unknown id" << id;
        return;
    }
    qCDebug(lcPhosphorShortcuts) << "TriggerAction:" << id;
    Q_EMIT activated(id);
}

} // namespace Phosphor::Shortcuts
