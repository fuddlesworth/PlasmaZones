// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Apply / discard / reset lifecycle for SettingsController:
//   * adoptOnDiskState() — adopt whatever is on disk as the session state
//                   (settings, local rule store, daemon rules, screens,
//                   layouts) and drop every staged edit. Shared by load()
//                   and the config-import success path.
//   * load()      — delegate the whole reload to adoptOnDiskState() and
//                   clear the dirty state when it comes back clean. The
//                   reload steps themselves (settings, rule store, daemon
//                   rules, screens, layouts, staging) all live in that
//                   shared helper, because the import path needs exactly
//                   the same sequence.
//   * save()      — flush dirty settings to disk, notify the daemon to
//                   reload, and push the three staged D-Bus surfaces
//                   (virtual screens, quick slots, assignments). Per-page
//                   stagers (animations + rules) commit through the
//                   framework's own domain walk, not from here.
//   * defaults()  — restore factory defaults, stage the daemon-backed
//                   clears the config reset cannot cover, and recompute
//                   the dirty set.
//
// Same class as settingscontroller.cpp, separate TU, no API change.

#include "settingscontroller.h"

#include "settings/utils/dbusutils.h"
#include "settingscontroller_pagekeys.h"

#include <PhosphorProtocol/ClientHelpers.h>
// Directly, not through the chain: the save() path below calls contains() on
// the store, which needs the complete type. LayoutRegistry.h (reached via
// settingscontroller.h) only forward-declares it, and no header in the tree
// includes this one, so without this line the TU compiles only when a unity
// batch happens to pair it with a sibling that does.
#include <PhosphorZones/ScrollingTemplateStore.h>

#include "core/platform/logging.h"

#include <QScopeGuard>
#include <QStringList>
#include <QTimer>

namespace PlasmaZones {

bool SettingsController::adoptOnDiskState(bool treatAsyncRevertAsClean)
{
    // Hold m_loading for the whole body: every step below can fire a NOTIFY that
    // routes through onSettingsPropertyChanged and would re-dirty the very pages
    // this is cleaning. The caller's setNeedsSave(false) is the authoritative
    // reset, and it runs after the flag drops.
    const ScopedFlag loadingScope(m_loading);

    // Animation pages persist per-event motion overrides as separate
    // files (file-per-path under ~/.local/share/plasmazones/profiles/);
    // m_settings.load() alone wouldn't restore them. The page controller's
    // pre-edit snapshot rewinds those files. Shader overrides don't need this —
    // they ride Settings::load()'s Q_PROPERTY re-emit like every other page
    // setting.
    //
    // A refusal has two causes and they mean opposite things, which is what
    // @p treatAsyncRevertAsClean selects between.
    //
    // On the global Discard path this page's async revert is dispatched FIRST, so
    // by the time the settings domain gets here the worker already owns the
    // snapshot map and revertPending() refuses. That is the restore proceeding
    // normally, not a failure: the worker finishes the job and re-raises
    // pendingChangesChanged itself if it has to retain a file. Treating it as
    // "not clean" left every dirty badge lit after a discard that succeeded.
    //
    // A refusal with no worker running, or a partial restore failure, IS a page
    // that is still dirty, and forcing needsSave false there would strand the
    // snapshots for the next Discard to write back over the new state.
    const bool animationsClean = !m_animationsPage
        || (treatAsyncRevertAsClean && m_animationsPage->asyncRevertInFlight()) || m_animationsPage->revertPending();

    m_settings.load();
    // m_settings borrows the shared m_localRuleStore, so Settings::load() above
    // deliberately does NOT reload it (the owner drives reloads — see the
    // borrowed-store note in Settings::load()). As that owner, re-read
    // rules.json here so the store matches its on-disk state.
    // Idempotent: RuleStore::load() only emits when the content differs,
    // mirroring the daemon-rulesChanged path in reloadLocalRuleStore().
    if (m_localRuleStore)
        m_localRuleStore->load();
    // Rules are owned by the daemon (rules.json); re-fetch the daemon's
    // authoritative set, dropping staged edits. Async: the reply handler
    // re-derives dirty through the permanent revertFinished listener in
    // settingscontroller.cpp.
    if (m_rulesPage)
        m_rulesPage->revert();
    m_screenHelper.refreshScreens();
    scheduleLayoutLoad();
    m_staging.clearAll();
    // Emit stagedXxxChanged only when reset() actually transitions a
    // non-empty optional to empty. Unconditional emit violates
    // CLAUDE.md's "only emit signals when value actually changes" rule
    // and re-walks every QML binding keyed on these signals on every
    // call, including the startup load when nothing was staged.
    const bool hadStagedSnap = m_stagedSnappingOrder.has_value();
    const bool hadStagedTile = m_stagedTilingOrder.has_value();
    const bool hadStagedScroll = m_stagedScrollingOrder.has_value();
    m_stagedSnappingOrder.reset();
    m_stagedTilingOrder.reset();
    m_stagedScrollingOrder.reset();
    if (hadStagedSnap)
        Q_EMIT stagedSnappingOrderChanged();
    if (hadStagedTile)
        Q_EMIT stagedTilingOrderChanged();
    if (hadStagedScroll)
        Q_EMIT stagedScrollingOrderChanged();
    return animationsClean;
}

void SettingsController::load()
{
    // A full load adopts the on-disk state wholesale, which is everything a
    // reload deferred by onExternalSettingsChanged() would have done — clear
    // the flag so the clean transition at the end of this load doesn't
    // schedule a redundant second reload.
    m_pendingExternalReload = false;
    const bool animationsClean = adoptOnDiskState(/*treatAsyncRevertAsClean=*/true);
    if (!animationsClean) {
        qCWarning(lcConfig) << "load: animation snapshots are still staged after the revert";
    } else {
        setNeedsSave(false);
    }
}

void SettingsController::save()
{
    m_saving = true;
    // Released on EVERY exit — normal or exceptional — and still deferred by
    // one event-loop turn (see the comment on the deferral below; the guard
    // posts the reset rather than performing it). save() raises a suppression
    // flag and then runs code that can throw (the config write, five D-Bus
    // round trips, allocations); a stranded m_saving = true makes
    // onExternalSettingsChanged drop every later daemon broadcast for the
    // rest of the session and savingFinished never fire, which is the exact
    // hazard defaults() guards its m_loading against with ScopedFlag. A plain
    // ScopedFlag will not do here because the reset must stay deferred.
    const auto savingRelease = qScopeGuard([this]() {
        QTimer::singleShot(0, this, [this]() {
            m_saving = false;
            // Now that m_saving has drained, downstream consumers
            // (SettingsStagingDomain in particular) can release any
            // in-flight guards and emit their applyResult signals
            // without racing this controller's deferred state reset.
            Q_EMIT savingFinished();
        });
    });

    // Flush staged ordering INTO Settings, but do not consume the staging yet.
    // The write has to happen before Settings::save(), because save() is what
    // persists m_settings, so there is no way to defer it past the outcome.
    // Consuming the optionals here as well is what made a failed write
    // invisible: the ordering pages' dirty check is
    // `staged.has_value() && *staged != m_settings.xxxOrder()`, and this write
    // makes the two sides EQUAL, so clearing the optional too left the page
    // reading clean with the reorder unpersisted. The reset, the NOTIFY emits
    // and the rollback all live at the end of save() now, keyed on whether the
    // disk write actually landed.
    const bool hadStagedSnap = m_stagedSnappingOrder.has_value();
    const bool hadStagedTile = m_stagedTilingOrder.has_value();
    const bool hadStagedScroll = m_stagedScrollingOrder.has_value();
    // Captured for the rollback: the values Settings held BEFORE this save
    // overwrote them. Only meaningful for the slots that were actually staged.
    const QStringList prevSnappingOrder = hadStagedSnap ? m_settings.snappingLayoutOrder() : QStringList();
    const QStringList prevTilingOrder = hadStagedTile ? m_settings.tilingAlgorithmOrder() : QStringList();
    const QStringList prevScrollingOrder = hadStagedScroll ? m_settings.scrollingTemplateOrder() : QStringList();
    if (hadStagedSnap) {
        m_settings.setSnappingLayoutOrder(*m_stagedSnappingOrder);
    }
    if (hadStagedTile) {
        m_settings.setTilingAlgorithmOrder(*m_stagedTilingOrder);
    }
    if (hadStagedScroll) {
        m_settings.setScrollingTemplateOrder(*m_stagedScrollingOrder);
    }

    // Persistence phase (pre-save): staged VS configs need to be in Settings
    // before the save flushes to disk. Quick-layout slots (all three modes) are
    // daemon-backed now and flush via D-Bus after notifyReload, below.
    // Verdict gated like the D-Bus flushes below: a staged split whose defs
    // all failed validation is skipped rather than written, and the badge has
    // to stay lit so the user knows the edit did not persist.
    const bool virtualScreensPersisted = m_staging.flushVirtualScreensToSettings(m_settings);

    // Save main settings (includes editor settings + VS configs persisted
    // above). The verdict feeds the same commitOk gate as the D-Bus flushes:
    // Settings::save() deliberately leaves the committed baseline UNMOVED on
    // a failed disk write so the values stay discardable and the next save
    // retries — taking the clean transition anyway made the footer report a
    // save that never landed while the manifest pages' value-based dirty
    // still read staged against the unmoved baseline.
    bool commitOk = virtualScreensPersisted;
    // Kept as its own verdict, separate from the aggregate: the ordering
    // staging below depends on whether the FILE was written, and nothing else.
    // A later D-Bus flush failing does not un-persist an order that reached
    // disk, so rolling the order back on the aggregate would strand memory
    // disagreeing with a file that is already correct.
    const bool configWritten = m_settings.save();
    if (!configWritten) {
        qCWarning(lcConfig) << "save: writing the config file failed — baseline unmoved, values stay staged";
        commitOk = false;
    }

    // save() re-captured the committed baseline, so the animation controller's
    // value-based shader-tree dirty check must re-evaluate: apply() (commitPending)
    // may have run earlier in the domain walk, while the tree still read
    // "divergent" against the not-yet-recaptured baseline. This is a no-op flip
    // guard away from free when nothing changed.
    if (m_animationsPage)
        m_animationsPage->refreshDirtyState();

    // RuleController and AnimationsPageController are registered
    // as their own StagingDomains and the framework's applyAllAsync
    // walks them directly. Both registrations happen in
    // settingscontroller_pageregistration.cpp and route through
    // trackDomain() (which connects dirtyChanged + appends the
    // controller to m_domains): RuleController via regPage(...) →
    // ApplicationController::registerPage(...), and AnimationsPageController
    // via an explicit registerDomain(...) call (it is a headless staging
    // controller, distinct from the "animations" nav-parent node).
    // Their own apply() methods drive the async D-Bus push (rules)
    // and the snapshot clear (animations). Calling commit/commitPending here
    // would double-dispatch (and for rules, ALSO send a
    // synchronous setAllRules over D-Bus *before* the async one
    // returned, hitting the daemon twice in the same save tick). The
    // framework owns those terminal signals; this save() handles only
    // the Settings-
    // backed surface.

    // The three D-Bus flushes below all report TRANSPORT failure and all
    // RETAIN their staging when they fail, so `commitOk` gates the same clean
    // transition for every one of them: the badge re-lights with the data
    // still staged and the next Save re-sends it. Treating them as infallible
    // cleared the staging AND the badge while the daemon had never applied
    // the edits. Transport-level only: a daemon-side rejection inside a void
    // adaptor slot (e.g. setQuickLayoutSlot ignoring an out-of-range slot)
    // still replies successfully and is not caught here.

    // Flush staged VS configs to daemon BEFORE notifyReload so virtual screen
    // IDs exist when assignments referencing them are processed.
    if (!m_staging.flushVirtualScreensToDaemon()) {
        commitOk = false;
    }

    // Notify daemon to reload KConfig settings (before D-Bus assignment mutations)
    DaemonDBus::notifyReload();

    // Flush staged quick-layout slots (snapping + tiling + scrolling) via D-Bus
    // (after reload).
    if (!m_staging.flushQuickSlotsToDaemon()) {
        commitOk = false;
    }

    // Flush staged assignment changes to daemon (same batch protocol as KCM).
    // This must happen AFTER notifyReload so the reload doesn't overwrite
    // the assignment changes.
    if (m_staging.hasPendingAssignments()) {
        QDBusMessage batchOn = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                      QStringLiteral("setSaveBatchMode"), {true});
        const bool batchActive = (batchOn.type() != QDBusMessage::ErrorMessage);
        if (!batchActive) {
            qCWarning(lcCore)
                << "save: setSaveBatchMode(true) failed:" << batchOn.errorMessage()
                << "— skipping flush+apply to avoid per-assignment writes the batch was meant to coalesce";
            commitOk = false;
        } else {
            // The local store is the settings app's own view of the template
            // set, kept in step with the Layouts page, so it answers this
            // without a round trip. See the parameter's doc for why a
            // pre-check is needed at all: the daemon's setter refuses an
            // unknown template with a successful-looking reply.
            const auto templateStillExists = [this](const QString& templateId) {
                if (!m_localTemplateStore) {
                    return true;
                }
                return m_localTemplateStore->contains(QUuid::fromString(templateId));
            };
            // StagingService is not a QObject, so a refusal cannot raise its
            // own signal — it hands the ids back and this owns telling the
            // user. Without a message the only trace would be the re-lit badge
            // below, and the pick is gone from the staging map by design (the
            // template was deleted, so retrying it could never succeed).
            QStringList refusedTemplates;
            if (!m_staging.flushAssignmentsToDaemon(templateStillExists, &refusedTemplates)) {
                commitOk = false;
            }
            if (!refusedTemplates.isEmpty()) {
                // The id is a UUID of a template that no longer exists, so
                // there is no name left to resolve and printing the raw id
                // would tell the user nothing. Say what happened and what to
                // do instead. Split at one because with no translator loaded
                // tr() returns the one source string verbatim, so its
                // grammatical number is fixed whatever n is; a single source
                // would read "1 scrolling templates ... have" at n == 1.
                Q_EMIT layoutOperationFailed(
                    refusedTemplates.size() == 1
                        ? PhosphorI18n::tr("A scrolling template you picked has been deleted, so that monitor kept "
                                           "its previous template. Pick one again on the Monitors page.")
                        : PhosphorI18n::tr("%n scrolling templates you picked have been deleted, so those monitors "
                                           "kept their previous templates. Pick them again on the Monitors page.",
                                           nullptr, static_cast<int>(refusedTemplates.size())));
            }
            QDBusMessage apply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                        QStringLiteral("applyAssignmentChanges"));
            if (apply.type() == QDBusMessage::ErrorMessage) {
                qCWarning(lcCore) << "save: applyAssignmentChanges failed:" << apply.errorMessage();
                Q_EMIT layoutOperationFailed(
                    PhosphorI18n::tr("Failed to apply assignment changes: %1").arg(apply.errorMessage()));
                commitOk = false;
            }
            // Only drop batch mode if we actually entered it. ALWAYS attempt
            // to drop — leaving the daemon in batch mode after a failure
            // would break the next save attempt.
            QDBusMessage batchOff =
                DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                       QStringLiteral("setSaveBatchMode"), {false});
            if (batchOff.type() == QDBusMessage::ErrorMessage) {
                qCWarning(lcCore) << "save: setSaveBatchMode(false) failed:" << batchOff.errorMessage();
                commitOk = false;
            }
        }
    }

    // Settle the ordering staging now that the disk verdict is known.
    //
    // Written: consume the optionals and announce the transition, which is the
    // clean state the pages read as "nothing staged, Settings holds it".
    //
    // Not written: roll Settings back to what it held before and KEEP the
    // optionals. That combination is what makes the pages honest — the dirty
    // check compares the staged list against the Settings list, so the two
    // have to differ again for the reorder to still register as unsaved. No
    // NOTIFY fires on this path because nothing transitioned; the staged value
    // the QML reads through effectiveSnappingOrder() is the same one it read
    // before the save, so the user's order stays on screen.
    //
    // The rollback's own NOTIFY is swallowed by onSettingsPropertyChanged's
    // m_saving guard, which is still raised here, so it cannot re-dirty a page
    // behind the recompute below.
    if (configWritten) {
        if (hadStagedSnap) {
            m_stagedSnappingOrder.reset();
        }
        if (hadStagedTile) {
            m_stagedTilingOrder.reset();
        }
        if (hadStagedScroll) {
            m_stagedScrollingOrder.reset();
        }
        if (hadStagedSnap)
            Q_EMIT stagedSnappingOrderChanged();
        if (hadStagedTile)
            Q_EMIT stagedTilingOrderChanged();
        if (hadStagedScroll)
            Q_EMIT stagedScrollingOrderChanged();
    } else {
        if (hadStagedSnap) {
            m_settings.setSnappingLayoutOrder(prevSnappingOrder);
        }
        if (hadStagedTile) {
            m_settings.setTilingAlgorithmOrder(prevTilingOrder);
        }
        if (hadStagedScroll) {
            m_settings.setScrollingTemplateOrder(prevScrollingOrder);
        }
    }

    // Defer `m_saving = false` to the next event-loop tick. Although
    // notifyReload() is synchronous at the D-Bus level, the daemon's
    // reply-time emission of its own settingsChanged broadcast is a
    // separate D-Bus message that lands in this process's connection
    // queue and is dispatched only when control returns to the event
    // loop. Clearing m_saving immediately exposes a narrow race where
    // onExternalSettingsChanged() fires with m_saving=false and triggers
    // a spurious load() that reverts just-saved assignments. Posting the
    // reset through singleShot(0) drains those queued signals first, so
    // onExternalSettingsChanged() sees m_saving=true and returns early.
    //
    // A reload deferred while edits were pending is superseded by this save:
    // the whole-schema flush above rewrote the file from this process's
    // state, so re-reading disk afterwards would recover nothing of the
    // external change. Clear the flag so the clean transition below doesn't
    // fire a pointless reload.
    m_pendingExternalReload = false;
    setNeedsSave(false);
    // Window-rule failure handling moved to RuleController itself
    // (see pushToDaemonAsync): a failed/partial-drop push keeps the page
    // m_dirty=true and emits applyResult(false), and the framework's
    // applyAllComplete carries the error. SettingsController::save()
    // no longer dispatches window-rule pushes (see comment above) so
    // there is no commit-result to re-flag here.
    if (!commitOk) {
        // Surface the failed daemon commit to the user — same shape
        // as the rules retry path. Without this, a failed batch
        // looks "saved" in the UI while the daemon never applied the
        // edits, so the next launch silently shows stale assignments.
        // MUST wrap in an external-edit envelope targeting "overview" —
        // assignments are edited from MonitorStatePage (Overview), so
        // re-flagging the active page would dirty whatever page the user
        // happens to be viewing at save time, not the page that actually
        // has the unsaved data. Same shape as the rules block above.
        //
        // "overview" is the target for all three flushes, not just
        // assignments: it is the one page that is always registered, and the
        // retained staging is what the badge is really reporting. The
        // virtual-screens and quick-shortcuts pages ALSO light on their own,
        // because their dirty state is value-based over the staging maps that
        // the failed flush just kept.
        ExternalEditScope scope(*this, QStringLiteral("overview"));
        setNeedsSave(true);
    }
    // The deferred m_saving release and savingFinished emit ride the scope
    // guard declared at the top of this function, so an exception anywhere
    // above releases them the same way this return does.
}

void SettingsController::defaults()
{
    // A factory reset rewrites the whole schema to disk just like save()
    // does, superseding any reload deferred while edits were pending —
    // clear the flag for the same reason save() does.
    m_pendingExternalReload = false;
    // Hold m_loading = true through the ENTIRE reset cleanup, not just
    // the reset() call. revertPending() emits pendingChangesChanged
    // synchronously and the staged-order reset transitions through
    // optional<>::reset() emit NOTIFY signals — all of which route via
    // onSettingsPropertyChanged and would otherwise re-mark the active
    // page dirty before the final dirty-set computation below. Keeping the
    // gate engaged for the full body matches the load()/save() pattern and
    // gives us one clean dirtyPagesChanged emit at the end.
    //
    // ScopedFlag, not a bare pair of assignments: anything below can throw
    // (a D-Bus call, a QString allocation), and a stranded m_loading = true
    // swallows every subsequent edit for the rest of the session.
    ScopedFlag loadingScope(m_loading);

    // Nothing below may proceed on a reset that did not land. A failed commit
    // leaves the previous configuration on disk and in the store, so notifying
    // the daemon, resetting its managed rules and clearing the staging would
    // half-apply a factory reset the config itself never took.
    if (!m_settings.reset()) {
        qCWarning(lcConfig) << "defaults: the cleared configuration could not be written — nothing was reset";
        Q_EMIT pageResetFailed(QString(), QString(ReasonResetNotWritten));
        return;
    }

    m_staging.clearAll();
    // Gate the staged-order NOTIFY emits on transition (same rationale
    // as load() / save()) — CLAUDE.md emit-on-change rule.
    const bool hadStagedSnap = m_stagedSnappingOrder.has_value();
    const bool hadStagedTile = m_stagedTilingOrder.has_value();
    const bool hadStagedScroll = m_stagedScrollingOrder.has_value();
    m_stagedSnappingOrder.reset();
    m_stagedTilingOrder.reset();
    m_stagedScrollingOrder.reset();
    if (hadStagedSnap)
        Q_EMIT stagedSnappingOrderChanged();
    if (hadStagedTile)
        Q_EMIT stagedTilingOrderChanged();
    if (hadStagedScroll)
        Q_EMIT stagedScrollingOrderChanged();

    // Drop the animations page's in-memory staged edits so the page
    // matches the reset settings (on-disk animation overrides in
    // per-event JSON files are a separate concern — reset() doesn't
    // touch them, and the user would need a dedicated "reset all
    // animation customizations" entry point to clear those).
    if (m_animationsPage && !m_animationsPage->revertPending()) {
        // Refused because an async discard is still in flight. The reset leaves
        // the per-event override files as they are, so say so rather than
        // reporting defaults that are only half applied.
        //
        // Deliberately NOT the asyncRevertInFlight() shortcut load() takes: on
        // the Discard path the worker owns the restore and finishing it IS the
        // goal, but defaults() wants the files reset NOW, so treating a busy
        // worker as clean would silently skip that work. Same posture as
        // importAllSettings().
        qCWarning(lcConfig) << "defaults: animation snapshots are still staged after the revert (a discard is in "
                               "flight, or a restore failed)";
        // A log line is not a result. The two sibling paths (per-page Reset and
        // per-page Discard) both raise a signal for exactly this refusal, and
        // without one here the animation pages silently keep their overrides
        // while the rest of the app reports a completed factory reset.
        Q_EMIT pageResetFailed(QStringLiteral("animations"), QString(ReasonOverridesNotCleared));
    }

    // Quick-layout slots are daemon-backed (mode-keyed LayoutRegistry), so
    // m_settings.reset() above did not touch them and clearAll() only dropped
    // whatever was staged. Stage the clears the same way per-page Reset does —
    // through the same helper — so "Restore Defaults" actually unassigns the
    // slots instead of leaving the user's assignments behind on the three
    // Quick Shortcuts pages. The clears flush on the next Save, which is what
    // leaves those pages legitimately dirty below.
    //
    // The loop runs over WIRE MODES, matching AssignmentEntry::Mode — the same
    // enumeration stageQuickSlotClears and flushQuickSlotsToDaemon key on.
    bool quickSlotsStaged = false;
    for (const int wireMode : {QuickSlotModeSnapping, QuickSlotModeTiling, QuickSlotModeScrolling}) {
        bool staged = false;
        if (!stageQuickSlotClears(wireMode, staged)) {
            const QString page = wireMode == QuickSlotModeSnapping ? QStringLiteral("snapping-shortcuts")
                : wireMode == QuickSlotModeScrolling               ? QStringLiteral("scrolling-shortcuts")
                                                                   : QStringLiteral("tiling-shortcuts");
            Q_EMIT pageResetFailed(page, QString(ReasonDaemonUnreachable));
            continue;
        }
        quickSlotsStaged = quickSlotsStaged || staged;
    }
    if (quickSlotsStaged)
        Q_EMIT quickLayoutSlotsChanged();

    // Refresh screen list — symmetric with load(), which calls this
    // immediately after m_settings.load(). reset() can change screen
    // assignments (per-screen overrides cleared) so QML monitor pages
    // need a fresh snapshot too.
    m_screenHelper.refreshScreens();

    // Notify daemon to reload — reset() wrote defaults to disk
    DaemonDBus::notifyReload();

    // Reset the daemon's managed baseline rules to factory and drop any staged
    // rule edits. resetManagedDefaults() asks the daemon to overwrite just the
    // managed baselines (preserving user rules) and broadcast rulesChanged; the
    // paired revert() reloads the model from the reset set (ordered after the
    // reset on the same D-Bus connection). The window border / title bar / gap
    // values are plain config now (reset by m_settings.reset() above), so this is
    // purely about the daemon-side managed rules, not the Windows appearance page.
    //
    // m_localRuleStore is deliberately NOT reloaded here. resetManagedDefaults is
    // fire-and-forget, so rules.json still holds the pre-reset content at this
    // point and a load() now would re-read exactly what is already in memory. The
    // daemon's rulesChanged broadcast is what carries the rewrite back, through
    // reloadLocalRuleStore (wired in settingscontroller_dbuswire.cpp), and the
    // RuleStoreWatcher covers the file for a session with no daemon at all — a
    // session where this reset never reaches the daemon either.
    if (m_rulesPage) {
        m_rulesPage->resetManagedDefaults();
        m_rulesPage->revert();
    }

    // Drop a staged profile activation the same way the rules staging is
    // dropped. The pointer half of an activation lives in
    // ProfilePageController (staged vs committed active id), OUTSIDE the
    // config store this reset just rewrote — its config half died with the
    // baseline re-capture, but the pointer would survive as a dirty staging
    // domain and the next Save would commit "profile X is active" over a
    // factory configuration that no longer matches it. discard() reverts the
    // staged pointer to the committed one; whether a factory reset should
    // also clear the COMMITTED pointer is a different question this
    // deliberately does not answer.
    if (m_profilesPage) {
        m_profilesPage->discard();
    }

    // A factory reset is APPLIED, not staged: m_settings.reset() wrote the
    // cleared configuration to disk and reloaded from it, and the daemon has
    // been notified. So the config pages are clean, and the blanket "mark every
    // page dirty" this used to do was unbacked — every one of those pages
    // reports its dirty state value-based (owned keys against the committed
    // baseline, which reset() re-captured), so they read CLEAN no matter what
    // m_dirtyPages says. All the mark achieved was lighting the footer over a
    // Save that had nothing to write and a Discard that had nothing to revert.
    //
    // What IS genuinely unsaved after this is the staged quick-slot clears
    // above: they are daemon-backed and only reach the daemon on the next Save.
    // Those pages therefore compute dirty on their own, through the same
    // isPageDirty the rest of the app uses — no special-casing needed here, and
    // no exclusion list to keep in step with the page tree either (the old
    // "rules" carve-out existed only because the blanket mark would have badged
    // a live, daemon-persisted page it could never clear).
    //
    // Drop m_loading before recomputing so isPageDirty sees the settled state,
    // and guard the emit on actual change to keep the emit-on-change discipline.
    loadingScope.release();
    // Start the recompute from an EMPTY set, not from the existing membership:
    // isPageDirty falls back to m_dirtyPages for any page with no value-based
    // rule of its own, so an entry left over from before the reset would keep
    // re-electing itself forever.
    const QSet<QString> before = m_dirtyPages;
    m_dirtyPages.clear();
    for (const QString& page : validPageNames()) {
        if (isPageDirty(page))
            m_dirtyPages.insert(page);
    }
    // A factory reset is a clean transition unconditionally: any value-blind
    // (per-screen) edit was just reset with everything else, so drop the
    // reconcile latch even when the recomputed membership happens to match.
    m_valueBlindDirty = false;
    if (m_dirtyPages != before) {
        // Through the wrapper (not a bare emit) so this path honours the
        // DirtyEmitScope coalescing like every other membership change.
        emitDirtyPagesChanged();
    }
}

} // namespace PlasmaZones
