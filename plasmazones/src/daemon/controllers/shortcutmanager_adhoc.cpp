// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The ADHOC half of ShortcutManager: transient bindings a caller registers at
// runtime and owns for as long as its surface is up (the layout picker's six
// keys are the standing example), as opposed to the persistent
// settings-driven table in shortcutmanager.cpp.
//
// Split from that file when it crossed the size ceiling. The seam is a real
// concern rather than a line count: everything here exists because these
// bindings have a caller's lifetime instead of a setting's, which is why it
// carries its own collision guard against the persistent ids and its own
// pending-op queue for the window before the registry exists.

#include "shortcutmanager.h"
#include "shortcutmanager_ids.h"

#include "core/platform/logging.h"

#include <PhosphorShortcuts/Registry.h>

#include <QSet>
#include <QStringList>

#include <algorithm>

namespace PlasmaZones {

// Ids are compared against the STATIC table and the indexed slot prefixes
// (shared with the registration TU).
using namespace ShortcutIds;

namespace {
/// Collision test shared by BOTH halves of the adhoc pair: an adhoc id
/// colliding with the settings-driven table (or the indexed slot prefixes)
/// would rebind the persistent entry as transient on register, and — the
/// destructive half — unregisterAdhocShortcut on such an id purges the
/// persistent binding's saved kglobalshortcutsrc record (Registry::unbind
/// → KGlobalAccel removeAllShortcuts), the exact wipe unregisterShortcuts()
/// exists to avoid (discussion #851). register returns void and only logs
/// on rejection, so a caller whose register was refused still calls the
/// matching unregister — the guard must therefore hold on BOTH sides and
/// in drainPendingAdhocOps' direct-unbind arm.
bool collidesWithSettingsDrivenId(const QString& id)
{
    // Hoisted once: staticShortcutIds() rebuilds the whole static-id list
    // per call, and this guard runs per binding of every adhoc batch (six
    // per layout-picker show). The table has internal linkage and never
    // changes at runtime, so a function-local static set is sound.
    static const QSet<QString> kStaticIdSet = [] {
        const QStringList ids = ShortcutManager::staticShortcutIds();
        return QSet<QString>(ids.cbegin(), ids.cend());
    }();
    return kStaticIdSet.contains(id) || id.startsWith(QLatin1String(kQuickLayoutPrefix))
        || id.startsWith(QLatin1String(kSnapToZonePrefix)) || id.startsWith(QLatin1String(kScrollFocusTabPrefix))
        || id.startsWith(QLatin1String(kWorkspaceMoveSlotPrefix))
        || id.startsWith(QLatin1String(kWorkspaceFocusSlotPrefix))
        || id.startsWith(QLatin1String(kWorkspaceNamedFocusPrefix))
        || id.startsWith(QLatin1String(kWorkspaceNamedMovePrefix));
}
} // namespace

void ShortcutManager::registerAdhocShortcut(const QString& id, const QKeySequence& sequence, const QString& description,
                                            std::function<void()> callback)
{
    if (!m_registry) {
        // Backend lifecycle: registerShortcuts() must run first so the
        // Registry exists. Adhoc consumers (drag adaptor) wire up post-init
        // in response to user actions, so this is a programming error if
        // it ever fires.
        qCWarning(lcShortcuts) << "registerAdhocShortcut(" << id
                               << "): no registry — registerShortcuts() must be called before adhoc binding";
        return;
    }
    // Boundary validation — see collidesWithSettingsDrivenId for why this
    // guard exists and why the UNREGISTER side carries it too.
    if (collidesWithSettingsDrivenId(id)) {
        qCWarning(lcShortcuts) << "registerAdhocShortcut(" << id
                               << "): id collides with a settings-driven shortcut — rejected";
        return;
    }
    // Adhoc registration during the initial settings-driven batch would race
    // the batched BindShortcuts on the Portal backend (the per-batch Request
    // subscription gets torn down mid-flight when the adhoc flush fires,
    // leaving m_confirmedBound out of sync with the compositor). Queue the
    // request instead of dropping it — the Registry ready() callback drains
    // pending ops after the initial batch settles, so a drag that fires in
    // the first few hundred ms after daemon startup still gets its Escape
    // cancel-overlay grab (just slightly later). De-dup: any earlier
    // (un)register for the same id is superseded — last write wins.
    if (m_registrationInProgress) {
        erasePendingAdhocOps(id);
        m_pendingAdhocOps.push_back({PendingAdhocOp::Register, id, sequence, description, std::move(callback)});
        return;
    }
    m_registry->bind(id, sequence, description, std::move(callback), /*persistent=*/false);
    // Re-register path: a prior adhoc with the same id would have set
    // currentSeq via bind() already; a second bind() preserves currentSeq by
    // contract (to protect user rebinds on the settings-driven table), so
    // force the requested sequence here for the adhoc case where the caller
    // wants the new sequence to win. For a fresh id the rebind is a same-
    // sequence short-circuit inside Registry.
    m_registry->rebind(id, sequence);
    if (!m_suppressAdhocFlush) {
        m_registry->flush();
    }
}

void ShortcutManager::registerAdhocShortcuts(
    const QVector<PhosphorShortcutsIntegration::IAdhocRegistrar::AdhocBinding>& bindings)
{
    // Every entry binds through the per-id path with its flush deferred to a
    // single trailing call: on the Portal backend each flush issues a
    // BindShortcuts whose Request supersedes the prior in-flight Response, so
    // a burst of per-id flushes (the six layout-picker navigation grabs)
    // would lose the read-back confirmation for all but the last.
    if (bindings.isEmpty()) {
        return;
    }
    m_suppressAdhocFlush = true;
    for (const auto& binding : bindings) {
        registerAdhocShortcut(binding.id, binding.sequence, binding.description, binding.callback);
    }
    m_suppressAdhocFlush = false;
    if (m_registry && !m_registrationInProgress) {
        m_registry->flush();
    }
}

void ShortcutManager::unregisterAdhocShortcuts(const QStringList& ids)
{
    if (ids.isEmpty()) {
        return;
    }
    m_suppressAdhocFlush = true;
    for (const QString& id : ids) {
        unregisterAdhocShortcut(id);
    }
    m_suppressAdhocFlush = false;
    if (m_registry && !m_registrationInProgress) {
        m_registry->flush();
    }
}

void ShortcutManager::unregisterAdhocShortcut(const QString& id)
{
    if (!m_registry) {
        // Backend already torn down (e.g. via unregisterShortcuts() during
        // shutdown) — release is implicit since the entire session is gone.
        return;
    }
    // The destructive half of the register-side collision guard: unbind on
    // a settings-driven id reaches KGlobalAccel::removeAllShortcuts and
    // purges the user's saved binding. A caller whose register was refused
    // (void return, warning only) still calls this in its teardown, so the
    // refusal must be symmetric.
    if (collidesWithSettingsDrivenId(id)) {
        qCWarning(lcShortcuts) << "unregisterAdhocShortcut(" << id
                               << "): id collides with a settings-driven shortcut — rejected";
        return;
    }
    // Same race as registerAdhocShortcut: if the initial batch is still in
    // flight, queue the unregister. Supersede any pending register for the
    // same id — register-then-unregister before the batch drains is just a
    // no-op.
    if (m_registrationInProgress) {
        const bool hadPendingRegister =
            std::any_of(m_pendingAdhocOps.cbegin(), m_pendingAdhocOps.cend(), [&id](const PendingAdhocOp& op) {
                return op.id == id && op.kind == PendingAdhocOp::Register;
            });
        erasePendingAdhocOps(id);
        // Only queue an Unregister if the id hasn't already been seen as a
        // pending Register — cancelling a never-sent register is a no-op and
        // queuing Unregister for it would send a spurious release to the
        // backend for an id it never heard of.
        if (!hadPendingRegister) {
            m_pendingAdhocOps.push_back({PendingAdhocOp::Unregister, id, {}, {}, {}});
        }
        return;
    }
    m_registry->unbind(id);
    if (!m_suppressAdhocFlush) {
        m_registry->flush();
    }
}

void ShortcutManager::erasePendingAdhocOps(const QString& id)
{
    m_pendingAdhocOps.erase(std::remove_if(m_pendingAdhocOps.begin(), m_pendingAdhocOps.end(),
                                           [&id](const PendingAdhocOp& op) {
                                               return op.id == id;
                                           }),
                            m_pendingAdhocOps.end());
}

bool ShortcutManager::drainPendingAdhocOps()
{
    if (m_pendingAdhocOps.isEmpty()) {
        return false;
    }
    // Swap so the member queue is empty for the duration; the registration
    // flag is already cleared, so a re-entrant (un)registerAdhocShortcut
    // binds immediately instead of re-queuing into the vector we're walking.
    QVector<PendingAdhocOp> ops;
    m_pendingAdhocOps.swap(ops);
    qCInfo(lcShortcuts) << "Draining" << ops.size() << "deferred adhoc shortcut op(s) after initial registration";
    // Bind/unbind directly instead of replaying through the public methods:
    // each of those flushes per call, and a drained batch only needs one
    // backend round-trip at the end. The registration flag is already
    // cleared, so nothing re-queues.
    for (auto& op : ops) {
        // Defence in depth: the queue is fed only by the public methods,
        // which already reject colliding ids before queuing, so this guard
        // should never fire. It stays because this direct arm bypasses those
        // methods, and an Unregister for a colliding id slipping through
        // would purge the persistent record the public path refuses to.
        if (collidesWithSettingsDrivenId(op.id)) {
            qCWarning(lcShortcuts) << "drainPendingAdhocOps: dropping colliding adhoc op for" << op.id;
            continue;
        }
        if (op.kind == PendingAdhocOp::Register) {
            m_registry->bind(op.id, op.sequence, op.description, std::move(op.callback), /*persistent=*/false);
            m_registry->rebind(op.id, op.sequence);
        } else {
            m_registry->unbind(op.id);
        }
    }
    m_registry->flush();
    return true;
}

} // namespace PlasmaZones
