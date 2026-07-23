// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcutmanager.h"

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
#include <QVariantMap>

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
constexpr auto kIdToggleCheatsheet = "toggle_cheatsheet";
constexpr auto kIdSpanWindowLeft = "span_window_left";
constexpr auto kIdSpanWindowRight = "span_window_right";
constexpr auto kIdSpanWindowUp = "span_window_up";
constexpr auto kIdSpanWindowDown = "span_window_down";

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
    {kIdOpenEditor, &ConfigDefaults::openEditorShortcut, &Settings::openEditorShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Open Zone Editor"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->openEditorRequested();
     }},
    {kIdOpenSettings, &ConfigDefaults::openSettingsShortcut, &Settings::openSettingsShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Open Settings"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->openSettingsRequested();
     }},
    {kIdPreviousLayout, &ConfigDefaults::previousLayoutShortcut, &Settings::previousLayoutShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Previous Layout"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->previousLayoutRequested();
     }},
    {kIdNextLayout, &ConfigDefaults::nextLayoutShortcut, &Settings::nextLayoutShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Next Layout"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->nextLayoutRequested();
     }},

    // ─── Move window ───────────────────────────────────────────────────────
    {kIdMoveWindowLeft, &ConfigDefaults::moveWindowLeftShortcut, &Settings::moveWindowLeftShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Move Window Left"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->moveWindowRequested(NavigationDirection::Left);
     }},
    {kIdMoveWindowRight, &ConfigDefaults::moveWindowRightShortcut, &Settings::moveWindowRightShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Move Window Right"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->moveWindowRequested(NavigationDirection::Right);
     }},
    {kIdMoveWindowUp, &ConfigDefaults::moveWindowUpShortcut, &Settings::moveWindowUpShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Move Window Up"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->moveWindowRequested(NavigationDirection::Up);
     }},
    {kIdMoveWindowDown, &ConfigDefaults::moveWindowDownShortcut, &Settings::moveWindowDownShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Move Window Down"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->moveWindowRequested(NavigationDirection::Down);
     }},

    // ─── Span window (grow/shrink the zone span) ───────────────────────────
    {kIdSpanWindowLeft, &ConfigDefaults::spanWindowLeftShortcut, &Settings::spanWindowLeftShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Span Window Left"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->spanWindowRequested(NavigationDirection::Left);
     }},
    {kIdSpanWindowRight, &ConfigDefaults::spanWindowRightShortcut, &Settings::spanWindowRightShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Span Window Right"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->spanWindowRequested(NavigationDirection::Right);
     }},
    {kIdSpanWindowUp, &ConfigDefaults::spanWindowUpShortcut, &Settings::spanWindowUpShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Span Window Up"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->spanWindowRequested(NavigationDirection::Up);
     }},
    {kIdSpanWindowDown, &ConfigDefaults::spanWindowDownShortcut, &Settings::spanWindowDownShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Span Window Down"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->spanWindowRequested(NavigationDirection::Down);
     }},

    // ─── Focus zone ────────────────────────────────────────────────────────
    {kIdFocusZoneLeft, &ConfigDefaults::focusZoneLeftShortcut, &Settings::focusZoneLeftShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Zone Left"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->focusZoneRequested(NavigationDirection::Left);
     }},
    {kIdFocusZoneRight, &ConfigDefaults::focusZoneRightShortcut, &Settings::focusZoneRightShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Zone Right"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->focusZoneRequested(NavigationDirection::Right);
     }},
    {kIdFocusZoneUp, &ConfigDefaults::focusZoneUpShortcut, &Settings::focusZoneUpShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Zone Up"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->focusZoneRequested(NavigationDirection::Up);
     }},
    {kIdFocusZoneDown, &ConfigDefaults::focusZoneDownShortcut, &Settings::focusZoneDownShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Zone Down"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->focusZoneRequested(NavigationDirection::Down);
     }},

    // ─── Non-directional navigation ────────────────────────────────────────
    {kIdPushToEmptyZone, &ConfigDefaults::pushToEmptyZoneShortcut, &Settings::pushToEmptyZoneShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Move Window to Empty Zone"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->pushToEmptyZoneRequested();
     }},
    {kIdRestoreWindowSize, &ConfigDefaults::restoreWindowSizeShortcut, &Settings::restoreWindowSizeShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Restore Window Size"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->restoreWindowSizeRequested();
     }},
    {kIdToggleWindowFloat, &ConfigDefaults::toggleWindowFloatShortcut, &Settings::toggleWindowFloatShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Toggle Window Floating"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->toggleWindowFloatRequested();
     }},

    // ─── Swap window ───────────────────────────────────────────────────────
    {kIdSwapWindowLeft, &ConfigDefaults::swapWindowLeftShortcut, &Settings::swapWindowLeftShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Swap Window Left"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapWindowRequested(NavigationDirection::Left);
     }},
    {kIdSwapWindowRight, &ConfigDefaults::swapWindowRightShortcut, &Settings::swapWindowRightShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Swap Window Right"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapWindowRequested(NavigationDirection::Right);
     }},
    {kIdSwapWindowUp, &ConfigDefaults::swapWindowUpShortcut, &Settings::swapWindowUpShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Swap Window Up"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapWindowRequested(NavigationDirection::Up);
     }},
    {kIdSwapWindowDown, &ConfigDefaults::swapWindowDownShortcut, &Settings::swapWindowDownShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Swap Window Down"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapWindowRequested(NavigationDirection::Down);
     }},

    // ─── Swap virtual screen ───────────────────────────────────────────────
    {kIdSwapVirtualScreenLeft, &ConfigDefaults::swapVirtualScreenLeftShortcut, &Settings::swapVirtualScreenLeftShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Swap Virtual Screen Left"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapVirtualScreenRequested(NavigationDirection::Left);
     }},
    {kIdSwapVirtualScreenRight, &ConfigDefaults::swapVirtualScreenRightShortcut,
     &Settings::swapVirtualScreenRightShortcut, QT_TRANSLATE_NOOP("plasmazones", "Swap Virtual Screen Right"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapVirtualScreenRequested(NavigationDirection::Right);
     }},
    {kIdSwapVirtualScreenUp, &ConfigDefaults::swapVirtualScreenUpShortcut, &Settings::swapVirtualScreenUpShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Swap Virtual Screen Up"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapVirtualScreenRequested(NavigationDirection::Up);
     }},
    {kIdSwapVirtualScreenDown, &ConfigDefaults::swapVirtualScreenDownShortcut, &Settings::swapVirtualScreenDownShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Swap Virtual Screen Down"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapVirtualScreenRequested(NavigationDirection::Down);
     }},

    // ─── Rotate virtual screens ────────────────────────────────────────────
    {kIdRotateVirtualScreensCW, &ConfigDefaults::rotateVirtualScreensClockwiseShortcut,
     &Settings::rotateVirtualScreensClockwiseShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Rotate Virtual Screens Clockwise"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->rotateVirtualScreensRequested(true);
     }},
    {kIdRotateVirtualScreensCCW, &ConfigDefaults::rotateVirtualScreensCounterclockwiseShortcut,
     &Settings::rotateVirtualScreensCounterclockwiseShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Rotate Virtual Screens Counterclockwise"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->rotateVirtualScreensRequested(false);
     }},

    // ─── Rotate windows ────────────────────────────────────────────────────
    {kIdRotateWindowsCW, &ConfigDefaults::rotateWindowsClockwiseShortcut, &Settings::rotateWindowsClockwiseShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Rotate Windows Clockwise"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->rotateWindowsRequested(true);
     }},
    {kIdRotateWindowsCCW, &ConfigDefaults::rotateWindowsCounterclockwiseShortcut,
     &Settings::rotateWindowsCounterclockwiseShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Rotate Windows Counterclockwise"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->rotateWindowsRequested(false);
     }},

    // ─── Cycle window in zone ──────────────────────────────────────────────
    {kIdCycleWindowForward, &ConfigDefaults::cycleWindowForwardShortcut, &Settings::cycleWindowForwardShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Cycle Window Forward in Zone"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->cycleWindowsInZoneRequested(true);
     }},
    {kIdCycleWindowBackward, &ConfigDefaults::cycleWindowBackwardShortcut, &Settings::cycleWindowBackwardShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Cycle Window Backward in Zone"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->cycleWindowsInZoneRequested(false);
     }},

    // ─── Misc layout ops ───────────────────────────────────────────────────
    {kIdResnapToNewLayout, &ConfigDefaults::resnapToNewLayoutShortcut, &Settings::resnapToNewLayoutShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Reapply Layout to Windows"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->resnapToNewLayoutRequested();
     }},
    {kIdSnapAllWindows, &ConfigDefaults::snapAllWindowsShortcut, &Settings::snapAllWindowsShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Snap All Windows to Zones"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->snapAllWindowsRequested();
     }},
    {kIdLayoutPicker, &ConfigDefaults::layoutPickerShortcut, &Settings::layoutPickerShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Open Layout Picker"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->layoutPickerRequested();
     }},
    {kIdToggleLayoutLock, &ConfigDefaults::toggleLayoutLockShortcut, &Settings::toggleLayoutLockShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Toggle Layout Lock"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->toggleLayoutLockRequested();
     }},

    // ─── Autotile ──────────────────────────────────────────────────────────
    {kIdToggleAutotile, &ConfigDefaults::autotileToggleShortcut, &Settings::autotileToggleShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Toggle Autotile"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->toggleAutotileRequested();
     }},
    {kIdFocusMaster, &ConfigDefaults::autotileFocusMasterShortcut, &Settings::autotileFocusMasterShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Master Window"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->focusMasterRequested();
     }},
    {kIdSwapMaster, &ConfigDefaults::autotileSwapMasterShortcut, &Settings::autotileSwapMasterShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Swap with Master"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->swapWithMasterRequested();
     }},
    {kIdIncreaseMasterRatio, &ConfigDefaults::autotileIncMasterRatioShortcut, &Settings::autotileIncMasterRatioShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Increase Master Ratio"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->increaseMasterRatioRequested();
     }},
    {kIdDecreaseMasterRatio, &ConfigDefaults::autotileDecMasterRatioShortcut, &Settings::autotileDecMasterRatioShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Decrease Master Ratio"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->decreaseMasterRatioRequested();
     }},
    {kIdIncreaseMasterCount, &ConfigDefaults::autotileIncMasterCountShortcut, &Settings::autotileIncMasterCountShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Increase Master Count"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->increaseMasterCountRequested();
     }},
    {kIdDecreaseMasterCount, &ConfigDefaults::autotileDecMasterCountShortcut, &Settings::autotileDecMasterCountShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Decrease Master Count"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->decreaseMasterCountRequested();
     }},
    {kIdRetile, &ConfigDefaults::autotileRetileShortcut, &Settings::autotileRetileShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Retile Windows"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->retileRequested();
     }},

    // ─── Cheatsheet ────────────────────────────────────────────────────────
    {kIdToggleCheatsheet, &ConfigDefaults::toggleCheatsheetShortcut, &Settings::toggleCheatsheetShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Open Shortcut Cheatsheet"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->toggleCheatsheetRequested();
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

// ─── Cheatsheet catalog metadata ────────────────────────────────────────────
// Display category + mode applicability per shortcut id, consumed by
// cheatsheetModel(). Kept as a separate id-keyed table (rather than columns
// on kStaticEntries) so registration stays independent of presentation and
// the whole classification reads in one place. Category labels are
// untranslated keys; cheatsheetModel() runs them through PhosphorI18n::tr.
//
// Mode classification contract (maintainer-decided):
//  - directional move/focus/swap are generic navigation → all modes
//  - zone-centric ops (snap-to-zone slots, empty-zone push, restore size,
//    cycle/rotate within zones) are hard no-ops off snapping → snapping
//  - master-stack ops are hard no-ops off autotile → autotile
//  - toggle_autotile is the doorway INTO autotile → all modes, always shown
struct CatalogMeta
{
    const char* category;
    int categoryOrder;
    // "all" | "snapping" | "autotile" — string form matches what the QML
    // filter consumes; no enum round-trip needed.
    const char* mode;
    // Optional cheatsheet display label. The registration description must
    // stand alone (System Settings lists it without context), but on the
    // sheet the group heading already carries the context, so rows that
    // repeat it ("Rotate Virtual Screens Clockwise" under "Virtual
    // Screens") overflow the column for no information. nullptr = use the
    // registration description.
    const char* shortLabel = nullptr;
};

CatalogMeta catalogMetaForId(const QString& id)
{
    static const QHash<QString, CatalogMeta> kMeta = [] {
        QHash<QString, CatalogMeta> m;
        const auto add = [&m](const char* id, const char* category, int order, const char* mode,
                              const char* shortLabel = nullptr) {
            m.insert(QLatin1String(id), {category, order, mode, shortLabel});
        };
        add(kIdOpenEditor, QT_TRANSLATE_NOOP("plasmazones", "General"), 0, "all");
        add(kIdOpenSettings, QT_TRANSLATE_NOOP("plasmazones", "General"), 0, "all");
        add(kIdToggleCheatsheet, QT_TRANSLATE_NOOP("plasmazones", "General"), 0, "all");
        add(kIdToggleWindowFloat, QT_TRANSLATE_NOOP("plasmazones", "General"), 0, "all");
        add(kIdToggleAutotile, QT_TRANSLATE_NOOP("plasmazones", "General"), 0, "all");
        add(kIdPreviousLayout, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all");
        add(kIdNextLayout, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all");
        add(kIdLayoutPicker, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all");
        add(kIdToggleLayoutLock, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all");
        add(kIdResnapToNewLayout, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all");
        add(kIdSnapAllWindows, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all");
        add(kIdPushToEmptyZone, QT_TRANSLATE_NOOP("plasmazones", "Snap to Zone"), 3, "snapping");
        add(kIdRestoreWindowSize, QT_TRANSLATE_NOOP("plasmazones", "Snap to Zone"), 3, "snapping");
        // Directional families compress to one row each in cheatsheetModel(),
        // so Move/Focus/Swap/rotate/cycle all fit one "Windows" group instead
        // of four near-empty ones.
        add(kIdMoveWindowLeft, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdMoveWindowRight, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdMoveWindowUp, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdMoveWindowDown, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdFocusZoneLeft, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdFocusZoneRight, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdFocusZoneUp, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdFocusZoneDown, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdSwapWindowLeft, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdSwapWindowRight, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdSwapWindowUp, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdSwapWindowDown, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        // Span grows/shrinks a multi-zone snap — a hard no-op off snapping.
        add(kIdSpanWindowLeft, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping");
        add(kIdSpanWindowRight, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping");
        add(kIdSpanWindowUp, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping");
        add(kIdSpanWindowDown, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping");
        add(kIdRotateWindowsCW, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping",
            QT_TRANSLATE_NOOP("plasmazones", "Rotate Clockwise"));
        add(kIdRotateWindowsCCW, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping",
            QT_TRANSLATE_NOOP("plasmazones", "Rotate Counterclockwise"));
        add(kIdCycleWindowForward, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping",
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Forward in Zone"));
        add(kIdCycleWindowBackward, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping",
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Backward in Zone"));
        add(kIdSwapVirtualScreenLeft, QT_TRANSLATE_NOOP("plasmazones", "Virtual Screens"), 8, "all",
            QT_TRANSLATE_NOOP("plasmazones", "Swap Screen Left"));
        add(kIdSwapVirtualScreenRight, QT_TRANSLATE_NOOP("plasmazones", "Virtual Screens"), 8, "all",
            QT_TRANSLATE_NOOP("plasmazones", "Swap Screen Right"));
        add(kIdSwapVirtualScreenUp, QT_TRANSLATE_NOOP("plasmazones", "Virtual Screens"), 8, "all",
            QT_TRANSLATE_NOOP("plasmazones", "Swap Screen Up"));
        add(kIdSwapVirtualScreenDown, QT_TRANSLATE_NOOP("plasmazones", "Virtual Screens"), 8, "all",
            QT_TRANSLATE_NOOP("plasmazones", "Swap Screen Down"));
        add(kIdRotateVirtualScreensCW, QT_TRANSLATE_NOOP("plasmazones", "Virtual Screens"), 8, "all",
            QT_TRANSLATE_NOOP("plasmazones", "Rotate Clockwise"));
        add(kIdRotateVirtualScreensCCW, QT_TRANSLATE_NOOP("plasmazones", "Virtual Screens"), 8, "all",
            QT_TRANSLATE_NOOP("plasmazones", "Rotate Counterclockwise"));
        add(kIdFocusMaster, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        add(kIdSwapMaster, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        add(kIdIncreaseMasterRatio, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        add(kIdDecreaseMasterRatio, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        add(kIdIncreaseMasterCount, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        add(kIdDecreaseMasterCount, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        add(kIdRetile, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        return m;
    }();

    const auto it = kMeta.constFind(id);
    if (it != kMeta.constEnd()) {
        return *it;
    }
    // Indexed slot families are prefix-keyed, not enumerated above.
    if (id.startsWith(QLatin1String("quick_layout_"))) {
        return {QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all"};
    }
    if (id.startsWith(QLatin1String("snap_to_zone_"))) {
        return {QT_TRANSLATE_NOOP("plasmazones", "Snap to Zone"), 3, "snapping"};
    }
    // A shortcut added to the table without catalog metadata still shows up
    // (miscategorised beats invisible), and the log points at the fix.
    qCWarning(lcShortcuts) << "cheatsheet: no catalog metadata for shortcut id" << id;
    return {QT_TRANSLATE_NOOP("plasmazones", "Other"), 99, "all"};
}

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

ShortcutManager::ShortcutManager(Settings* settings, PhosphorZones::LayoutRegistry* layoutManager, QObject* parent)
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

    connect(
        m_registry.get(), &PhosphorShortcuts::Registry::ready, this,
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
            // The catalog is first meaningful once the batch has settled
            // (backend read-back can answer now).
            Q_EMIT cheatsheetModelChanged();
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
    // settingsChanged fires for every settings save; only a save that
    // actually moved a shortcut sequence needs a backend flush or a
    // cheatsheet refresh (a visible sheet re-pushes its model on the emit,
    // so over-notifying is user-visible churn, not just wasted work).
    if (!rebindAll()) {
        return;
    }
    m_registry->flush();
    Q_EMIT cheatsheetModelChanged();
}

void ShortcutManager::unregisterShortcuts()
{
    m_registrationInProgress = false;
    m_settingsDirty = false;
    // Adhoc ops queued behind an in-flight initial batch belong to UI
    // contexts (e.g. an overlay's Escape grab) that no longer exist after
    // teardown; replaying them on the next registerShortcuts() would
    // re-bind a stale grab.
    m_pendingAdhocOps.clear();
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
        const bool hadPendingRegister =
            std::any_of(m_pendingAdhocOps.cbegin(), m_pendingAdhocOps.cend(), [&id](const PendingAdhocOp& op) {
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
        if (!hadPendingRegister) {
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

QVariantList ShortcutManager::cheatsheetModel() const
{
    QVector<QVariantMap> rows;
    rows.reserve(m_entries.size());
    for (const auto& e : m_entries) {
        const CatalogMeta meta = catalogMetaForId(e.id);
        QStringList triggers;
        if (m_registry) {
            triggers = m_registry->effectiveTriggers(e.id);
        }
        // Normalize to PortableText where the string parses as a key
        // sequence. KGlobalAccel read-back is PortableText already, but the
        // Portal backend relays the compositor's trigger_description
        // verbatim, which may be native/localized spelling — without
        // normalization the family compression's token compares silently
        // fail and the sheet shows every family-member row uncompressed. A string that
        // doesn't parse stays verbatim (better an odd chip than a lost
        // binding).
        for (QString& t : triggers) {
            const QKeySequence parsed(t);
            if (!parsed.isEmpty()) {
                t = parsed.toString(QKeySequence::PortableText);
            }
        }
        QVariantMap row;
        row.insert(QStringLiteral("id"), e.id);
        row.insert(QStringLiteral("label"), meta.shortLabel ? PhosphorI18n::tr(meta.shortLabel) : e.description);
        row.insert(QStringLiteral("category"), PhosphorI18n::tr(meta.category));
        row.insert(QStringLiteral("categoryOrder"), meta.categoryOrder);
        row.insert(QStringLiteral("triggers"), triggers);
        row.insert(QStringLiteral("assigned"), !triggers.isEmpty());
        row.insert(QStringLiteral("mode"), QString::fromLatin1(meta.mode));
        rows.push_back(row);
    }

    // ─── Family compression ────────────────────────────────────────────────
    // The numbered slot families (9 rows each) and the directional quads
    // (4 rows each) dominate the sheet as walls of near-identical lines.
    // When every member of a family is assigned, all members share the same
    // modifier prefix, and each member's final key token is the expected one
    // (its digit / direction), the family collapses into ONE row whose last
    // chip is a range token ("1…9") or "Arrows". Any deviation — a member
    // unassigned, a rebind off-pattern — falls back to the individual rows,
    // because a compressed row would then lie about what the keys do.
    using FamilySpec = CheatsheetFamily;
    const QStringList arrowTokens{QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Up"),
                                  QStringLiteral("Down")};
    const QString arrowsTail = PhosphorI18n::tr("Arrows");
    QStringList digitTokens;
    QStringList quickLayoutIds;
    QStringList snapToZoneIds;
    for (int i = 0; i < 9; ++i) {
        digitTokens.append(QString::number(i + 1));
        quickLayoutIds.append(quickLayoutId(i));
        snapToZoneIds.append(snapToZoneId(i));
    }
    const QVector<FamilySpec> families = {
        {quickLayoutIds, digitTokens, PhosphorI18n::tr("Apply Layout 1-9"), QStringLiteral("1…9")},
        {snapToZoneIds, digitTokens, PhosphorI18n::tr("Snap to Zone 1-9"), QStringLiteral("1…9")},
        {{QString::fromLatin1(kIdMoveWindowLeft), QString::fromLatin1(kIdMoveWindowRight),
          QString::fromLatin1(kIdMoveWindowUp), QString::fromLatin1(kIdMoveWindowDown)},
         arrowTokens,
         PhosphorI18n::tr("Move Window"),
         arrowsTail},
        {{QString::fromLatin1(kIdFocusZoneLeft), QString::fromLatin1(kIdFocusZoneRight),
          QString::fromLatin1(kIdFocusZoneUp), QString::fromLatin1(kIdFocusZoneDown)},
         arrowTokens,
         PhosphorI18n::tr("Focus Zone"),
         arrowsTail},
        {{QString::fromLatin1(kIdSwapWindowLeft), QString::fromLatin1(kIdSwapWindowRight),
          QString::fromLatin1(kIdSwapWindowUp), QString::fromLatin1(kIdSwapWindowDown)},
         arrowTokens,
         PhosphorI18n::tr("Swap Window"),
         arrowsTail},
        {{QString::fromLatin1(kIdSpanWindowLeft), QString::fromLatin1(kIdSpanWindowRight),
          QString::fromLatin1(kIdSpanWindowUp), QString::fromLatin1(kIdSpanWindowDown)},
         arrowTokens,
         PhosphorI18n::tr("Span Window"),
         arrowsTail},
        {{QString::fromLatin1(kIdSwapVirtualScreenLeft), QString::fromLatin1(kIdSwapVirtualScreenRight),
          QString::fromLatin1(kIdSwapVirtualScreenUp), QString::fromLatin1(kIdSwapVirtualScreenDown)},
         arrowTokens,
         // Group heading ("Virtual Screens") carries the context.
         PhosphorI18n::tr("Swap Screens"),
         arrowsTail},
    };

    QVariantList out = compressCheatsheetFamilies(rows, families);
    // Category blocks in display order; stable sort keeps the table's
    // hand-authored order within each category.
    std::stable_sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QLatin1String("categoryOrder")).toInt()
            < b.toMap().value(QLatin1String("categoryOrder")).toInt();
    });
    return out;
}

QVariantList ShortcutManager::compressCheatsheetFamilies(QVector<QVariantMap> rows,
                                                         const QVector<CheatsheetFamily>& families)
{
    QHash<QString, int> rowIndexById;
    for (int i = 0; i < rows.size(); ++i) {
        rowIndexById.insert(rows[i].value(QLatin1String("id")).toString(), i);
    }
    QSet<int> removedIndices;
    for (const auto& family : families) {
        // The two lists are parallel arrays; a mismatched spec would index
        // expectedLastTokens out of bounds below. All current families are
        // 9/9 or 4/4 — this guards the table against a future bad entry.
        Q_ASSERT(family.ids.size() == family.expectedLastTokens.size());
        if (family.ids.size() != family.expectedLastTokens.size()) {
            qCWarning(lcShortcuts) << "cheatsheet: family spec size mismatch for" << family.combinedLabel
                                   << "— skipping compression";
            continue;
        }
        QString sharedPrefix;
        bool compressible = true;
        for (int m = 0; m < family.ids.size() && compressible; ++m) {
            const int idx = rowIndexById.value(family.ids[m], -1);
            if (idx < 0 || !rows[idx].value(QLatin1String("assigned")).toBool()) {
                compressible = false;
                break;
            }
            const QStringList memberTriggers = rows[idx].value(QLatin1String("triggers")).toStringList();
            // A member carrying an alternate binding must not compress: the
            // compressed row shows a single combined chip, so the extra
            // binding would silently vanish from the sheet.
            if (memberTriggers.size() != 1) {
                compressible = false;
                break;
            }
            const QString seq = memberTriggers.first();
            const int split = seq.lastIndexOf(QLatin1Char('+'));
            if (split <= 0 || seq.mid(split + 1) != family.expectedLastTokens[m]) {
                compressible = false;
                break;
            }
            const QString prefix = seq.left(split);
            if (m == 0) {
                sharedPrefix = prefix;
            } else if (prefix != sharedPrefix) {
                compressible = false;
            }
        }
        if (!compressible) {
            continue;
        }
        const int firstIdx = rowIndexById.value(family.ids.first());
        QVariantMap& row = rows[firstIdx];
        row.insert(QStringLiteral("label"), family.combinedLabel);
        row.insert(QStringLiteral("triggers"), QStringList{sharedPrefix + QLatin1Char('+') + family.tailToken});
        for (int m = 1; m < family.ids.size(); ++m) {
            removedIndices.insert(rowIndexById.value(family.ids[m]));
        }
    }

    QVariantList out;
    out.reserve(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        if (!removedIndices.contains(i)) {
            out.push_back(rows[i]);
        }
    }
    return out;
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
        e.description = PhosphorI18n::tr(src.label);
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

    // Snap-to-zone slots 1–9. Mirror of quick-layout slots — separate signal,
    // separate Settings getter, but identical structure.
    for (int i = 0; i < 9; ++i) {
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
