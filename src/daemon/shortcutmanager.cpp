// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcutmanager.h"

#include "../config/configdefaults.h"
#include "../config/settings.h"
#include "../core/logging.h"
#include "pz_i18n.h"

#include <PhosphorShortcuts/Factory.h>
#include <PhosphorShortcuts/IBackend.h>
#include <PhosphorShortcuts/Registry.h>

#include <algorithm>

namespace PlasmaZones {

namespace {

// Stable string ids are documented contract: they appear in
// ~/.config/kglobalshortcutsrc under the "plasmazonesd" component and in
// XDG Portal settings UIs. Changing one is an on-disk rename that users pay
// for, so add new ones at the bottom; never rename existing.
constexpr auto kIdOpenEditor = "open_editor";
constexpr auto kIdOpenSettings = "open_settings";
constexpr auto kIdPreviousLayout = "previous_layout";
constexpr auto kIdNextLayout = "next_layout";
constexpr auto kIdMoveWindowLeft = "move_window_left";
constexpr auto kIdMoveWindowRight = "move_window_right";
constexpr auto kIdMoveWindowUp = "move_window_up";
constexpr auto kIdMoveWindowDown = "move_window_down";
constexpr auto kIdFocusZoneLeft = "focus_zone_left";
constexpr auto kIdFocusZoneRight = "focus_zone_right";
constexpr auto kIdFocusZoneUp = "focus_zone_up";
constexpr auto kIdFocusZoneDown = "focus_zone_down";
constexpr auto kIdPushToEmptyZone = "push_to_empty_zone";
constexpr auto kIdRestoreWindowSize = "restore_window_size";
constexpr auto kIdToggleWindowFloat = "toggle_window_float";
constexpr auto kIdSwapWindowLeft = "swap_window_left";
constexpr auto kIdSwapWindowRight = "swap_window_right";
constexpr auto kIdSwapWindowUp = "swap_window_up";
constexpr auto kIdSwapWindowDown = "swap_window_down";
constexpr auto kIdSwapVirtualScreenLeft = "swap_virtual_screen_left";
constexpr auto kIdSwapVirtualScreenRight = "swap_virtual_screen_right";
constexpr auto kIdSwapVirtualScreenUp = "swap_virtual_screen_up";
constexpr auto kIdSwapVirtualScreenDown = "swap_virtual_screen_down";
constexpr auto kIdRotateVirtualScreensCW = "rotate_virtual_screens_clockwise";
constexpr auto kIdRotateVirtualScreensCCW = "rotate_virtual_screens_counterclockwise";
constexpr auto kIdRotateWindowsCW = "rotate_windows_clockwise";
constexpr auto kIdRotateWindowsCCW = "rotate_windows_counterclockwise";
constexpr auto kIdCycleWindowForward = "cycle_window_forward";
constexpr auto kIdCycleWindowBackward = "cycle_window_backward";
constexpr auto kIdResnapToNewLayout = "resnap_to_new_layout";
constexpr auto kIdSnapAllWindows = "snap_all_windows";
constexpr auto kIdLayoutPicker = "layout_picker";
constexpr auto kIdToggleLayoutLock = "toggle_layout_lock";
constexpr auto kIdToggleAutotile = "toggle_autotile";
constexpr auto kIdFocusMaster = "focus_master";
constexpr auto kIdSwapMaster = "swap_master";
constexpr auto kIdIncreaseMasterRatio = "increase_master_ratio";
constexpr auto kIdDecreaseMasterRatio = "decrease_master_ratio";
constexpr auto kIdIncreaseMasterCount = "increase_master_count";
constexpr auto kIdDecreaseMasterCount = "decrease_master_count";
constexpr auto kIdRetile = "retile";

QString quickLayoutId(int slotZeroBased)
{
    return QStringLiteral("quick_layout_%1").arg(slotZeroBased + 1);
}

QString snapToZoneId(int slotZeroBased)
{
    return QStringLiteral("snap_to_zone_%1").arg(slotZeroBased + 1);
}

// ─── Static shortcut table ──────────────────────────────────────────────────
// One row per settings-driven shortcut. The two indexed slot families
// (quick_layout_N, snap_to_zone_N) are below in helper loops because their
// getters are array-indexed rather than per-id.
//
// Adding a shortcut: declare the Q_SIGNAL in shortcutmanager.h, add a
// ConfigDefaults::xxxShortcut accessor, add the Settings::xxxShortcut getter,
// then add one row here. The signal-emit lambda must be capture-less so it
// can decay to a function pointer for storage in the table.
struct StaticEntry
{
    const char* id;
    QString (*defGetter)();
    QString (Settings::*curGetter)() const;
    const char* label;
    void (*fire)(ShortcutManager*);
};

const StaticEntry kStaticEntries[] = {
    // ─── Core ──────────────────────────────────────────────────────────────
    {kIdOpenEditor, &ConfigDefaults::openEditorShortcut, &Settings::openEditorShortcut, "Open Zone Editor",
     [](ShortcutManager* sm) {
         Q_EMIT sm->openEditorRequested();
     }},
    {kIdOpenSettings, &ConfigDefaults::openSettingsShortcut, &Settings::openSettingsShortcut, "Open Settings",
     [](ShortcutManager* sm) {
         Q_EMIT sm->openSettingsRequested();
     }},
    {kIdPreviousLayout, &ConfigDefaults::previousLayoutShortcut, &Settings::previousLayoutShortcut, "Previous Layout",
     [](ShortcutManager* sm) {
         Q_EMIT sm->previousLayoutRequested();
     }},
    {kIdNextLayout, &ConfigDefaults::nextLayoutShortcut, &Settings::nextLayoutShortcut, "Next Layout",
     [](ShortcutManager* sm) {
         Q_EMIT sm->nextLayoutRequested();
     }},

    // ─── Move window ───────────────────────────────────────────────────────
    {kIdMoveWindowLeft, &ConfigDefaults::moveWindowLeftShortcut, &Settings::moveWindowLeftShortcut, "Move Window Left",
     [](ShortcutManager* sm) {
         Q_EMIT sm->moveWindowRequested(NavigationDirection::Left);
     }},
    {kIdMoveWindowRight, &ConfigDefaults::moveWindowRightShortcut, &Settings::moveWindowRightShortcut,
     "Move Window Right",
     [](ShortcutManager* sm) {
         Q_EMIT sm->moveWindowRequested(NavigationDirection::Right);
     }},
    {kIdMoveWindowUp, &ConfigDefaults::moveWindowUpShortcut, &Settings::moveWindowUpShortcut, "Move Window Up",
     [](ShortcutManager* sm) {
         Q_EMIT sm->moveWindowRequested(NavigationDirection::Up);
     }},
    {kIdMoveWindowDown, &ConfigDefaults::moveWindowDownShortcut, &Settings::moveWindowDownShortcut, "Move Window Down",
     [](ShortcutManager* sm) {
         Q_EMIT sm->moveWindowRequested(NavigationDirection::Down);
     }},

    // ─── Focus zone ────────────────────────────────────────────────────────
    {kIdFocusZoneLeft, &ConfigDefaults::focusZoneLeftShortcut, &Settings::focusZoneLeftShortcut, "Focus Zone Left",
     [](ShortcutManager* sm) {
         Q_EMIT sm->focusZoneRequested(NavigationDirection::Left);
     }},
    {kIdFocusZoneRight, &ConfigDefaults::focusZoneRightShortcut, &Settings::focusZoneRightShortcut, "Focus Zone Right",
     [](ShortcutManager* sm) {
         Q_EMIT sm->focusZoneRequested(NavigationDirection::Right);
     }},
    {kIdFocusZoneUp, &ConfigDefaults::focusZoneUpShortcut, &Settings::focusZoneUpShortcut, "Focus Zone Up",
     [](ShortcutManager* sm) {
         Q_EMIT sm->focusZoneRequested(NavigationDirection::Up);
     }},
    {kIdFocusZoneDown, &ConfigDefaults::focusZoneDownShortcut, &Settings::focusZoneDownShortcut, "Focus Zone Down",
     [](ShortcutManager* sm) {
         Q_EMIT sm->focusZoneRequested(NavigationDirection::Down);
     }},

    // ─── Non-directional navigation ────────────────────────────────────────
    {kIdPushToEmptyZone, &ConfigDefaults::pushToEmptyZoneShortcut, &Settings::pushToEmptyZoneShortcut,
     "Move Window to Empty Zone",
     [](ShortcutManager* sm) {
         Q_EMIT sm->pushToEmptyZoneRequested();
     }},
    {kIdRestoreWindowSize, &ConfigDefaults::restoreWindowSizeShortcut, &Settings::restoreWindowSizeShortcut,
     "Restore Window Size",
     [](ShortcutManager* sm) {
         Q_EMIT sm->restoreWindowSizeRequested();
     }},
    {kIdToggleWindowFloat, &ConfigDefaults::toggleWindowFloatShortcut, &Settings::toggleWindowFloatShortcut,
     "Toggle Window Floating",
     [](ShortcutManager* sm) {
         Q_EMIT sm->toggleWindowFloatRequested();
     }},

    // ─── Swap window ───────────────────────────────────────────────────────
    {kIdSwapWindowLeft, &ConfigDefaults::swapWindowLeftShortcut, &Settings::swapWindowLeftShortcut, "Swap Window Left",
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapWindowRequested(NavigationDirection::Left);
     }},
    {kIdSwapWindowRight, &ConfigDefaults::swapWindowRightShortcut, &Settings::swapWindowRightShortcut,
     "Swap Window Right",
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapWindowRequested(NavigationDirection::Right);
     }},
    {kIdSwapWindowUp, &ConfigDefaults::swapWindowUpShortcut, &Settings::swapWindowUpShortcut, "Swap Window Up",
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapWindowRequested(NavigationDirection::Up);
     }},
    {kIdSwapWindowDown, &ConfigDefaults::swapWindowDownShortcut, &Settings::swapWindowDownShortcut, "Swap Window Down",
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapWindowRequested(NavigationDirection::Down);
     }},

    // ─── Swap virtual screen ───────────────────────────────────────────────
    {kIdSwapVirtualScreenLeft, &ConfigDefaults::swapVirtualScreenLeftShortcut, &Settings::swapVirtualScreenLeftShortcut,
     "Swap Virtual Screen Left",
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapVirtualScreenRequested(NavigationDirection::Left);
     }},
    {kIdSwapVirtualScreenRight, &ConfigDefaults::swapVirtualScreenRightShortcut,
     &Settings::swapVirtualScreenRightShortcut, "Swap Virtual Screen Right",
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapVirtualScreenRequested(NavigationDirection::Right);
     }},
    {kIdSwapVirtualScreenUp, &ConfigDefaults::swapVirtualScreenUpShortcut, &Settings::swapVirtualScreenUpShortcut,
     "Swap Virtual Screen Up",
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapVirtualScreenRequested(NavigationDirection::Up);
     }},
    {kIdSwapVirtualScreenDown, &ConfigDefaults::swapVirtualScreenDownShortcut, &Settings::swapVirtualScreenDownShortcut,
     "Swap Virtual Screen Down",
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapVirtualScreenRequested(NavigationDirection::Down);
     }},

    // ─── Rotate virtual screens ────────────────────────────────────────────
    {kIdRotateVirtualScreensCW, &ConfigDefaults::rotateVirtualScreensClockwiseShortcut,
     &Settings::rotateVirtualScreensClockwiseShortcut, "Rotate Virtual Screens Clockwise",
     [](ShortcutManager* sm) {
         Q_EMIT sm->rotateVirtualScreensRequested(true);
     }},
    {kIdRotateVirtualScreensCCW, &ConfigDefaults::rotateVirtualScreensCounterclockwiseShortcut,
     &Settings::rotateVirtualScreensCounterclockwiseShortcut, "Rotate Virtual Screens Counterclockwise",
     [](ShortcutManager* sm) {
         Q_EMIT sm->rotateVirtualScreensRequested(false);
     }},

    // ─── Rotate windows ────────────────────────────────────────────────────
    {kIdRotateWindowsCW, &ConfigDefaults::rotateWindowsClockwiseShortcut, &Settings::rotateWindowsClockwiseShortcut,
     "Rotate Windows Clockwise",
     [](ShortcutManager* sm) {
         Q_EMIT sm->rotateWindowsRequested(true);
     }},
    {kIdRotateWindowsCCW, &ConfigDefaults::rotateWindowsCounterclockwiseShortcut,
     &Settings::rotateWindowsCounterclockwiseShortcut, "Rotate Windows Counterclockwise",
     [](ShortcutManager* sm) {
         Q_EMIT sm->rotateWindowsRequested(false);
     }},

    // ─── Cycle window in zone ──────────────────────────────────────────────
    {kIdCycleWindowForward, &ConfigDefaults::cycleWindowForwardShortcut, &Settings::cycleWindowForwardShortcut,
     "Cycle Window Forward in Zone",
     [](ShortcutManager* sm) {
         Q_EMIT sm->cycleWindowsInZoneRequested(true);
     }},
    {kIdCycleWindowBackward, &ConfigDefaults::cycleWindowBackwardShortcut, &Settings::cycleWindowBackwardShortcut,
     "Cycle Window Backward in Zone",
     [](ShortcutManager* sm) {
         Q_EMIT sm->cycleWindowsInZoneRequested(false);
     }},

    // ─── Misc layout ops ───────────────────────────────────────────────────
    {kIdResnapToNewLayout, &ConfigDefaults::resnapToNewLayoutShortcut, &Settings::resnapToNewLayoutShortcut,
     "Reapply Layout to Windows",
     [](ShortcutManager* sm) {
         Q_EMIT sm->resnapToNewLayoutRequested();
     }},
    {kIdSnapAllWindows, &ConfigDefaults::snapAllWindowsShortcut, &Settings::snapAllWindowsShortcut,
     "Snap All Windows to Zones",
     [](ShortcutManager* sm) {
         Q_EMIT sm->snapAllWindowsRequested();
     }},
    {kIdLayoutPicker, &ConfigDefaults::layoutPickerShortcut, &Settings::layoutPickerShortcut, "Open Layout Picker",
     [](ShortcutManager* sm) {
         Q_EMIT sm->layoutPickerRequested();
     }},
    {kIdToggleLayoutLock, &ConfigDefaults::toggleLayoutLockShortcut, &Settings::toggleLayoutLockShortcut,
     "Toggle Layout Lock",
     [](ShortcutManager* sm) {
         Q_EMIT sm->toggleLayoutLockRequested();
     }},

    // ─── Autotile ──────────────────────────────────────────────────────────
    {kIdToggleAutotile, &ConfigDefaults::autotileToggleShortcut, &Settings::autotileToggleShortcut, "Toggle Autotile",
     [](ShortcutManager* sm) {
         Q_EMIT sm->toggleAutotileRequested();
     }},
    {kIdFocusMaster, &ConfigDefaults::autotileFocusMasterShortcut, &Settings::autotileFocusMasterShortcut,
     "Focus Master Window",
     [](ShortcutManager* sm) {
         Q_EMIT sm->focusMasterRequested();
     }},
    {kIdSwapMaster, &ConfigDefaults::autotileSwapMasterShortcut, &Settings::autotileSwapMasterShortcut,
     "Swap with Master",
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapWithMasterRequested();
     }},
    {kIdIncreaseMasterRatio, &ConfigDefaults::autotileIncMasterRatioShortcut, &Settings::autotileIncMasterRatioShortcut,
     "Increase Master Ratio",
     [](ShortcutManager* sm) {
         Q_EMIT sm->increaseMasterRatioRequested();
     }},
    {kIdDecreaseMasterRatio, &ConfigDefaults::autotileDecMasterRatioShortcut, &Settings::autotileDecMasterRatioShortcut,
     "Decrease Master Ratio",
     [](ShortcutManager* sm) {
         Q_EMIT sm->decreaseMasterRatioRequested();
     }},
    {kIdIncreaseMasterCount, &ConfigDefaults::autotileIncMasterCountShortcut, &Settings::autotileIncMasterCountShortcut,
     "Increase Master Count",
     [](ShortcutManager* sm) {
         Q_EMIT sm->increaseMasterCountRequested();
     }},
    {kIdDecreaseMasterCount, &ConfigDefaults::autotileDecMasterCountShortcut, &Settings::autotileDecMasterCountShortcut,
     "Decrease Master Count",
     [](ShortcutManager* sm) {
         Q_EMIT sm->decreaseMasterCountRequested();
     }},
    {kIdRetile, &ConfigDefaults::autotileRetileShortcut, &Settings::autotileRetileShortcut, "Retile Windows",
     [](ShortcutManager* sm) {
         Q_EMIT sm->retileRequested();
     }},
};

// Indexed slot defaults — array of static accessors so the slot loop can
// resolve defaults by index without 9 constexpr-if branches.
using DefaultGetter = QString (*)();
constexpr DefaultGetter kQuickLayoutDefaults[9] = {
    &ConfigDefaults::quickLayout1Shortcut, &ConfigDefaults::quickLayout2Shortcut, &ConfigDefaults::quickLayout3Shortcut,
    &ConfigDefaults::quickLayout4Shortcut, &ConfigDefaults::quickLayout5Shortcut, &ConfigDefaults::quickLayout6Shortcut,
    &ConfigDefaults::quickLayout7Shortcut, &ConfigDefaults::quickLayout8Shortcut, &ConfigDefaults::quickLayout9Shortcut,
};
constexpr DefaultGetter kSnapToZoneDefaults[9] = {
    &ConfigDefaults::snapToZone1Shortcut, &ConfigDefaults::snapToZone2Shortcut, &ConfigDefaults::snapToZone3Shortcut,
    &ConfigDefaults::snapToZone4Shortcut, &ConfigDefaults::snapToZone5Shortcut, &ConfigDefaults::snapToZone6Shortcut,
    &ConfigDefaults::snapToZone7Shortcut, &ConfigDefaults::snapToZone8Shortcut, &ConfigDefaults::snapToZone9Shortcut,
};

// QKeySequence(QString) silently returns an empty sequence on malformed
// input. Wrap with a warning log so a typo in the config surfaces from the
// logs instead of silently disabling a shortcut.
QKeySequence parseSequence(const QString& raw, const QString& contextId)
{
    if (raw.isEmpty()) {
        return {};
    }
    QKeySequence seq(raw);
    if (seq.isEmpty()) {
        qCWarning(lcShortcuts) << "Failed to parse shortcut sequence for" << contextId << ":" << raw;
    }
    return seq;
}

} // namespace

ShortcutManager::ShortcutManager(Settings* settings, LayoutManager* layoutManager, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_layoutManager(layoutManager)
{
    Q_ASSERT(settings);
    Q_ASSERT(layoutManager);

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

void ShortcutManager::registerShortcuts()
{
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
        m_backend = Phosphor::Shortcuts::createBackend(Phosphor::Shortcuts::BackendHint::Auto, nullptr);
    }
    if (!m_registry) {
        m_registry = std::make_unique<Phosphor::Shortcuts::Registry>(m_backend.get(), nullptr);
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

    connect(
        m_registry.get(), &Phosphor::Shortcuts::Registry::ready, this,
        [this] {
            m_registrationInProgress = false;
            qCInfo(lcShortcuts) << "Registered" << m_entries.size() << "shortcuts";
            if (m_settingsDirty) {
                m_settingsDirty = false;
                updateShortcuts();
            }
            // Replay any adhoc (un)registrations that arrived while the
            // initial batch was in flight. Must run AFTER the in-progress
            // flag is cleared so each drained op goes through the normal
            // path instead of re-queuing itself.
            drainPendingAdhocOps();
        },
        Qt::SingleShotConnection);

    m_registry->flush();
}

void ShortcutManager::updateShortcuts()
{
    if (m_registrationInProgress) {
        // Defer — the ready() callback above will call us again.
        m_settingsDirty = true;
        return;
    }
    if (m_entries.isEmpty()) {
        return;
    }
    rebindAll();
    m_registry->flush();
}

void ShortcutManager::unregisterShortcuts()
{
    m_registrationInProgress = false;
    m_settingsDirty = false;
    if (m_registry) {
        for (const auto& e : std::as_const(m_entries)) {
            m_registry->unbind(e.id);
        }
    }
    m_entries.clear();

    // Tear down the backend so any Portal session is closed and grabs are
    // released compositor-side. KGlobalAccel and DBusTrigger backends are
    // cheap to recreate; PortalBackend's CreateSession is a single async
    // round-trip that's hidden by its existing m_flushRequested deferral.
    // A subsequent registerShortcuts() will lazily recreate both.
    m_registry.reset();
    m_backend.reset();
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
        m_pendingAdhocOps.erase(std::remove_if(m_pendingAdhocOps.begin(), m_pendingAdhocOps.end(),
                                               [&id](const PendingAdhocOp& op) {
                                                   return op.id == id;
                                               }),
                                m_pendingAdhocOps.end());
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
    m_registry->flush();
}

void ShortcutManager::unregisterAdhocShortcut(const QString& id)
{
    if (!m_registry) {
        // Backend already torn down (e.g. via unregisterShortcuts() during
        // shutdown) — release is implicit since the entire session is gone.
        return;
    }
    // Same race as registerAdhocShortcut: if the initial batch is still in
    // flight, queue the unregister. Supersede any pending register for the
    // same id — register-then-unregister before the batch drains is just a
    // no-op.
    if (m_registrationInProgress) {
        const auto wasRegister =
            std::find_if(m_pendingAdhocOps.cbegin(), m_pendingAdhocOps.cend(), [&id](const PendingAdhocOp& op) {
                return op.id == id && op.kind == PendingAdhocOp::Register;
            });
        m_pendingAdhocOps.erase(std::remove_if(m_pendingAdhocOps.begin(), m_pendingAdhocOps.end(),
                                               [&id](const PendingAdhocOp& op) {
                                                   return op.id == id;
                                               }),
                                m_pendingAdhocOps.end());
        // Only queue an Unregister if the id hasn't already been seen as a
        // pending Register — cancelling a never-sent register is a no-op and
        // queuing Unregister for it would send a spurious release to the
        // backend for an id it never heard of.
        if (wasRegister == m_pendingAdhocOps.cend()) {
            m_pendingAdhocOps.push_back({PendingAdhocOp::Unregister, id, {}, {}, {}});
        }
        return;
    }
    m_registry->unbind(id);
    m_registry->flush();
}

void ShortcutManager::drainPendingAdhocOps()
{
    if (m_pendingAdhocOps.isEmpty()) {
        return;
    }
    // Swap-and-iterate so a callback that itself calls (un)registerAdhocShortcut
    // lands in a fresh queue rather than mutating the one we're draining.
    QVector<PendingAdhocOp> ops;
    m_pendingAdhocOps.swap(ops);
    qCInfo(lcShortcuts) << "Draining" << ops.size() << "deferred adhoc shortcut op(s) after initial registration";
    for (auto& op : ops) {
        if (op.kind == PendingAdhocOp::Register) {
            registerAdhocShortcut(op.id, op.sequence, op.description, std::move(op.callback));
        } else {
            unregisterAdhocShortcut(op.id);
        }
    }
}

void ShortcutManager::rebindAll()
{
    for (const auto& e : std::as_const(m_entries)) {
        m_registry->rebind(e.id, e.currentSeq());
    }
}

void ShortcutManager::buildEntries()
{
    m_entries.clear();
    m_entries.reserve(std::size(kStaticEntries) + 9 + 9);

    Settings* s = m_settings;

    // Static table — one entry per row.
    for (const auto& src : kStaticEntries) {
        Entry e;
        e.id = QString::fromLatin1(src.id);
        e.defaultSeq = parseSequence(src.defGetter(), e.id);
        e.description = PzI18n::tr(src.label);
        const auto curGetter = src.curGetter;
        const QString idCopy = e.id;
        e.currentSeq = [s, curGetter, idCopy] {
            return parseSequence((s->*curGetter)(), idCopy);
        };
        ShortcutManager* sm = this;
        const auto fire = src.fire;
        e.fire = [sm, fire] {
            fire(sm);
        };
        m_entries.push_back(std::move(e));
    }

    // Quick layout slots 1–9. Default getters indexed via kQuickLayoutDefaults;
    // current getter is Settings::quickLayoutShortcut(int) keyed by slot index.
    for (int i = 0; i < 9; ++i) {
        Entry e;
        e.id = quickLayoutId(i);
        e.defaultSeq = parseSequence(kQuickLayoutDefaults[i](), e.id);
        e.description = PzI18n::tr("Apply Layout %1").arg(i + 1);
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

    // Snap-to-zone slots 1–9. Mirror of quick-layout slots — separate signal,
    // separate Settings getter, but identical structure.
    for (int i = 0; i < 9; ++i) {
        Entry e;
        e.id = snapToZoneId(i);
        e.defaultSeq = parseSequence(kSnapToZoneDefaults[i](), e.id);
        e.description = PzI18n::tr("Snap to Zone %1").arg(i + 1);
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
