// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ShortcutManager — the STATIC registration table, split out of
// shortcutmanager.cpp by concern: that TU had grown past the 1150-line
// ceiling and this table was roughly half of it. Only data lives here, one
// row per settings-driven shortcut plus the accessors the manager walks it
// through. Registration lifecycle, adhoc binding and the entry build stay in
// shortcutmanager.cpp.

#include "shortcutmanager_table.h"

#include "shortcutmanager.h"
#include "shortcutmanager_ids.h"

#include "config/configdefaults.h"
#include "config/settings.h"

#include <iterator>

namespace PlasmaZones {
namespace ShortcutTable {

namespace {

using namespace ShortcutIds;

// ─── Static shortcut table ──────────────────────────────────────────────────
// One row per settings-driven shortcut. The four indexed slot families
// (quick_layout_N, snap_to_zone_N, workspace_move_slot_N,
// workspace_focus_slot_N) are absent: their getters are array-indexed rather
// than per-id, so ShortcutManager::buildEntries registers them in its own
// loops.
//
// Adding a shortcut: declare the Q_SIGNAL in shortcutmanager.h, add a
// ConfigDefaults::xxxShortcut accessor, add the Settings::xxxShortcut getter,
// then add one row here. The signal-emit lambda must be capture-less so it
// can decay to a function pointer for storage in the table.
const StaticEntry kStaticEntries[] = {
    // ─── Dynamic workspaces ────────────────────────────────────────────────
    {kIdWorkspaceFocusUp, &ConfigDefaults::workspaceFocusUpShortcut, &Settings::workspaceFocusUpShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Workspace Above"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->workspaceFocusRequested(-1);
     }},
    {kIdWorkspaceFocusDown, &ConfigDefaults::workspaceFocusDownShortcut, &Settings::workspaceFocusDownShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Workspace Below"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->workspaceFocusRequested(1);
     }},
    {kIdWorkspaceMoveWindowUp, &ConfigDefaults::workspaceMoveWindowUpShortcut, &Settings::workspaceMoveWindowUpShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Move Window to Workspace Above"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->workspaceMoveWindowRequested(-1);
     }},
    {kIdWorkspaceMoveWindowDown, &ConfigDefaults::workspaceMoveWindowDownShortcut,
     &Settings::workspaceMoveWindowDownShortcut, QT_TRANSLATE_NOOP("plasmazones", "Move Window to Workspace Below"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->workspaceMoveWindowRequested(1);
     }},
    {kIdWorkspaceMoveColumnUp, &ConfigDefaults::workspaceMoveColumnUpShortcut, &Settings::workspaceMoveColumnUpShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Move Column to Workspace Above"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->workspaceMoveColumnRequested(-1);
     }},
    {kIdWorkspaceMoveColumnDown, &ConfigDefaults::workspaceMoveColumnDownShortcut,
     &Settings::workspaceMoveColumnDownShortcut, QT_TRANSLATE_NOOP("plasmazones", "Move Column to Workspace Below"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->workspaceMoveColumnRequested(1);
     }},
    {kIdWorkspaceReorderUp, &ConfigDefaults::workspaceReorderUpShortcut, &Settings::workspaceReorderUpShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Move Workspace Up"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->workspaceReorderRequested(-1);
     }},
    {kIdWorkspaceReorderDown, &ConfigDefaults::workspaceReorderDownShortcut, &Settings::workspaceReorderDownShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Move Workspace Down"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->workspaceReorderRequested(1);
     }},
    {kIdWorkspaceMoveToMonitorLeft, &ConfigDefaults::workspaceMoveToMonitorLeftShortcut,
     &Settings::workspaceMoveToMonitorLeftShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Move Workspace to Monitor on the Left"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->workspaceMoveToMonitorRequested(QStringLiteral("left"));
     }},
    {kIdWorkspaceMoveToMonitorRight, &ConfigDefaults::workspaceMoveToMonitorRightShortcut,
     &Settings::workspaceMoveToMonitorRightShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Move Workspace to Monitor on the Right"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->workspaceMoveToMonitorRequested(QStringLiteral("right"));
     }},
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
    {kIdSwitchFocusFloatTiling, &ConfigDefaults::switchFocusFloatTilingShortcut,
     &Settings::switchFocusFloatTilingShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Switch Focus Between Floating and Placed Windows"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->switchFocusFloatTilingRequested();
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
    // Label says "cycle", not "toggle": since scrolling became a first-class
    // mode this walks Snapping → Tiling → Scrolling, skipping disabled
    // modes. The id stays toggle_autotile so existing kglobalshortcutsrc
    // bindings survive.
    {kIdToggleAutotile, &ConfigDefaults::autotileToggleShortcut, &Settings::autotileToggleShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Cycle Placement Mode"),
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
    // Mode-neutral since the scrolling arm landed (the catalog tags it
    // "managed"); it stays in this block because its config key and
    // accessor are still the autotile ones, which existing bindings name.
    {kIdRetile, &ConfigDefaults::autotileRetileShortcut, &Settings::autotileRetileShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Retile Windows"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->retileRequested();
     }},

    // ─── Scrolling columns ─────────────────────────────────────────────────
    {kIdScrollFocusColumnFirst, &ConfigDefaults::scrollingFocusColumnFirstShortcut,
     &Settings::scrollingFocusColumnFirstShortcut, QT_TRANSLATE_NOOP("plasmazones", "Focus First Column"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusColumnEndRequested(false);
     }},
    {kIdScrollFocusColumnLast, &ConfigDefaults::scrollingFocusColumnLastShortcut,
     &Settings::scrollingFocusColumnLastShortcut, QT_TRANSLATE_NOOP("plasmazones", "Focus Last Column"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusColumnEndRequested(true);
     }},
    {kIdScrollMoveColumnToFirst, &ConfigDefaults::scrollingMoveColumnToFirstShortcut,
     &Settings::scrollingMoveColumnToFirstShortcut, QT_TRANSLATE_NOOP("plasmazones", "Move Column to Start"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollMoveColumnToEndRequested(false);
     }},
    {kIdScrollMoveColumnToLast, &ConfigDefaults::scrollingMoveColumnToLastShortcut,
     &Settings::scrollingMoveColumnToLastShortcut, QT_TRANSLATE_NOOP("plasmazones", "Move Column to End"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollMoveColumnToEndRequested(true);
     }},
    {kIdScrollConsumeWindow, &ConfigDefaults::scrollingConsumeWindowShortcut,
     &Settings::scrollingConsumeWindowShortcut, QT_TRANSLATE_NOOP("plasmazones", "Consume Window into Column"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollConsumeWindowRequested();
     }},
    {kIdScrollExpelWindow, &ConfigDefaults::scrollingExpelWindowShortcut, &Settings::scrollingExpelWindowShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Expel Window from Column"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollExpelWindowRequested();
     }},
    {kIdScrollConsumeOrExpelLeft, &ConfigDefaults::scrollingConsumeOrExpelLeftShortcut,
     &Settings::scrollingConsumeOrExpelLeftShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Consume or Expel Toward the Strip Start"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollConsumeOrExpelRequested(-1);
     }},
    {kIdScrollConsumeOrExpelRight, &ConfigDefaults::scrollingConsumeOrExpelRightShortcut,
     &Settings::scrollingConsumeOrExpelRightShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Consume or Expel Toward the Strip End"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollConsumeOrExpelRequested(1);
     }},
    {kIdScrollCenterColumn, &ConfigDefaults::scrollingCenterColumnShortcut, &Settings::scrollingCenterColumnShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Center Column"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollCenterColumnRequested();
     }},
    {kIdScrollToggleColumnTabbed, &ConfigDefaults::scrollingToggleColumnTabbedShortcut,
     &Settings::scrollingToggleColumnTabbedShortcut, QT_TRANSLATE_NOOP("plasmazones", "Toggle Tabbed Column"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollToggleColumnTabbedRequested();
     }},
    {kIdScrollToggleWindowedFullscreen, &ConfigDefaults::scrollingToggleWindowedFullscreenShortcut,
     &Settings::scrollingToggleWindowedFullscreenShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Toggle Windowed Fullscreen"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollToggleWindowedFullscreenRequested();
     }},
    {kIdScrollCycleColumnWidth, &ConfigDefaults::scrollingCycleColumnWidthShortcut,
     &Settings::scrollingCycleColumnWidthShortcut, QT_TRANSLATE_NOOP("plasmazones", "Cycle Column Width Preset"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollCycleColumnWidthRequested(1);
     }},
    {kIdScrollCycleColumnWidthBack, &ConfigDefaults::scrollingCycleColumnWidthBackShortcut,
     &Settings::scrollingCycleColumnWidthBackShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Cycle Column Width Preset Back"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollCycleColumnWidthRequested(-1);
     }},
    {kIdScrollIncreaseColumnWidth, &ConfigDefaults::scrollingIncreaseColumnWidthShortcut,
     &Settings::scrollingIncreaseColumnWidthShortcut, QT_TRANSLATE_NOOP("plasmazones", "Increase Column Width"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollAdjustColumnWidthRequested(sm->scrollColumnWidthStepPercent());
     }},
    {kIdScrollDecreaseColumnWidth, &ConfigDefaults::scrollingDecreaseColumnWidthShortcut,
     &Settings::scrollingDecreaseColumnWidthShortcut, QT_TRANSLATE_NOOP("plasmazones", "Decrease Column Width"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollAdjustColumnWidthRequested(-sm->scrollColumnWidthStepPercent());
     }},
    {kIdScrollMaximizeColumn, &ConfigDefaults::scrollingMaximizeColumnShortcut,
     &Settings::scrollingMaximizeColumnShortcut, QT_TRANSLATE_NOOP("plasmazones", "Maximize Column"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollMaximizeColumnRequested();
     }},
    {kIdScrollMaximizeToEdges, &ConfigDefaults::scrollingMaximizeToEdgesShortcut,
     &Settings::scrollingMaximizeToEdgesShortcut, QT_TRANSLATE_NOOP("plasmazones", "Maximize to Screen Edges"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollMaximizeToEdgesRequested();
     }},
    // "Grow into empty space", not "expand to available width": the old
    // wording was indistinguishable from Maximize Column in the System
    // Settings list. This one names what the op actually does — claim the
    // visible leftover space without touching other columns.
    {kIdScrollExpandColumn, &ConfigDefaults::scrollingExpandColumnShortcut, &Settings::scrollingExpandColumnShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Grow Column into Empty Space"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollExpandColumnRequested();
     }},
    {kIdScrollCycleWindowHeight, &ConfigDefaults::scrollingCycleWindowHeightShortcut,
     &Settings::scrollingCycleWindowHeightShortcut, QT_TRANSLATE_NOOP("plasmazones", "Cycle Window Height Preset"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollCycleWindowHeightRequested(1);
     }},
    {kIdScrollCycleWindowHeightBack, &ConfigDefaults::scrollingCycleWindowHeightBackShortcut,
     &Settings::scrollingCycleWindowHeightBackShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Cycle Window Height Preset Back"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollCycleWindowHeightRequested(-1);
     }},
    {kIdScrollIncreaseWindowHeight, &ConfigDefaults::scrollingIncreaseWindowHeightShortcut,
     &Settings::scrollingIncreaseWindowHeightShortcut, QT_TRANSLATE_NOOP("plasmazones", "Increase Window Height"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollAdjustWindowHeightRequested(sm->scrollWindowHeightStepPercent());
     }},
    {kIdScrollDecreaseWindowHeight, &ConfigDefaults::scrollingDecreaseWindowHeightShortcut,
     &Settings::scrollingDecreaseWindowHeightShortcut, QT_TRANSLATE_NOOP("plasmazones", "Decrease Window Height"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollAdjustWindowHeightRequested(-sm->scrollWindowHeightStepPercent());
     }},
    {kIdScrollMaximizeWindowHeight, &ConfigDefaults::scrollingMaximizeWindowHeightShortcut,
     &Settings::scrollingMaximizeWindowHeightShortcut, QT_TRANSLATE_NOOP("plasmazones", "Maximize Window Height"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollMaximizeWindowHeightRequested();
     }},
    // "Grow window into empty space", the wording its width twin settled on
    // (see kIdScrollExpandColumn): it names what the op does rather than
    // reading as another spelling of Maximize Window Height.
    {kIdScrollExpandWindow, &ConfigDefaults::scrollingExpandWindowShortcut, &Settings::scrollingExpandWindowShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Grow Window into Empty Space"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollExpandWindowRequested();
     }},
    {kIdScrollCenterVisibleColumns, &ConfigDefaults::scrollingCenterVisibleColumnsShortcut,
     &Settings::scrollingCenterVisibleColumnsShortcut, QT_TRANSLATE_NOOP("plasmazones", "Center Visible Columns"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollCenterVisibleColumnsRequested();
     }},
    {kIdScrollFocusWindowTop, &ConfigDefaults::scrollingFocusWindowTopShortcut,
     &Settings::scrollingFocusWindowTopShortcut, QT_TRANSLATE_NOOP("plasmazones", "Focus First Window in Column"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusWindowEndRequested(false);
     }},
    {kIdScrollFocusWindowBottom, &ConfigDefaults::scrollingFocusWindowBottomShortcut,
     &Settings::scrollingFocusWindowBottomShortcut, QT_TRANSLATE_NOOP("plasmazones", "Focus Last Window in Column"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusWindowEndRequested(true);
     }},
    {kIdScrollFocusColumnLeft, &ConfigDefaults::scrollingFocusColumnLeftShortcut,
     &Settings::scrollingFocusColumnLeftShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Previous Column, Stopping at the Edge"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusColumnPlainRequested(-1);
     }},
    {kIdScrollFocusColumnRight, &ConfigDefaults::scrollingFocusColumnRightShortcut,
     &Settings::scrollingFocusColumnRightShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Next Column, Stopping at the Edge"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusColumnPlainRequested(1);
     }},
    {kIdScrollFocusColumnLeftOrLast, &ConfigDefaults::scrollingFocusColumnLeftOrLastShortcut,
     &Settings::scrollingFocusColumnLeftOrLastShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Previous Column, Wrapping"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusColumnWrapRequested(-1);
     }},
    {kIdScrollFocusColumnRightOrFirst, &ConfigDefaults::scrollingFocusColumnRightOrFirstShortcut,
     &Settings::scrollingFocusColumnRightOrFirstShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Next Column, Wrapping"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusColumnWrapRequested(1);
     }},
    {kIdScrollMoveToFloating, &ConfigDefaults::scrollingMoveToFloatingShortcut,
     &Settings::scrollingMoveToFloatingShortcut, QT_TRANSLATE_NOOP("plasmazones", "Move Window to Floating"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollMoveToFloatRequested(true);
     }},
    {kIdScrollMoveToTiling, &ConfigDefaults::scrollingMoveToTilingShortcut, &Settings::scrollingMoveToTilingShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Move Window to Tiled"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollMoveToFloatRequested(false);
     }},
    // A literal 100: one viewport is one viewport, and a percent setting with
    // a single meaningful value would be a setting for nothing. The STEP pan
    // has no keyboard row — it is the wheel's (Meta+Shift+wheel, registered
    // by the KWin effect and reading the view scroll step through the
    // ScrollingAdaptor's provider), for the same reason Meta+wheel's column
    // focus has no keyboard twin.
    {kIdScrollViewPageBack, &ConfigDefaults::scrollingViewPageBackShortcut, &Settings::scrollingViewPageBackShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Scroll View Back a Page"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollViewRequested(-100);
     }},
    {kIdScrollViewPageForward, &ConfigDefaults::scrollingViewPageForwardShortcut,
     &Settings::scrollingViewPageForwardShortcut, QT_TRANSLATE_NOOP("plasmazones", "Scroll View Forward a Page"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollViewRequested(100);
     }},
    {kIdScrollEqualizeColumnWidths, &ConfigDefaults::scrollingEqualizeColumnWidthsShortcut,
     &Settings::scrollingEqualizeColumnWidthsShortcut, QT_TRANSLATE_NOOP("plasmazones", "Equalize Column Widths"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollEqualizeColumnWidthsRequested();
     }},
    {kIdScrollMinimizeColumnWidth, &ConfigDefaults::scrollingMinimizeColumnWidthShortcut,
     &Settings::scrollingMinimizeColumnWidthShortcut, QT_TRANSLATE_NOOP("plasmazones", "Minimize Column Width"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollMinimizeColumnWidthRequested();
     }},
    {kIdScrollEqualizeWindowHeights, &ConfigDefaults::scrollingEqualizeWindowHeightsShortcut,
     &Settings::scrollingEqualizeWindowHeightsShortcut, QT_TRANSLATE_NOOP("plasmazones", "Equalize Window Heights"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollEqualizeWindowHeightsRequested();
     }},
    {kIdScrollMinimizeWindowHeight, &ConfigDefaults::scrollingMinimizeWindowHeightShortcut,
     &Settings::scrollingMinimizeWindowHeightShortcut, QT_TRANSLATE_NOOP("plasmazones", "Minimize Window Height"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollMinimizeWindowHeightRequested();
     }},

    // ─── Cheatsheet ────────────────────────────────────────────────────────
    {kIdToggleCheatsheet, &ConfigDefaults::toggleCheatsheetShortcut, &Settings::toggleCheatsheetShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Open Shortcut Cheatsheet"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->toggleCheatsheetRequested();
     }},
};

} // namespace

const StaticEntry* staticEntries()
{
    return kStaticEntries;
}

std::size_t staticEntryCount()
{
    return std::size(kStaticEntries);
}

} // namespace ShortcutTable
} // namespace PlasmaZones
