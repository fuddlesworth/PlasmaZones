// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "backends.h"
#include "shortcutslogging.h"

#include <KGlobalAccel>
#include <QAction>
#include <QCoreApplication>
#include <QHash>

namespace Phosphor::Shortcuts {

// KGlobalAccel's C++ API trafficks exclusively in QAction*. We hide that
// behind the id-based IBackend interface by owning one QAction per id —
// created on registerShortcut(), objectName'd to the id (KGlobalAccel uses
// objectName as the persistent key on disk), and connected so QAction::triggered
// fans out to IBackend::activated().
//
// Component name: KGlobalAccel groups shortcuts by
// QCoreApplication::applicationName() at the time setShortcut() is first
// called. Callers should set an applicationName before instantiating this
// backend — otherwise shortcuts end up under an empty component which is
// hard to find in System Settings.

class KGlobalAccelBackend::Impl
{
public:
    struct Entry
    {
        QAction* action = nullptr;
        bool defaultApplied = false; // true after the first setDefaultShortcut call
    };
    QHash<QString, Entry> entries;
};

KGlobalAccelBackend::KGlobalAccelBackend(QObject* parent)
    : IBackend(parent)
    , m_impl(std::make_unique<Impl>())
{
    qCInfo(lcPhosphorShortcuts) << "KGlobalAccelBackend: active";
}

KGlobalAccelBackend::~KGlobalAccelBackend()
{
    // IMPORTANT: do NOT call KGlobalAccel::removeAllShortcuts here.
    //
    // removeAllShortcuts purges the binding from the persistent on-disk
    // registry (kglobalshortcutsrc). On a normal daemon shutdown that would
    // wipe every user-customised shortcut — users would reset to defaults
    // on each restart. KGlobalAccel cleans up its in-memory action table
    // automatically when the QAction is destroyed, so a plain delete is the
    // correct teardown path.
    for (auto& entry : m_impl->entries) {
        if (entry.action) {
            entry.action->deleteLater();
        }
    }
}

void KGlobalAccelBackend::registerShortcut(const QString& id, const QKeySequence& defaultSeq,
                                           const QKeySequence& currentSeq, const QString& description)
{
    auto& entry = m_impl->entries[id];
    if (!entry.action) {
        entry.action = new QAction(description, this);
        entry.action->setObjectName(id);
        entry.action->setProperty("componentName", QCoreApplication::applicationName());
        const QString idCopy = id;
        connect(entry.action, &QAction::triggered, this, [this, idCopy] {
            Q_EMIT activated(idCopy);
        });
    } else if (!description.isEmpty() && entry.action->text() != description) {
        entry.action->setText(description);
    }

    if (!entry.defaultApplied) {
        // setDefaultShortcut establishes the "reset to default" binding shown
        // in System Settings. Must be called with the compiled-in default
        // (NOT the user's current value) so "Reset to default" actually
        // resets to the factory default. Only needs to happen once per id —
        // repeating it on every re-register causes extra writes to
        // kglobalshortcutsrc.
        KGlobalAccel::self()->setDefaultShortcut(entry.action, {defaultSeq});
        entry.defaultApplied = true;
    }
    // setShortcut actually grabs the key. Uses the default autoloading flag
    // so any user override persisted in kglobalshortcutsrc wins over the
    // currentSeq passed here.
    KGlobalAccel::self()->setShortcut(entry.action, {currentSeq});
}

void KGlobalAccelBackend::updateShortcut(const QString& id, const QKeySequence& newTrigger)
{
    auto it = m_impl->entries.find(id);
    if (it == m_impl->entries.end() || !it->action) {
        qCWarning(lcPhosphorShortcuts) << "KGlobalAccel updateShortcut: unknown id" << id;
        return;
    }
    KGlobalAccel::self()->setShortcut(it->action, {newTrigger});
}

void KGlobalAccelBackend::unregisterShortcut(const QString& id)
{
    auto it = m_impl->entries.find(id);
    if (it == m_impl->entries.end()) {
        return;
    }
    QAction* action = it->action;
    m_impl->entries.erase(it);
    if (action) {
        // Explicit unregister IS the one path where we want to clear the
        // persistent binding — the consumer has asked to drop the shortcut
        // entirely (e.g. WindowDragAdaptor releasing the dynamic Escape
        // shortcut after a drag ends). Contrast with the destructor, which
        // preserves the on-disk state.
        KGlobalAccel::self()->removeAllShortcuts(action);
        action->deleteLater();
    }
}

void KGlobalAccelBackend::flush()
{
    // KGlobalAccel operations are synchronous per-call; there's no batch to
    // commit. Emit ready so Registry consumers can treat the two backends
    // uniformly.
    Q_EMIT ready();
}

} // namespace Phosphor::Shortcuts
