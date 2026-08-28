// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcutmanager.h"
#include "shortcutmanager_ids.h"
#include "shortcutmanager_table.h"

#include "config/configdefaults.h"
#include "config/settings.h"
#include "core/platform/logging.h"
#include "phosphor_i18n.h"

#include <PhosphorShortcuts/Factory.h>
#include <PhosphorShortcuts/IBackend.h>
#include <PhosphorShortcuts/Registry.h>

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <algorithm>

namespace PlasmaZones {

namespace {

// Shortcut ids + indexed-slot id helpers live in shortcutmanager_ids.h
// (shared with the cheatsheet catalog TU).
using namespace ShortcutIds;
using ShortcutTable::StaticEntry;

// The workspace slot families are registered from the indexed loops below,
// but their config keys are bounded by the settings-side constant. Tie the
// two together so raising one without the other fails to compile rather than
// silently registering a slot the config cannot store.
static_assert(kIndexedSlotCount == ConfigDefaults::WorkspaceSlotCount,
              "workspace slot families are registered with kIndexedSlotCount; keep it equal to the config bound");

// Indexed slot defaults — array of static accessors so the slot loop can
// resolve defaults by index without one constexpr-if branch per slot.
using DefaultGetter = QString (*)();
constexpr DefaultGetter kQuickLayoutDefaults[kIndexedSlotCount] = {
    &ConfigDefaults::quickLayout1Shortcut, &ConfigDefaults::quickLayout2Shortcut, &ConfigDefaults::quickLayout3Shortcut,
    &ConfigDefaults::quickLayout4Shortcut, &ConfigDefaults::quickLayout5Shortcut, &ConfigDefaults::quickLayout6Shortcut,
    &ConfigDefaults::quickLayout7Shortcut, &ConfigDefaults::quickLayout8Shortcut, &ConfigDefaults::quickLayout9Shortcut,
};
constexpr DefaultGetter kWorkspaceMoveSlotDefaults[kIndexedSlotCount] = {
    &ConfigDefaults::workspaceMoveSlot1Shortcut, &ConfigDefaults::workspaceMoveSlot2Shortcut,
    &ConfigDefaults::workspaceMoveSlot3Shortcut, &ConfigDefaults::workspaceMoveSlot4Shortcut,
    &ConfigDefaults::workspaceMoveSlot5Shortcut, &ConfigDefaults::workspaceMoveSlot6Shortcut,
    &ConfigDefaults::workspaceMoveSlot7Shortcut, &ConfigDefaults::workspaceMoveSlot8Shortcut,
    &ConfigDefaults::workspaceMoveSlot9Shortcut,
};
constexpr DefaultGetter kSnapToZoneDefaults[kIndexedSlotCount] = {
    &ConfigDefaults::snapToZone1Shortcut, &ConfigDefaults::snapToZone2Shortcut, &ConfigDefaults::snapToZone3Shortcut,
    &ConfigDefaults::snapToZone4Shortcut, &ConfigDefaults::snapToZone5Shortcut, &ConfigDefaults::snapToZone6Shortcut,
    &ConfigDefaults::snapToZone7Shortcut, &ConfigDefaults::snapToZone8Shortcut, &ConfigDefaults::snapToZone9Shortcut,
};

/// The feature-gated dynamic-workspaces family, ENUMERATED rather than
/// matched on an id prefix. A `startsWith("workspace_")` scan is
/// position-dependent in the wrong direction: it swept in any unrelated id
/// that happened to be spelled that way, and just as silently dropped a
/// workspace action that was not. The two slot families stay prefix-matched
/// because their prefix IS their definition.
bool isWorkspaceShortcutId(const QString& id)
{
    static const QSet<QString> kWorkspaceIds = {
        QString::fromLatin1(kIdWorkspaceFocusUp),           QString::fromLatin1(kIdWorkspaceFocusDown),
        QString::fromLatin1(kIdWorkspaceMoveWindowUp),      QString::fromLatin1(kIdWorkspaceMoveWindowDown),
        QString::fromLatin1(kIdWorkspaceMoveColumnUp),      QString::fromLatin1(kIdWorkspaceMoveColumnDown),
        QString::fromLatin1(kIdWorkspaceReorderUp),         QString::fromLatin1(kIdWorkspaceReorderDown),
        QString::fromLatin1(kIdWorkspaceMoveToMonitorLeft), QString::fromLatin1(kIdWorkspaceMoveToMonitorRight)};
    return kWorkspaceIds.contains(id) || id.startsWith(QLatin1String(kWorkspaceMoveSlotPrefix))
        || id.startsWith(QLatin1String(kWorkspaceFocusSlotPrefix));
}

// QKeySequence(QString) silently returns an empty sequence on malformed
// input. Wrap with a warning log so a typo in the config surfaces from the
// logs instead of silently disabling a shortcut. The warning latches per
// (id, spelling): the currentSeq lambdas re-parse every entry on every
// settings save, and a persistently malformed value would otherwise re-log
// the identical line for the life of the process.
QKeySequence parseSequence(const QString& raw, const QString& contextId)
{
    if (raw.isEmpty()) {
        return {};
    }
    QKeySequence seq(raw);
    if (seq.isEmpty()) {
        // Main-thread only: the warn-once latch's insert is not synchronized
        // (zero-init of the static is thread-safe, the mutation is not), and
        // every current caller runs on the daemon main thread.
        static QHash<QString, QString> warnedSpellings;
        if (warnedSpellings.value(contextId) != raw) {
            warnedSpellings.insert(contextId, raw);
            qCWarning(lcShortcuts) << "Failed to parse shortcut sequence for" << contextId << ":" << raw;
        }
    }
    return seq;
}

} // namespace

ShortcutManager::ShortcutManager(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    Q_ASSERT(settings);
    if (!settings) {
        // Release-build pair for the assert above. Every Entry::currentSeq
        // lambda captures this pointer raw, so a built table would deref null
        // on its first rebind. The manager instead stays inert: the guard at
        // the top of registerShortcuts() keeps m_entries empty, and every
        // other public entry point already no-ops on an empty table or a null
        // m_registry.
        qCCritical(lcShortcuts) << "ShortcutManager constructed without Settings — shortcuts disabled";
        return;
    }

    // Backend + Registry are NOT created here — they're created lazily in
    // registerShortcuts() so unregisterShortcuts() can fully release the
    // Portal session (XDG GlobalShortcuts has no per-id release; the only
    // way to drop grabs is to destroy the session). Re-creating in a
    // subsequent registerShortcuts() then goes through a fresh
    // CreateSession round-trip and the compositor sees clean state.
    // This matches the IBackend lifecycle documented on
    // PortalBackend::unregisterShortcut.

    // Settings::settingsChanged() fires both on individual setter emits AND
    // on bulk load (KCM reload). A single connect handles both cases; the
    // Registry internally no-ops rebinds that don't change the sequence, so
    // there's no cost to the full-table refresh pattern.
    connect(m_settings, &Settings::settingsChanged, this, &ShortcutManager::updateShortcuts);
}

ShortcutManager::~ShortcutManager() = default;

// The null branch is defence, not a live path: registerShortcuts() returns
// early without m_settings and nothing nulls it afterwards, so a fire lambda
// can only run with it set. Kept because these run from a callback the
// backend owns, and a default step beats a crash if that ever stops holding.
int ShortcutManager::scrollColumnWidthStepPercent() const
{
    return m_settings ? m_settings->scrollingColumnWidthStepPercent()
                      : ConfigDefaults::scrollingColumnWidthStepPercent();
}

int ShortcutManager::scrollWindowHeightStepPercent() const
{
    return m_settings ? m_settings->scrollingWindowHeightStepPercent()
                      : ConfigDefaults::scrollingWindowHeightStepPercent();
}

int ShortcutManager::scrollViewScrollStepPercent() const
{
    return m_settings ? m_settings->scrollingViewScrollStepPercent() : ConfigDefaults::scrollingViewScrollStepPercent();
}

void ShortcutManager::registerShortcuts()
{
    if (!m_settings) {
        // Constructed without Settings. buildEntries() would produce currentSeq
        // lambdas over a null pointer and the bind loop below would deref them,
        // so stop before the backend is even created — that keeps the ctor's
        // "shortcuts disabled" claim true instead of trading one crash site for
        // a Portal round-trip and the same crash.
        qCWarning(lcShortcuts) << "registerShortcuts(): no Settings — shortcuts stay disabled";
        return;
    }
    if (m_registrationInProgress) {
        qCWarning(lcShortcuts) << "registerShortcuts() called re-entrantly — ignoring";
        return;
    }
    if (!m_entries.isEmpty()) {
        // Already registered. A second registerShortcuts() call would
        // rebuild m_entries via buildEntries() (which clears the local
        // table) without first unbinding the prior set from the Registry,
        // leaving orphan registry entries pointing at stale callback
        // captures. The intended single-call lifecycle is enforced here
        // — call unregisterShortcuts() first if you want to rebuild.
        qCWarning(lcShortcuts) << "registerShortcuts(): already registered (" << m_entries.size()
                               << "entries) — call unregisterShortcuts() first to rebuild";
        return;
    }

    // Lazy backend creation. Lets unregisterShortcuts() fully drop the
    // Portal session and a subsequent re-register start fresh. unique_ptr
    // owns lifetime — DON'T also pass `this` as Qt parent, or the
    // backend/registry end up with two owners.
    if (!m_backend) {
        m_backend = PhosphorShortcuts::createBackend(PhosphorShortcuts::BackendHint::Auto, nullptr);
    }
    if (!m_registry) {
        m_registry = std::make_unique<PhosphorShortcuts::Registry>(m_backend.get(), nullptr);
        // External rebinds (System Settings, compositor-side) invalidate the
        // cheatsheet catalog's trigger strings. Registry filters to owned ids.
        connect(m_registry.get(), &PhosphorShortcuts::Registry::triggersChanged, this, [this](const QString&) {
            Q_EMIT cheatsheetModelChanged();
        });
    }

    m_registrationInProgress = true;

    buildEntries();

    for (const auto& e : std::as_const(m_entries)) {
        m_registry->bind(e.id, e.defaultSeq, e.description, e.fire);
        // Apply current sequence from settings after initial bind so the
        // first flush reflects the user's saved preference, not only the
        // compiled-in default.
        m_registry->rebind(e.id, e.currentSeq());
    }

    // Tag this batch so a settle that arrives late (either path) can tell
    // whether it still refers to the batch it was armed for.
    const quint64 generation = ++m_registrationGeneration;

    connect(
        m_registry.get(), &PhosphorShortcuts::Registry::ready, this,
        [this, generation] {
            settleRegistration(generation);
        },
        Qt::SingleShotConnection);

    // Fallback settle: a backend that never answers (Portal request lost, a
    // compositor that drops the reply) would otherwise leave
    // m_registrationInProgress true forever — every later settings rebind
    // silently defers and every adhoc grab queues undrained, which kills the
    // picker / snap-assist Escape binding for the rest of the session.
    QTimer::singleShot(kRegistrationSettleTimeoutMs, this, [this, generation] {
        if (generation != m_registrationGeneration || !m_registrationInProgress) {
            // Superseded by a later registerShortcuts()/unregisterShortcuts(),
            // or already settled by ready(). Settling here would clear the
            // in-flight flag of a DIFFERENT, still-pending batch.
            return;
        }
        qCWarning(lcShortcuts) << "Shortcut backend never reported ready after" << kRegistrationSettleTimeoutMs
                               << "ms — settling registration anyway";
        settleRegistration(generation);
    });

    m_registry->flush();
}

void ShortcutManager::settleRegistration(quint64 generation)
{
    if (generation != m_registrationGeneration || !m_registrationInProgress) {
        // Stale (a newer batch owns the manager) or already settled. Running
        // the body again would re-publish the catalog for no change, which
        // the cheatsheet re-pushes as visible churn.
        return;
    }
    m_registrationInProgress = false;
    qCInfo(lcShortcuts) << "Registered" << m_entries.size() << "shortcuts";
    // A settle with BOTH dirty settings and queued adhoc ops coalesces onto
    // ONE backend flush (see applyShortcutUpdates' doc): the rebinds stay
    // pending, the drain's trailing flush carries them, and only when
    // nothing was drained does the deferred flush run here. The
    // cheatsheetModelChanged for the rebinds is emitted AFTER whichever
    // flush carried them — the model reads the backend's read-back
    // (effectiveTriggers prefers it), so an emit ahead of the flush would
    // publish the pre-flush sequences and nothing re-emits until the next
    // settings save.
    bool modelEmitted = false;
    if (m_settingsDirty) {
        m_settingsDirty = false;
        const bool rebound = applyShortcutUpdates(/*deferFlush=*/true);
        // Replay any adhoc (un)registrations that arrived while the initial
        // batch was in flight. Must run AFTER the in-progress flag is
        // cleared so each drained op takes the immediate path instead of
        // re-queuing itself.
        const bool drainFlushed = drainPendingAdhocOps();
        if (rebound && !drainFlushed) {
            m_registry->flush();
        }
        if (rebound) {
            Q_EMIT cheatsheetModelChanged();
            modelEmitted = true;
        }
    } else {
        drainPendingAdhocOps();
    }
    // The catalog is first meaningful once the batch has settled (backend
    // read-back can answer now). One emit per settle: the dirty-settings
    // arm above may already have pushed it post-flush.
    if (!modelEmitted) {
        Q_EMIT cheatsheetModelChanged();
    }
}

bool ShortcutManager::updateShortcuts()
{
    return applyShortcutUpdates(/*deferFlush=*/false);
}

bool ShortcutManager::applyShortcutUpdates(bool deferFlush)
{
    if (m_registrationInProgress) {
        // Defer — the ready() callback above will call us again.
        m_settingsDirty = true;
        return false;
    }
    if (m_entries.isEmpty()) {
        return false;
    }
    // settingsChanged fires for every settings save; only a save that
    // actually moved a shortcut sequence needs a backend flush or a
    // cheatsheet refresh (a visible sheet re-pushes its model on the emit,
    // so over-notifying is user-visible churn, not just wasted work).
    if (!rebindAll()) {
        return false;
    }
    if (!deferFlush) {
        m_registry->flush();
        Q_EMIT cheatsheetModelChanged();
    }
    // Deferred: the CALLER owns both the flush and the post-flush emit —
    // emitting here would publish the pre-flush backend read-back.
    return true;
}

void ShortcutManager::setBackendForTesting(std::unique_ptr<PhosphorShortcuts::IBackend> backend)
{
    Q_ASSERT_X(m_entries.isEmpty(), "setBackendForTesting", "inject before registerShortcuts()");
    if (!m_entries.isEmpty()) {
        // Release-build pair for the assert. Resetting the registry while
        // m_entries stays populated would break the "entries non-empty
        // implies registry present" invariant updateShortcuts() relies on —
        // the next settings save would deref a null m_registry in
        // rebindAll(). Release the live registration first so the invariant
        // survives the misuse.
        qCWarning(lcShortcuts) << "setBackendForTesting() called after registerShortcuts() — releasing the live "
                                  "registration before installing the injected backend";
        unregisterShortcuts();
    }
    m_registry.reset();
    m_backend = std::move(backend);
}

void ShortcutManager::unregisterShortcuts()
{
    m_registrationInProgress = false;
    m_settingsDirty = false;
    // Supersede any armed fallback settle: its generation no longer matches.
    ++m_registrationGeneration;
    // Adhoc ops queued behind an in-flight initial batch belong to UI
    // contexts (e.g. an overlay's Escape grab) that no longer exist after
    // teardown; replaying them on the next registerShortcuts() would
    // re-bind a stale grab.
    m_pendingAdhocOps.clear();
    // Deliberately NO Registry::unbind() sweep over m_entries: unbind routes
    // to IBackend::unregisterShortcut, and on KGlobalAccel that purges the
    // id's persistent kglobalshortcutsrc record — the store where the user's
    // System Settings customisations live. This runs on the daemon stop()
    // path (SIGTERM included), so the sweep deleted every customised binding
    // on each service restart (discussion #851). Destroying the backend below
    // releases the grabs without touching persistent records: KGlobalAccel
    // drops its in-memory action table when the QActions die and the backend
    // destructor purges only transient ids, while PortalBackend releases
    // everything by closing the session.
    //
    // Deliberately NO cheatsheetModelChanged emit, unlike every other
    // catalog-changing path. The two callers are the daemon stop() path —
    // where the only consumer (refreshCheatsheetIfVisible) is being torn
    // down alongside the overlay service — and setBackendForTesting's
    // misuse branch, which is a programming-error recovery that no live
    // overlay should ever observe; neither needs an empty-catalog announce.
    m_entries.clear();

    // Tear down the backend so any Portal session is closed and grabs are
    // released compositor-side. KGlobalAccel and DBusTrigger backends are
    // cheap to recreate; PortalBackend's CreateSession is a single async
    // round-trip that's hidden by its existing m_flushRequested deferral.
    // A subsequent registerShortcuts() will lazily recreate both.
    m_registry.reset();
    m_backend.reset();
}

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
        || id.startsWith(QLatin1String(kSnapToZonePrefix)) || id.startsWith(QLatin1String(kWorkspaceMoveSlotPrefix))
        || id.startsWith(QLatin1String(kWorkspaceFocusSlotPrefix));
}
} // namespace

bool ShortcutManager::setForeignShortcuts(const QString& componentName, const QString& actionName,
                                          const QList<QKeySequence>& sequences)
{
    return m_registry ? m_registry->setForeignShortcuts(componentName, actionName, sequences) : false;
}

std::optional<QList<QKeySequence>> ShortcutManager::foreignShortcuts(const QString& componentName,
                                                                     const QString& actionName) const
{
    // No registry is a FAILED query, not an unbound action — nullopt, so the
    // caller keeps its hands off the chord.
    return m_registry ? m_registry->foreignShortcuts(componentName, actionName) : std::nullopt;
}

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

bool ShortcutManager::rebindAll()
{
    bool anyChanged = false;
    for (const auto& e : std::as_const(m_entries)) {
        const QKeySequence seq = e.currentSeq();
        if (m_registry->shortcut(e.id) != seq) {
            anyChanged = true;
        }
        m_registry->rebind(e.id, seq);
    }
    return anyChanged;
}

QStringList ShortcutManager::staticShortcutIds()
{
    const std::size_t count = ShortcutTable::staticEntryCount();
    const StaticEntry* const table = ShortcutTable::staticEntries();
    QStringList ids;
    ids.reserve(static_cast<int>(count));
    for (std::size_t i = 0; i < count; ++i) {
        ids.append(QString::fromLatin1(table[i].id));
    }
    return ids;
}

void ShortcutManager::buildEntries()
{
    const std::size_t staticCount = ShortcutTable::staticEntryCount();
    const StaticEntry* const staticTable = ShortcutTable::staticEntries();

    m_entries.clear();
    m_entries.reserve(static_cast<int>(staticCount) + 4 * kIndexedSlotCount);

    Settings* s = m_settings;
    ShortcutManager* sm = this;

    // Static table (shortcutmanager_table.cpp) — one entry per row.
    for (std::size_t i = 0; i < staticCount; ++i) {
        const StaticEntry& src = staticTable[i];
        Entry e;
        e.id = QString::fromLatin1(src.id);
        e.defaultSeq = parseSequence(src.defGetter(), e.id);
        e.description = PhosphorI18n::tr(src.label);
        const auto curGetter = src.curGetter;
        const QString idCopy = e.id;
        e.currentSeq = [s, curGetter, idCopy] {
            return parseSequence((s->*curGetter)(), idCopy);
        };
        const auto fire = src.fire;
        e.fire = [sm, fire] {
            fire(sm);
        };
        m_entries.push_back(std::move(e));
    }

    // Quick layout slots. Default getters indexed via kQuickLayoutDefaults;
    // current getter is Settings::quickLayoutShortcut(int) keyed by slot index.
    for (int i = 0; i < kIndexedSlotCount; ++i) {
        Entry e;
        e.id = quickLayoutId(i);
        e.defaultSeq = parseSequence(kQuickLayoutDefaults[i](), e.id);
        e.description = PhosphorI18n::tr("Apply Layout %1").arg(i + 1);
        const QString idCopy = e.id;
        e.currentSeq = [s, i, idCopy] {
            return parseSequence(s->quickLayoutShortcut(i), idCopy);
        };
        const int slot = i + 1;
        e.fire = [this, slot] {
            Q_EMIT quickLayoutRequested(slot);
        };
        m_entries.push_back(std::move(e));
    }

    // Workspace quick-shortcut slots. Quick-layout model: fixed factory
    // chords (KCM-rebindable); the slot's target workspace is assigned in
    // the settings app.
    for (int i = 0; i < kIndexedSlotCount; ++i) {
        Entry e;
        e.id = workspaceMoveSlotId(i);
        e.defaultSeq = parseSequence(kWorkspaceMoveSlotDefaults[i](), e.id);
        // "Slot", not a bare number: this is what KGlobalAccel shows in the
        // Shortcuts KCM, and the chord does NOT move the window to the Nth
        // workspace. It moves it to whichever named workspace the settings
        // app assigned to slot N, the quick-layout model.
        e.description = PhosphorI18n::tr("Move Window to Workspace Slot %1").arg(i + 1);
        const QString idCopy = e.id;
        e.currentSeq = [s, i, idCopy] {
            return parseSequence(s->workspaceMoveSlotShortcut(i), idCopy);
        };
        const int slot = i + 1;
        e.fire = [this, slot] {
            Q_EMIT workspaceMoveSlotRequested(slot);
        };
        m_entries.push_back(std::move(e));
    }

    // Workspace focus slots (plan §7): switch the acting monitor to workspace
    // N of its own list. Bound via KDE's Shortcuts settings only.
    //
    // Deliberately no defaultSeq: this family ships unbound. The digit row is
    // already spoken for by quick layouts and snap-to-zone, so shipping a
    // factory chord here would collide with one of them on a fresh install.
    for (int i = 0; i < kIndexedSlotCount; ++i) {
        Entry e;
        e.id = workspaceFocusSlotId(i);
        e.description = PhosphorI18n::tr("Focus Workspace %1").arg(i + 1);
        const QString idCopy = e.id;
        e.currentSeq = [s, i, idCopy] {
            return parseSequence(s->workspaceFocusSlotShortcut(i), idCopy);
        };
        const int slot = i + 1;
        e.fire = [this, slot] {
            Q_EMIT workspaceFocusSlotRequested(slot);
        };
        m_entries.push_back(std::move(e));
    }

    // Workspace chords grab only while the feature is on: with dynamic
    // workspaces off the verbs are inert, and a system-wide grab that does
    // nothing shadows those chords for every other application. rebindAll()
    // (riding every settings save, the enable toggle included) re-applies
    // the real sequences when the feature turns on.
    for (auto& e : m_entries) {
        if (isWorkspaceShortcutId(e.id)) {
            const auto inner = e.currentSeq;
            e.currentSeq = [s, inner] {
                return s->workspacesEnabled() ? inner() : QKeySequence();
            };
        }
    }

    // Snap-to-zone slots. Mirror of quick-layout slots — separate signal,
    // separate Settings getter, but identical structure.
    for (int i = 0; i < kIndexedSlotCount; ++i) {
        Entry e;
        e.id = snapToZoneId(i);
        e.defaultSeq = parseSequence(kSnapToZoneDefaults[i](), e.id);
        e.description = PhosphorI18n::tr("Snap to Zone %1").arg(i + 1);
        const QString idCopy = e.id;
        e.currentSeq = [s, i, idCopy] {
            return parseSequence(s->snapToZoneShortcut(i), idCopy);
        };
        const int zoneNumber = i + 1;
        e.fire = [this, zoneNumber] {
            Q_EMIT snapToZoneRequested(zoneNumber);
        };
        m_entries.push_back(std::move(e));
    }
}

} // namespace PlasmaZones
