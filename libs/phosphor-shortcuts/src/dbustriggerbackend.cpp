// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "backends.h"
#include "shortcutslogging.h"

#include <QDBusConnection>

namespace Phosphor::Shortcuts {

DBusTriggerBackend::DBusTriggerBackend(QObject* parent)
    : IBackend(parent)
{
    const bool ok = QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/Phosphor/Shortcuts"), this,
                                                                 QDBusConnection::ExportScriptableSlots);
    if (!ok) {
        qCWarning(lcPhosphorShortcuts)
            << "DBusTrigger backend failed to register /org/Phosphor/Shortcuts on the session bus"
            << "— another process on this D-Bus service name may already own the path."
            << "TriggerAction calls will not reach this instance.";
        return;
    }
    qCInfo(lcPhosphorShortcuts) << "DBusTrigger backend active — bind shortcuts via:"
                                << "dbus-send --session --dest=<your-service> /org/Phosphor/Shortcuts"
                                << "org.Phosphor.Shortcuts.TriggerAction string:\"<id>\"";
}

void DBusTriggerBackend::registerShortcut(const QString& id, const QKeySequence& /*defaultSeq*/,
                                          const QKeySequence& /*currentSeq*/, const QString& description)
{
    // DBusTrigger doesn't grab keys — the compositor binds them to dbus-send
    // calls externally — so both sequences are ignored.
    m_descriptions.insert(id, description);
}

void DBusTriggerBackend::updateShortcut(const QString& /*id*/, const QKeySequence& /*defaultSeq*/,
                                        const QKeySequence& /*newTrigger*/)
{
    // DBusTrigger ignores key sequences entirely — bindings live compositor-
    // side. Both args are accepted only to match the IBackend signature.
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
