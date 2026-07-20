// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "backends.h"
#include "shortcutslogging.h"

#include <KGlobalAccel>
#include <QAction>
#include <QCoreApplication>
#include <QHash>

namespace PhosphorShortcuts {

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
        // Last values forwarded to KGlobalAccel. Kept so registerShortcut can
        // skip redundant setDefaultShortcut / setShortcut calls when neither
        // sequence has changed — each setShortcut otherwise costs a D-Bus
        // round-trip and a write into kglobalshortcutsrc. Empty QKeySequence
        // means "nothing sent yet".
        QKeySequence lastDefault;
        QKeySequence lastCurrent;
        // True for transient/ad-hoc shortcuts (e.g. the cancel-overlay
        // Escape grab bound only while a drag or overlay is up). The
        // backend has to purge these from kglobalshortcutsrc on
        // destruction — otherwise an unexpected daemon exit (crash, kill,
        // segfault) leaves the entry visible in System Settings AND with
        // KGlobalAccel still grabbing the key system-wide on next login,
        // including over fullscreen games (discussion #461 item 14).
        // Persistent (false) entries always go through removeAllShortcuts
        // on unregisterShortcut, so the only leak window is unexpected
        // process exit.
        bool persistent = true;
    };
    QHash<QString, Entry> entries;
    // Last KGlobalAccel::shortcut() value reported via triggersChanged, per
    // id. Gates the globalShortcutChanged fan-out so self-originated echoes
    // and repeats don't trigger redundant consumer rebuilds.
    QHash<QString, QList<QKeySequence>> lastReportedTriggers;
};

KGlobalAccelBackend::KGlobalAccelBackend(QObject* parent)
    : IBackend(parent)
    , m_impl(std::make_unique<Impl>())
{
    // KGlobalAccel groups shortcuts by QCoreApplication::applicationName() at
    // the time setShortcut() is first called. An empty applicationName silently
    // groups every shortcut under "" — they show up under no component in
    // System Settings and persistent kglobalshortcutsrc entries are unfindable.
    // Assert in debug; warn in release. Callers MUST set applicationName before
    // constructing this backend (typically in main() before the daemon ctor).
    Q_ASSERT_X(!QCoreApplication::applicationName().isEmpty(), "KGlobalAccelBackend",
               "QCoreApplication::applicationName() must be set before constructing the backend");
    if (QCoreApplication::applicationName().isEmpty()) {
        qCWarning(lcPhosphorShortcuts)
            << "KGlobalAccelBackend: applicationName is empty — shortcuts will register under no component"
            << "and System Settings will not surface them. Set QCoreApplication::setApplicationName() first.";
    }
    qCInfo(lcPhosphorShortcuts) << "KGlobalAccelBackend: active (component:" << QCoreApplication::applicationName()
                                << ")";

    // Surface external rebinds (the user editing our component in System
    // Settings) as triggersChanged so consumers displaying bindings can
    // re-query currentTriggers(). The signal passes the QAction; it is ours
    // iff its objectName matches a registered id (registerShortcut()
    // objectNames every action to its id). Deduped against the last
    // reported value: kglobalaccel may echo changes we originated
    // ourselves (setShortcut during registration/rebind), and consumers
    // rebuild their whole display model per emission — without the gate a
    // bulk registration fans out into dozens of identical rebuilds.
    connect(KGlobalAccel::self(), &KGlobalAccel::globalShortcutChanged, this,
            [this](QAction* action, const QKeySequence& /*seq*/) {
                if (!action) {
                    return;
                }
                const QString id = action->objectName();
                const auto it = m_impl->entries.constFind(id);
                if (it == m_impl->entries.constEnd() || it->action != action) {
                    return;
                }
                const QList<QKeySequence> seqs = KGlobalAccel::self()->shortcut(action);
                const auto lastIt = m_impl->lastReportedTriggers.constFind(id);
                if (lastIt != m_impl->lastReportedTriggers.constEnd() && *lastIt == seqs) {
                    return;
                }
                m_impl->lastReportedTriggers.insert(id, seqs);
                Q_EMIT triggersChanged(id);
            });
}

KGlobalAccelBackend::~KGlobalAccelBackend()
{
    // IMPORTANT: do NOT call KGlobalAccel::removeAllShortcuts on PERSISTENT
    // shortcuts here.
    //
    // removeAllShortcuts purges the binding from the persistent on-disk
    // registry (kglobalshortcutsrc). On a normal daemon shutdown that would
    // wipe every user-customised shortcut — users would reset to defaults
    // on each restart. KGlobalAccel cleans up its in-memory action table
    // automatically when the QAction is destroyed, so a plain delete is the
    // correct teardown path for persistent ids.
    //
    // For NON-PERSISTENT (transient) shortcuts the situation is the
    // opposite: they're only ever supposed to be active while a specific
    // UI state is up (drag in progress, overlay visible, layout picker
    // open). On a normal shutdown the consumer has already called
    // unregisterShortcut. On an UNEXPECTED exit (crash, SIGKILL, segfault
    // mid-drag) the entry would otherwise survive — KGlobalAccel keeps
    // the persistent record AND keeps grabbing the key on next login,
    // routing it to a daemon action that no longer exists. End-user
    // surface: a stale "Esc" grab in System Settings that disables the
    // Escape key in fullscreen games even when no drag is happening
    // (discussion #461 item 14).
    //
    // Purge the on-disk entry for transient ids here so even an abnormal
    // exit can't outlive the daemon. Persistent ids skip the purge so
    // user customisations survive the shutdown.
    //
    // The QActions are parented to `this`, so Qt's ~QObject cleanup would
    // destroy them anyway. We delete explicitly to clear the hash table's
    // pointers in the right order (action gone, entry cleared) and avoid
    // relying on implicit child-destruction ordering.
    //
    // disconnect() before delete: the per-action triggered lambda captures
    // `this` (the backend). If a queued QAction::triggered event is
    // dispatched during the delete loop (vanishingly unlikely on the same
    // thread, but possible across D-Bus dispatch), the lambda would emit
    // activated() on a mid-destruction QObject. Severing the connection
    // first makes the race unreachable.
    for (auto& entry : m_impl->entries) {
        if (entry.action) {
            entry.action->disconnect();
            if (!entry.persistent) {
                KGlobalAccel::self()->removeAllShortcuts(entry.action);
            }
            delete entry.action;
            entry.action = nullptr;
        }
    }
}

void KGlobalAccelBackend::registerShortcut(const QString& id, const QKeySequence& defaultSeq,
                                           const QKeySequence& currentSeq, const QString& description, bool persistent)
{
    auto& entry = m_impl->entries[id];
    const bool firstRegistration = !entry.action;
    if (firstRegistration) {
        entry.action = new QAction(description, this);
        entry.action->setObjectName(id);
        entry.action->setProperty("componentName", QCoreApplication::applicationName());
        const QString idCopy = id;
        connect(entry.action, &QAction::triggered, this, [this, idCopy] {
            Q_EMIT activated(idCopy);
        });
        // For TRANSIENT shortcuts, scrub any stale on-disk record left by a
        // prior daemon process that exited abnormally before its destructor
        // ran. Without this, KGlobalAccel autoloads the orphan entry on the
        // next setShortcut call below, the user-visible "Cancel Zone Overlay
        // = Esc" registration in System Settings persists, and Esc keeps
        // being grabbed compositor-side outside any drag/overlay context —
        // including over fullscreen games (discussion #461 item 14).
        // Persistent ids deliberately keep their on-disk record; that's
        // where user customisations live.
        //
        // Note this is the FIRST-registration scrub, so after an abnormal
        // exit a leaked transient entry is only cleared the next time that
        // transient shortcut is registered again (i.e. the next drag/overlay
        // that needs it). That is the self-heal path; the destructor below is
        // the clean-shutdown path. The only uncovered window is "crashed and
        // the transient shortcut is never registered again" — acceptable, as
        // the grab is harmless until the key is actually pressed and the next
        // use scrubs it.
        if (!persistent) {
            KGlobalAccel::self()->removeAllShortcuts(entry.action);
        }
    } else if (!description.isEmpty() && entry.action->text() != description) {
        entry.action->setText(description);
    }
    // Latest persistent flag wins. A re-bind that escalates a transient
    // grab to persistent (or vice versa) updates the destructor-time
    // purge decision accordingly. Same-id rebinds with a different flag
    // are uncommon but not forbidden by the IBackend contract.
    entry.persistent = persistent;

    // setDefaultShortcut establishes the "reset to default" binding shown in
    // System Settings. Must be called with the compiled-in default (NOT the
    // user's current value) so "Reset to default" actually resets to the
    // factory default. Skip the call when the compiled-in default hasn't
    // changed since the last send — each call otherwise costs a D-Bus
    // round-trip and a kglobalshortcutsrc write.
    if (entry.lastDefault != defaultSeq) {
        KGlobalAccel::self()->setDefaultShortcut(entry.action, {defaultSeq});
        entry.lastDefault = defaultSeq;
    }
    // setShortcut actually grabs the key. Uses the default autoloading flag
    // so any user override persisted in kglobalshortcutsrc wins over the
    // currentSeq passed here. Same "only if changed" gate as above.
    if (entry.lastCurrent != currentSeq) {
        KGlobalAccel::self()->setShortcut(entry.action, {currentSeq});
        entry.lastCurrent = currentSeq;
    }
    // Seed the dedup gate with the post-set effective value so
    // kglobalaccel's echo of OUR OWN setShortcut doesn't fire
    // triggersChanged (the signal's contract is out-of-band changes only,
    // and an unseeded first echo fans a bulk registration into N consumer
    // rebuilds). Read back rather than assume currentSeq: with the
    // autoloading flag a persisted user override wins over what we just
    // pushed, and seeding the wrong value would suppress-or-emit
    // inconsistently. A genuinely external change still reports, because
    // it differs from this seed. Known boundary: if an external rebind
    // has landed in kglobalaccel's table but its change echo is still
    // queued when a re-register interleaves, this seed absorbs that echo
    // and one display refresh is lost — the grab itself stays correct
    // (autoload), so the cost is a stale label until the next report.
    m_impl->lastReportedTriggers.insert(id, KGlobalAccel::self()->shortcut(entry.action));
}

void KGlobalAccelBackend::updateShortcut(const QString& id, const QKeySequence& /*defaultSeq*/,
                                         const QKeySequence& newTrigger)
{
    // defaultSeq is intentionally unused: setDefaultShortcut is refreshed via
    // registerShortcut whenever the compiled-in default changes (Registry
    // re-invokes registerShortcut for default deltas; updateShortcut only
    // fires for currentSeq-only deltas). Avoiding a redundant
    // setDefaultShortcut call here also saves a D-Bus round-trip and a
    // kglobalshortcutsrc write per update.
    auto it = m_impl->entries.find(id);
    if (it == m_impl->entries.end() || !it->action) {
        qCWarning(lcPhosphorShortcuts) << "KGlobalAccel updateShortcut: unknown id" << id;
        return;
    }
    if (it->lastCurrent == newTrigger) {
        return;
    }
    // NoAutoloading, unlike registerShortcut: registration already SAVED a
    // binding to kglobalshortcutsrc, so with the default autoloading flag
    // this call would load that saved value back and silently ignore
    // newTrigger — making every programmatic rebind a no-op (verified
    // against a live daemon by test_kglobalaccel_backend). updateShortcut
    // only fires when the consumer's own config value actually changed
    // (Registry short-circuits same-sequence rebinds), i.e. an explicit
    // rebind that must win; NoAutoloading applies it AND persists it, so
    // System Settings stays in sync. Startup adoption of a System
    // Settings override still works — that path is registerShortcut's
    // autoloading setShortcut, not this one.
    KGlobalAccel::self()->setShortcut(it->action, {newTrigger}, KGlobalAccel::NoAutoloading);
    it->lastCurrent = newTrigger;
    // Same self-echo seeding as registerShortcut — see the note there.
    m_impl->lastReportedTriggers.insert(id, KGlobalAccel::self()->shortcut(it->action));
}

void KGlobalAccelBackend::unregisterShortcut(const QString& id)
{
    auto it = m_impl->entries.find(id);
    if (it == m_impl->entries.end()) {
        return;
    }
    QAction* action = it->action;
    m_impl->entries.erase(it);
    // Drop the dedup seed with the entry: a stale value would suppress the
    // first legitimate triggersChanged after a later re-register of the
    // same id (the transient drag-cancel Escape registers per drag), and
    // ids never re-registered would accumulate dead map entries.
    m_impl->lastReportedTriggers.remove(id);
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

std::optional<QStringList> KGlobalAccelBackend::currentTriggers(const QString& id) const
{
    const auto it = m_impl->entries.constFind(id);
    if (it == m_impl->entries.constEnd() || !it->action) {
        // Unknown id — nothing to report; the caller's own value stands.
        return std::nullopt;
    }
    // KGlobalAccel::shortcut returns the ACTIVE sequences — including a user
    // override persisted in kglobalshortcutsrc, which setShortcut's
    // autoloading behaviour lets win over whatever currentSeq we pushed.
    // This is exactly the "what does the user really press" answer that our
    // own bookkeeping (lastCurrent) cannot give. An engaged-but-empty
    // result is AUTHORITATIVE: the user cleared the binding, and falling
    // back to the stored sequence would display a key that no longer works.
    QStringList out;
    const QList<QKeySequence> seqs = KGlobalAccel::self()->shortcut(it->action);
    for (const QKeySequence& seq : seqs) {
        if (!seq.isEmpty()) {
            out.append(seq.toString(QKeySequence::PortableText));
        }
    }
    return out;
}

void KGlobalAccelBackend::flush()
{
    // KGlobalAccel operations are synchronous per-call; there's no batch to
    // commit. Emit ready so Registry consumers can treat the two backends
    // uniformly.
    Q_EMIT ready();
}

} // namespace PhosphorShortcuts
