// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Apply / discard / reset lifecycle for SettingsController:
//   * load()      — pull settings from disk, snapshot for dirty
//                   diff, ensure schema migration, hydrate algorithm
//                   ordering, refresh local layouts.
//   * save()      — flush dirty settings to disk, commit per-page
//                   stagers (animations + window rules), notify the
//                   daemon to reload, push quick-slot + assignment
//                   state through D-Bus.
//   * defaults()  — restore factory defaults and replay the page-
//                   reset signal chain.
//   * launchEditor() — fork the zone editor process.
//   * onSettingsPropertyChanged / onExternalSettingsChanged — the
//     two ISettings change-tracking hooks that flow into setNeedsSave.
//
// Split out of settingscontroller.cpp to keep that file under the
// 800-line cap (see CLAUDE.md). Same class, separate TU, no API
// change.

#include "settingscontroller.h"

#include "../config/configdefaults.h"
#include "../config/configmigration.h"
#include "../core/utils.h"
#include "dbusutils.h"

#include <PhosphorProtocol/ClientHelpers.h>

#include "../core/logging.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

namespace PlasmaZones {

void SettingsController::load()
{
    m_loading = true;
    // Animation pages persist per-event motion overrides as separate
    // files (file-per-path under ~/.local/share/plasmazones/profiles/);
    // m_settings.load() alone wouldn't restore them on Discard. The
    // page controller's pre-edit snapshot rewinds those files. Shader
    // overrides don't need this — they ride Settings::load()'s
    // Q_PROPERTY re-emit like every other page setting.
    if (m_animationsPage)
        m_animationsPage->revertPending();
    // Window rules are owned by the daemon (windowrules.json); Discard
    // re-fetches the daemon's authoritative set, dropping staged edits.
    if (m_windowRulesPage)
        m_windowRulesPage->revert();
    m_settings.load();
    // m_settings borrows the shared m_localRuleStore, so Settings::load() above
    // deliberately does NOT reload it (the owner drives reloads — see the
    // borrowed-store note in Settings::load()). As that owner, re-read
    // windowrules.json here so Discard reverts the store to its on-disk state.
    // Idempotent: WindowRuleStore::load() only emits when the content differs,
    // mirroring the daemon-rulesChanged path in reloadLocalRuleStore().
    if (m_localRuleStore)
        m_localRuleStore->load();
    m_screenHelper.refreshScreens();
    scheduleLayoutLoad();
    // Clear staged state BEFORE m_loading=false so any NOTIFY emits
    // it triggers (clearing a staged assignment fires the mirrored
    // Settings property) route through onSettingsPropertyChanged
    // with m_loading=true and stay clean — the trailing
    // setNeedsSave(false) is the authoritative reset.
    m_staging.clearAll();
    // Emit stagedXxxChanged only when reset() actually transitions a
    // non-empty optional to empty. Unconditional emit violates
    // CLAUDE.md's "only emit signals when value actually changes" rule
    // and re-walks every QML binding keyed on these signals on every
    // load(), including the startup load when nothing was staged.
    const bool hadStagedSnap = m_stagedSnappingOrder.has_value();
    const bool hadStagedTile = m_stagedTilingOrder.has_value();
    m_stagedSnappingOrder.reset();
    m_stagedTilingOrder.reset();
    if (hadStagedSnap)
        Q_EMIT stagedSnappingOrderChanged();
    if (hadStagedTile)
        Q_EMIT stagedTilingOrderChanged();
    m_loading = false;
    setNeedsSave(false);
}

void SettingsController::save()
{
    m_saving = true;

    // Flush staged ordering to settings before persisting; emit the
    // transitioned-to-empty NOTIFY signals after the writes so QML
    // bindings tracking the staged state see the persisted-clean
    // transition (load()/defaults() emit symmetrically — save() was
    // the asymmetric outlier).
    const bool hadStagedSnap = m_stagedSnappingOrder.has_value();
    const bool hadStagedTile = m_stagedTilingOrder.has_value();
    if (hadStagedSnap) {
        m_settings.setSnappingLayoutOrder(*m_stagedSnappingOrder);
        m_stagedSnappingOrder.reset();
    }
    if (hadStagedTile) {
        m_settings.setTilingAlgorithmOrder(*m_stagedTilingOrder);
        m_stagedTilingOrder.reset();
    }
    if (hadStagedSnap)
        Q_EMIT stagedSnappingOrderChanged();
    if (hadStagedTile)
        Q_EMIT stagedTilingOrderChanged();

    // Persistence phase (pre-save): staged VS configs need to be in Settings
    // before the save flushes to disk. Quick-layout slots (both modes) are
    // daemon-backed now and flush via D-Bus after notifyReload, below.
    m_staging.flushVirtualScreensToSettings(m_settings);

    // Save main settings (includes editor settings + VS configs persisted above)
    m_settings.save();

    // WindowRuleController and AnimationsPageController are registered
    // as their own StagingDomains and the framework's applyAllAsync
    // walks them directly. Both registrations happen in
    // settingscontroller_pageregistration.cpp and route through
    // trackDomain() (which connects dirtyChanged + appends the
    // controller to m_domains): WindowRuleController via regPage(...) →
    // ApplicationController::registerPage(...), and AnimationsPageController
    // via an explicit registerDomain(...) call (it is a headless staging
    // controller, distinct from the "animations" nav-parent node).
    // Their own apply() methods drive the async D-Bus push (windowrules)
    // and the snapshot clear (animations). Calling commit/commitPending here
    // would double-dispatch (and for window rules, ALSO send a
    // synchronous setAllRules over D-Bus *before* the async one
    // returned, hitting the daemon twice in the same save tick). The
    // framework owns those terminal signals; this save() handles only
    // the Settings-
    // backed surface.

    // Flush staged VS configs to daemon BEFORE notifyReload so virtual screen
    // IDs exist when assignments referencing them are processed.
    m_staging.flushVirtualScreensToDaemon();

    // Notify daemon to reload KConfig settings (before D-Bus assignment mutations)
    DaemonDBus::notifyReload();

    // Flush staged quick-layout slots (snapping + tiling) via D-Bus (after reload).
    m_staging.flushQuickSlotsToDaemon();

    // Flush staged assignment changes to daemon (same batch protocol as KCM).
    // This must happen AFTER notifyReload so the reload doesn't overwrite
    // the assignment changes.
    bool assignmentsCommitOk = true;
    if (m_staging.hasPendingAssignments()) {
        QDBusMessage batchOn = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                      QStringLiteral("setSaveBatchMode"), {true});
        const bool batchActive = (batchOn.type() != QDBusMessage::ErrorMessage);
        if (!batchActive) {
            qCWarning(lcCore)
                << "save: setSaveBatchMode(true) failed:" << batchOn.errorMessage()
                << "— skipping flush+apply to avoid per-assignment writes the batch was meant to coalesce";
            assignmentsCommitOk = false;
        } else {
            m_staging.flushAssignmentsToDaemon();
            QDBusMessage apply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                        QStringLiteral("applyAssignmentChanges"));
            if (apply.type() == QDBusMessage::ErrorMessage) {
                qCWarning(lcCore) << "save: applyAssignmentChanges failed:" << apply.errorMessage();
                Q_EMIT layoutOperationFailed(
                    PhosphorI18n::tr("Failed to apply assignment changes: %1").arg(apply.errorMessage()));
                assignmentsCommitOk = false;
            }
            // Only drop batch mode if we actually entered it. ALWAYS attempt
            // to drop — leaving the daemon in batch mode after a failure
            // would break the next save attempt.
            QDBusMessage batchOff =
                DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                       QStringLiteral("setSaveBatchMode"), {false});
            if (batchOff.type() == QDBusMessage::ErrorMessage) {
                qCWarning(lcCore) << "save: setSaveBatchMode(false) failed:" << batchOff.errorMessage();
                assignmentsCommitOk = false;
            }
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
    setNeedsSave(false);
    // Window-rule failure handling moved to WindowRuleController itself
    // (see pushToDaemonAsync): a failed/partial-drop push keeps the page
    // m_dirty=true and emits applyResult(false), and the framework's
    // applyAllComplete carries the error. SettingsController::save()
    // no longer dispatches window-rule pushes (see comment above) so
    // there is no commit-result to re-flag here.
    if (!assignmentsCommitOk) {
        // Surface the assignment-flush failure to the user — same shape
        // as the window-rules retry path. Without this, a failed batch
        // looks "saved" in the UI while the daemon never applied the
        // edits, so the next launch silently shows stale assignments.
        // MUST wrap in an external-edit envelope targeting "overview" —
        // assignments are edited from MonitorStatePage (Overview), so
        // re-flagging the active page would dirty whatever page the user
        // happens to be viewing at save time, not the page that actually
        // has the unsaved data. Same shape as the window-rules block above.
        ExternalEditScope scope(*this, QStringLiteral("overview"));
        setNeedsSave(true);
    }
    QTimer::singleShot(0, this, [this]() {
        m_saving = false;
        // Now that m_saving has drained, downstream consumers
        // (SettingsStagingDomain in particular) can release any
        // in-flight guards and emit their applyResult signals
        // without racing this controller's deferred state reset.
        Q_EMIT savingFinished();
    });
}

void SettingsController::defaults()
{
    // Hold m_loading = true through the ENTIRE reset cleanup, not just
    // the reset() call. revertPending() emits pendingChangesChanged
    // synchronously and the staged-order reset transitions through
    // optional<>::reset() emit NOTIFY signals — all of which route via
    // onSettingsPropertyChanged and would otherwise re-mark the active
    // page dirty before the trailing blanket-mark below overwrites
    // m_dirtyPages. Keeping the gate engaged for the full body matches
    // the load()/save() pattern and gives us one clean
    // dirtyPagesChanged emit at the end.
    m_loading = true;
    m_settings.reset();

    m_staging.clearAll();
    // Gate the staged-order NOTIFY emits on transition (same rationale
    // as load() / save()) — CLAUDE.md emit-on-change rule.
    const bool hadStagedSnap = m_stagedSnappingOrder.has_value();
    const bool hadStagedTile = m_stagedTilingOrder.has_value();
    m_stagedSnappingOrder.reset();
    m_stagedTilingOrder.reset();
    if (hadStagedSnap)
        Q_EMIT stagedSnappingOrderChanged();
    if (hadStagedTile)
        Q_EMIT stagedTilingOrderChanged();

    // Drop the animations page's in-memory staged edits so the page
    // matches the reset settings (on-disk animation overrides in
    // per-event JSON files are a separate concern — reset() doesn't
    // touch them, and the user would need a dedicated "reset all
    // animation customizations" entry point to clear those).
    if (m_animationsPage) {
        m_animationsPage->revertPending();
    }

    // Refresh screen list — symmetric with load(), which calls this
    // immediately after m_settings.load(). reset() can change screen
    // assignments (per-screen overrides cleared) so QML monitor pages
    // need a fresh snapshot too.
    m_screenHelper.refreshScreens();

    // Notify daemon to reload — reset() wrote defaults to disk
    DaemonDBus::notifyReload();

    // Defaults is a global action — mark every valid page dirty so the
    // unsaved indicator appears next to each of them. Guard the emit
    // on actual change so a back-to-back `defaults()` (or one called
    // when state already matches the post-defaults set) doesn't fire
    // a spurious `dirtyPagesChanged`, matching the emit-on-change
    // discipline used by `setNeedsSave` everywhere else in this file.
    //
    // "window-rules" is INTENTIONALLY excluded from the blanket-mark:
    // its source of truth is the daemon's `windowrules.json` (not the
    // KConfig store reset() clears), and `WindowRuleController::asyncCommit()`
    // is a no-op when the controller is clean. Marking the page dirty
    // here would surface a stale "unsaved changes" indicator that a
    // subsequent Save would never actually clear (asyncCommit short-circuits,
    // leaving the page dirty in perpetuity until the user touches it).
    // Window-rule defaults are out of scope for this entry point —
    // resetting them requires a separate daemon-side "reset rules" path.
    QSet<QString> fullSet = validPageNames();
    fullSet.remove(QStringLiteral("window-rules"));
    m_loading = false;
    if (m_dirtyPages != fullSet) {
        m_dirtyPages = fullSet;
        Q_EMIT dirtyPagesChanged();
    }
}

void SettingsController::launchEditor()
{
    // Prefer the editor next to our own executable (handles
    // build-tree runs + non-PATH installs); fall back to a PATH
    // lookup if not present. Logs the failure so a missing/broken
    // editor doesn't silently produce a no-op click in the UI.
    const QString colocated = QCoreApplication::applicationDirPath() + QLatin1String("/plasmazones-editor");
    QString program = QFileInfo::exists(colocated) ? colocated : QStringLiteral("plasmazones-editor");
    if (!QProcess::startDetached(program, {})) {
        qCWarning(lcCore) << "launchEditor: failed to start" << program << "— editor binary missing or not executable?";
    }
}

} // namespace PlasmaZones
