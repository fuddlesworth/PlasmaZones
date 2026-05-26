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

#include <PhosphorProtocol/ClientHelpers.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

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
    m_screenHelper.refreshScreens();
    scheduleLayoutLoad();
    m_staging.clearAll();
    m_stagedSnappingOrder.reset();
    m_stagedTilingOrder.reset();
    Q_EMIT stagedSnappingOrderChanged();
    Q_EMIT stagedTilingOrderChanged();
    m_loading = false;
    setNeedsSave(false);
}

void SettingsController::save()
{
    m_saving = true;

    // Flush staged ordering to settings before persisting
    if (m_stagedSnappingOrder.has_value()) {
        m_settings.setSnappingLayoutOrder(*m_stagedSnappingOrder);
        m_stagedSnappingOrder.reset();
    }
    if (m_stagedTilingOrder.has_value()) {
        m_settings.setTilingAlgorithmOrder(*m_stagedTilingOrder);
        m_stagedTilingOrder.reset();
    }

    // Persistence phase (pre-save): staged tiling-quick-slot writes + VS
    // configs need to be in Settings before the save flushes to disk.
    m_staging.flushTilingQuickSlotsToSettings(m_settings);
    m_staging.flushVirtualScreensToSettings(m_settings);

    // Save main settings (includes editor settings + VS configs persisted above)
    m_settings.save();

    // Animations write to disk immediately, so commit just clears the
    // session snapshot — there's nothing left to flush. After this the
    // user can no longer Discard back to the pre-session state for any
    // animation edits made so far.
    if (m_animationsPage)
        m_animationsPage->commitPending();

    // Push the staged window-rule set to the daemon (sole writer of
    // windowrules.json). Done before notifyReload so the daemon's rule
    // engine picks up the new set as part of the same save. A failed push
    // (daemon down, or a partial rule drop) must NOT be reported as saved —
    // tracked here and re-flagged after the blanket setNeedsSave(false) below.
    const bool windowRulesCommitOk = !m_windowRulesPage || m_windowRulesPage->commit();

    // Flush staged VS configs to daemon BEFORE notifyReload so virtual screen
    // IDs exist when assignments referencing them are processed.
    m_staging.flushVirtualScreensToDaemon();

    // Notify daemon to reload KConfig settings (before D-Bus assignment mutations)
    DaemonDBus::notifyReload();

    // Flush staged snapping quick-layout slots via D-Bus (after reload).
    m_staging.flushSnappingQuickSlotsToDaemon();

    // Flush staged assignment changes to daemon (same batch protocol as KCM).
    // This must happen AFTER notifyReload so the reload doesn't overwrite
    // the assignment changes.
    if (m_staging.hasPendingAssignments()) {
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("setSaveBatchMode"), {true});
        m_staging.flushAssignmentsToDaemon();
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("applyAssignmentChanges"));
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("setSaveBatchMode"), {false});
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
    // If the window-rule push failed, the page still has unsaved staged edits
    // — re-flag it so the user is not told everything saved. Done after the
    // blanket clear above (and with m_saving still true, so the dirtyChanged
    // connect lambda's guard short-circuits and does not double-mark).
    if (!windowRulesCommitOk) {
        beginExternalEdit(QStringLiteral("window-rules"));
        setNeedsSave(true);
        endExternalEdit();
    }
    QTimer::singleShot(0, this, [this]() {
        m_saving = false;
    });
}

void SettingsController::defaults()
{
    // reset() deletes all config groups, syncs to disk, then calls load()
    // internally — load()'s reflective NOTIFY emission would otherwise
    // route through onSettingsPropertyChanged and incrementally mark the
    // active page dirty before we overwrite m_dirtyPages below. Suppress
    // it so we get one clean dirtyPagesChanged emit instead of two.
    m_loading = true;
    m_settings.reset();
    m_loading = false;

    m_staging.clearAll();
    m_stagedSnappingOrder.reset();
    m_stagedTilingOrder.reset();
    Q_EMIT stagedSnappingOrderChanged();
    Q_EMIT stagedTilingOrderChanged();

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
    // KConfig store reset() clears), and `WindowRuleController::commit()`
    // is a no-op when the controller is clean. Marking the page dirty
    // here would surface a stale "unsaved changes" indicator that a
    // subsequent Save would never actually clear (commit short-circuits,
    // leaving the page dirty in perpetuity until the user touches it).
    // Window-rule defaults are out of scope for this entry point —
    // resetting them requires a separate daemon-side "reset rules" path.
    QSet<QString> fullSet = validPageNames();
    fullSet.remove(QStringLiteral("window-rules"));
    if (m_dirtyPages != fullSet) {
        m_dirtyPages = fullSet;
        Q_EMIT dirtyPagesChanged();
    }
}

void SettingsController::launchEditor()
{
    QProcess::startDetached(QStringLiteral("plasmazones-editor"), {});
}

} // namespace PlasmaZones
