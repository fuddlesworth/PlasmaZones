// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcutmanager.h"
#include "shortcutmanager_ids.h"

#include "config/configdefaults.h"
#include "config/settings.h"
#include "core/platform/logging.h"
#include "phosphor_i18n.h"

#include <PhosphorShortcuts/Factory.h>
#include <PhosphorShortcuts/IBackend.h>
#include <PhosphorShortcuts/Registry.h>

#include <QHash>
#include <QStringList>
#include <QTimer>

#include <algorithm>

namespace PlasmaZones {

namespace {

// Shortcut ids + indexed-slot id helpers live in shortcutmanager_ids.h
// (shared with the cheatsheet catalog TU).
using namespace ShortcutIds;

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
     &Settings::scrollingConsumeOrExpelLeftShortcut, QT_TRANSLATE_NOOP("plasmazones", "Consume or Expel Left"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollConsumeOrExpelRequested(-1);
     }},
    {kIdScrollConsumeOrExpelRight, &ConfigDefaults::scrollingConsumeOrExpelRightShortcut,
     &Settings::scrollingConsumeOrExpelRightShortcut, QT_TRANSLATE_NOOP("plasmazones", "Consume or Expel Right"),
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
    {kIdScrollResetWindowHeights, &ConfigDefaults::scrollingResetWindowHeightsShortcut,
     &Settings::scrollingResetWindowHeightsShortcut, QT_TRANSLATE_NOOP("plasmazones", "Reset Window Heights"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollResetWindowHeightsRequested();
     }},
    {kIdScrollCenterVisibleColumns, &ConfigDefaults::scrollingCenterVisibleColumnsShortcut,
     &Settings::scrollingCenterVisibleColumnsShortcut, QT_TRANSLATE_NOOP("plasmazones", "Center Visible Columns"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollCenterVisibleColumnsRequested();
     }},
    {kIdScrollFocusWindowTop, &ConfigDefaults::scrollingFocusWindowTopShortcut,
     &Settings::scrollingFocusWindowTopShortcut, QT_TRANSLATE_NOOP("plasmazones", "Focus Top Window in Column"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusWindowEndRequested(false);
     }},
    {kIdScrollFocusWindowBottom, &ConfigDefaults::scrollingFocusWindowBottomShortcut,
     &Settings::scrollingFocusWindowBottomShortcut, QT_TRANSLATE_NOOP("plasmazones", "Focus Bottom Window in Column"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusWindowEndRequested(true);
     }},
    {kIdScrollFocusColumnLeft, &ConfigDefaults::scrollingFocusColumnLeftShortcut,
     &Settings::scrollingFocusColumnLeftShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Column Left, Stopping at the Edge"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusColumnPlainRequested(-1);
     }},
    {kIdScrollFocusColumnRight, &ConfigDefaults::scrollingFocusColumnRightShortcut,
     &Settings::scrollingFocusColumnRightShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Column Right, Stopping at the Edge"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusColumnPlainRequested(1);
     }},
    {kIdScrollFocusColumnLeftOrLast, &ConfigDefaults::scrollingFocusColumnLeftOrLastShortcut,
     &Settings::scrollingFocusColumnLeftOrLastShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Column Left, Wrapping"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusColumnWrapRequested(-1);
     }},
    {kIdScrollFocusColumnRightOrFirst, &ConfigDefaults::scrollingFocusColumnRightOrFirstShortcut,
     &Settings::scrollingFocusColumnRightOrFirstShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Focus Column Right, Wrapping"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollFocusColumnWrapRequested(1);
     }},
    {kIdScrollSwitchFocusFloatTiling, &ConfigDefaults::scrollingSwitchFocusFloatTilingShortcut,
     &Settings::scrollingSwitchFocusFloatTilingShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Switch Focus Between Floating and Tiled"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->scrollSwitchFocusFloatTilingRequested();
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

    // ─── Cheatsheet ────────────────────────────────────────────────────────
    {kIdToggleCheatsheet, &ConfigDefaults::toggleCheatsheetShortcut, &Settings::toggleCheatsheetShortcut,
     QT_TRANSLATE_NOOP("plasmazones", "Open Shortcut Cheatsheet"),
     [](ShortcutManager* sm) {
         Q_EMIT sm->toggleCheatsheetRequested();
     }},
};

// Indexed slot defaults — array of static accessors so the slot loop can
// resolve defaults by index without one constexpr-if branch per slot.
using DefaultGetter = QString (*)();
constexpr DefaultGetter kQuickLayoutDefaults[kIndexedSlotCount] = {
    &ConfigDefaults::quickLayout1Shortcut, &ConfigDefaults::quickLayout2Shortcut, &ConfigDefaults::quickLayout3Shortcut,
    &ConfigDefaults::quickLayout4Shortcut, &ConfigDefaults::quickLayout5Shortcut, &ConfigDefaults::quickLayout6Shortcut,
    &ConfigDefaults::quickLayout7Shortcut, &ConfigDefaults::quickLayout8Shortcut, &ConfigDefaults::quickLayout9Shortcut,
};
constexpr DefaultGetter kSnapToZoneDefaults[kIndexedSlotCount] = {
    &ConfigDefaults::snapToZone1Shortcut, &ConfigDefaults::snapToZone2Shortcut, &ConfigDefaults::snapToZone3Shortcut,
    &ConfigDefaults::snapToZone4Shortcut, &ConfigDefaults::snapToZone5Shortcut, &ConfigDefaults::snapToZone6Shortcut,
    &ConfigDefaults::snapToZone7Shortcut, &ConfigDefaults::snapToZone8Shortcut, &ConfigDefaults::snapToZone9Shortcut,
};

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
    bool modelAlreadyEmitted = false;
    if (m_settingsDirty) {
        m_settingsDirty = false;
        modelAlreadyEmitted = updateShortcuts();
    }
    // Replay any adhoc (un)registrations that arrived while the initial batch
    // was in flight. Must run AFTER the in-progress flag is cleared so each
    // drained op takes the immediate path instead of re-queuing itself.
    drainPendingAdhocOps();
    // The catalog is first meaningful once the batch has settled (backend
    // read-back can answer now). One emit per settle: a dirty-settings replay
    // above may already have re-pushed it.
    if (!modelAlreadyEmitted) {
        Q_EMIT cheatsheetModelChanged();
    }
}

bool ShortcutManager::updateShortcuts()
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
    m_registry->flush();
    Q_EMIT cheatsheetModelChanged();
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
    // Boundary validation: an adhoc id colliding with the settings-driven
    // table (or the indexed slot prefixes) would rebind the persistent entry
    // as transient, and the matching unregisterAdhocShortcut() would purge
    // the persistent binding's saved kglobalshortcutsrc record — the exact
    // wipe unregisterShortcuts() exists to avoid (discussion #851).
    // Set built once: staticShortcutIds() materialises a ~70-entry
    // QStringList per call, and the layout-picker batch registers six adhoc
    // shortcuts per show.
    static const QSet<QString> staticIds = [] {
        const QStringList ids = staticShortcutIds();
        return QSet<QString>(ids.cbegin(), ids.cend());
    }();
    if (staticIds.contains(id) || id.startsWith(QLatin1String(kQuickLayoutPrefix))
        || id.startsWith(QLatin1String(kSnapToZonePrefix))) {
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

void ShortcutManager::drainPendingAdhocOps()
{
    if (m_pendingAdhocOps.isEmpty()) {
        return;
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
        if (op.kind == PendingAdhocOp::Register) {
            m_registry->bind(op.id, op.sequence, op.description, std::move(op.callback), /*persistent=*/false);
            m_registry->rebind(op.id, op.sequence);
        } else {
            m_registry->unbind(op.id);
        }
    }
    m_registry->flush();
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
    QStringList ids;
    ids.reserve(static_cast<int>(std::size(kStaticEntries)));
    for (const auto& e : kStaticEntries) {
        ids.append(QString::fromLatin1(e.id));
    }
    return ids;
}

void ShortcutManager::buildEntries()
{
    m_entries.clear();
    m_entries.reserve(std::size(kStaticEntries) + 2 * kIndexedSlotCount);

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
