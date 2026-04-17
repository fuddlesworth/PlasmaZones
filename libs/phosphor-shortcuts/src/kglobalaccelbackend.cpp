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
    QHash<QString, QAction*> actions;
};

KGlobalAccelBackend::KGlobalAccelBackend(QObject* parent)
    : IBackend(parent)
    , m_impl(std::make_unique<Impl>())
{
    qCInfo(lcPhosphorShortcuts) << "KGlobalAccelBackend: active";
}

KGlobalAccelBackend::~KGlobalAccelBackend()
{
    for (auto* action : std::as_const(m_impl->actions)) {
        if (!action) {
            continue;
        }
        KGlobalAccel::self()->removeAllShortcuts(action);
        action->deleteLater();
    }
}

void KGlobalAccelBackend::registerShortcut(const QString& id, const QKeySequence& preferredTrigger,
                                           const QString& description)
{
    auto it = m_impl->actions.find(id);
    QAction* action = (it != m_impl->actions.end()) ? *it : nullptr;
    if (!action) {
        action = new QAction(description, this);
        action->setObjectName(id);
        action->setProperty("componentName", QCoreApplication::applicationName());
        connect(action, &QAction::triggered, this, [this, id] {
            Q_EMIT activated(id);
        });
        m_impl->actions.insert(id, action);
    } else if (!description.isEmpty() && action->text() != description) {
        action->setText(description);
    }

    // setDefaultShortcut establishes the "reset to default" binding shown in
    // System Settings; setShortcut actually grabs the key. The pair mirrors
    // KF6 examples and survives a KGlobalAccel-side config reset.
    KGlobalAccel::self()->setDefaultShortcut(action, {preferredTrigger});
    KGlobalAccel::self()->setShortcut(action, {preferredTrigger});
}

void KGlobalAccelBackend::updateShortcut(const QString& id, const QKeySequence& newTrigger)
{
    auto it = m_impl->actions.find(id);
    if (it == m_impl->actions.end() || !*it) {
        qCWarning(lcPhosphorShortcuts) << "KGlobalAccel updateShortcut: unknown id" << id;
        return;
    }
    KGlobalAccel::self()->setShortcut(*it, {newTrigger});
}

void KGlobalAccelBackend::unregisterShortcut(const QString& id)
{
    auto it = m_impl->actions.find(id);
    if (it == m_impl->actions.end()) {
        return;
    }
    QAction* action = *it;
    m_impl->actions.erase(it);
    if (action) {
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
