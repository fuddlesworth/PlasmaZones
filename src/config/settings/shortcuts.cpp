// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/settings/settings_detail.h"
#include "config/configdefaults.h"
#include "core/platform/logging.h"

namespace PlasmaZones {

// ── Shortcuts (PhosphorConfig::Store-backed) ────────────────────────────────
// Every shortcut is a flat string; schema registers them without validators.
// Change-detection goes through the P_STORE_SET_STRING macro.

// Global shortcuts — meta actions, zone navigation, snap-to-zone numbered,
// layout rotation, virtual-screen swap/rotate.
P_STORE_GET(QString, openEditorShortcut, shortcutsGlobalGroup, openEditorKey, QString)
P_STORE_SET_STRING(setOpenEditorShortcut, shortcutsGlobalGroup, openEditorKey, openEditorShortcutChanged)
P_STORE_GET(QString, toggleCheatsheetShortcut, shortcutsGlobalGroup, toggleCheatsheetKey, QString)
P_STORE_SET_STRING(setToggleCheatsheetShortcut, shortcutsGlobalGroup, toggleCheatsheetKey,
                   toggleCheatsheetShortcutChanged)
P_STORE_GET(QString, openSettingsShortcut, shortcutsGlobalGroup, openSettingsKey, QString)
P_STORE_SET_STRING(setOpenSettingsShortcut, shortcutsGlobalGroup, openSettingsKey, openSettingsShortcutChanged)
P_STORE_GET(QString, previousLayoutShortcut, shortcutsGlobalGroup, previousLayoutKey, QString)
P_STORE_SET_STRING(setPreviousLayoutShortcut, shortcutsGlobalGroup, previousLayoutKey, previousLayoutShortcutChanged)
P_STORE_GET(QString, nextLayoutShortcut, shortcutsGlobalGroup, nextLayoutKey, QString)
P_STORE_SET_STRING(setNextLayoutShortcut, shortcutsGlobalGroup, nextLayoutKey, nextLayoutShortcutChanged)

// quickLayoutN and snapToZoneN arrays — dispatch to per-index key.
// Each wrapper reads/writes the same store using ConfigDefaults::quickLayoutKey(n).

// Navigation shortcuts.
P_STORE_GET(QString, moveWindowLeftShortcut, shortcutsGlobalGroup, moveWindowLeftKey, QString)
P_STORE_SET_STRING(setMoveWindowLeftShortcut, shortcutsGlobalGroup, moveWindowLeftKey, moveWindowLeftShortcutChanged)
P_STORE_GET(QString, moveWindowRightShortcut, shortcutsGlobalGroup, moveWindowRightKey, QString)
P_STORE_SET_STRING(setMoveWindowRightShortcut, shortcutsGlobalGroup, moveWindowRightKey, moveWindowRightShortcutChanged)
P_STORE_GET(QString, moveWindowUpShortcut, shortcutsGlobalGroup, moveWindowUpKey, QString)
P_STORE_SET_STRING(setMoveWindowUpShortcut, shortcutsGlobalGroup, moveWindowUpKey, moveWindowUpShortcutChanged)
P_STORE_GET(QString, moveWindowDownShortcut, shortcutsGlobalGroup, moveWindowDownKey, QString)
P_STORE_SET_STRING(setMoveWindowDownShortcut, shortcutsGlobalGroup, moveWindowDownKey, moveWindowDownShortcutChanged)
P_STORE_GET(QString, focusZoneLeftShortcut, shortcutsGlobalGroup, focusZoneLeftKey, QString)
P_STORE_SET_STRING(setFocusZoneLeftShortcut, shortcutsGlobalGroup, focusZoneLeftKey, focusZoneLeftShortcutChanged)
P_STORE_GET(QString, focusZoneRightShortcut, shortcutsGlobalGroup, focusZoneRightKey, QString)
P_STORE_SET_STRING(setFocusZoneRightShortcut, shortcutsGlobalGroup, focusZoneRightKey, focusZoneRightShortcutChanged)
P_STORE_GET(QString, focusZoneUpShortcut, shortcutsGlobalGroup, focusZoneUpKey, QString)
P_STORE_SET_STRING(setFocusZoneUpShortcut, shortcutsGlobalGroup, focusZoneUpKey, focusZoneUpShortcutChanged)
P_STORE_GET(QString, focusZoneDownShortcut, shortcutsGlobalGroup, focusZoneDownKey, QString)
P_STORE_SET_STRING(setFocusZoneDownShortcut, shortcutsGlobalGroup, focusZoneDownKey, focusZoneDownShortcutChanged)
P_STORE_GET(QString, pushToEmptyZoneShortcut, shortcutsGlobalGroup, pushToEmptyZoneKey, QString)
P_STORE_SET_STRING(setPushToEmptyZoneShortcut, shortcutsGlobalGroup, pushToEmptyZoneKey, pushToEmptyZoneShortcutChanged)
P_STORE_GET(QString, restoreWindowSizeShortcut, shortcutsGlobalGroup, restoreWindowSizeKey, QString)
P_STORE_SET_STRING(setRestoreWindowSizeShortcut, shortcutsGlobalGroup, restoreWindowSizeKey,
                   restoreWindowSizeShortcutChanged)
P_STORE_GET(QString, toggleWindowFloatShortcut, shortcutsGlobalGroup, toggleWindowFloatKey, QString)
P_STORE_SET_STRING(setToggleWindowFloatShortcut, shortcutsGlobalGroup, toggleWindowFloatKey,
                   toggleWindowFloatShortcutChanged)
P_STORE_GET(QString, swapWindowLeftShortcut, shortcutsGlobalGroup, swapWindowLeftKey, QString)
P_STORE_SET_STRING(setSwapWindowLeftShortcut, shortcutsGlobalGroup, swapWindowLeftKey, swapWindowLeftShortcutChanged)
P_STORE_GET(QString, swapWindowRightShortcut, shortcutsGlobalGroup, swapWindowRightKey, QString)
P_STORE_SET_STRING(setSwapWindowRightShortcut, shortcutsGlobalGroup, swapWindowRightKey, swapWindowRightShortcutChanged)
P_STORE_GET(QString, swapWindowUpShortcut, shortcutsGlobalGroup, swapWindowUpKey, QString)
P_STORE_SET_STRING(setSwapWindowUpShortcut, shortcutsGlobalGroup, swapWindowUpKey, swapWindowUpShortcutChanged)
P_STORE_GET(QString, swapWindowDownShortcut, shortcutsGlobalGroup, swapWindowDownKey, QString)
P_STORE_SET_STRING(setSwapWindowDownShortcut, shortcutsGlobalGroup, swapWindowDownKey, swapWindowDownShortcutChanged)
P_STORE_GET(QString, spanWindowLeftShortcut, shortcutsGlobalGroup, spanWindowLeftKey, QString)
P_STORE_SET_STRING(setSpanWindowLeftShortcut, shortcutsGlobalGroup, spanWindowLeftKey, spanWindowLeftShortcutChanged)
P_STORE_GET(QString, spanWindowRightShortcut, shortcutsGlobalGroup, spanWindowRightKey, QString)
P_STORE_SET_STRING(setSpanWindowRightShortcut, shortcutsGlobalGroup, spanWindowRightKey, spanWindowRightShortcutChanged)
P_STORE_GET(QString, spanWindowUpShortcut, shortcutsGlobalGroup, spanWindowUpKey, QString)
P_STORE_SET_STRING(setSpanWindowUpShortcut, shortcutsGlobalGroup, spanWindowUpKey, spanWindowUpShortcutChanged)
P_STORE_GET(QString, spanWindowDownShortcut, shortcutsGlobalGroup, spanWindowDownKey, QString)
P_STORE_SET_STRING(setSpanWindowDownShortcut, shortcutsGlobalGroup, spanWindowDownKey, spanWindowDownShortcutChanged)

P_STORE_GET(QString, rotateWindowsClockwiseShortcut, shortcutsGlobalGroup, rotateWindowsClockwiseKey, QString)
P_STORE_SET_STRING(setRotateWindowsClockwiseShortcut, shortcutsGlobalGroup, rotateWindowsClockwiseKey,
                   rotateWindowsClockwiseShortcutChanged)
P_STORE_GET(QString, rotateWindowsCounterclockwiseShortcut, shortcutsGlobalGroup, rotateWindowsCounterclockwiseKey,
            QString)
P_STORE_SET_STRING(setRotateWindowsCounterclockwiseShortcut, shortcutsGlobalGroup, rotateWindowsCounterclockwiseKey,
                   rotateWindowsCounterclockwiseShortcutChanged)
P_STORE_GET(QString, cycleWindowForwardShortcut, shortcutsGlobalGroup, cycleWindowForwardKey, QString)
P_STORE_SET_STRING(setCycleWindowForwardShortcut, shortcutsGlobalGroup, cycleWindowForwardKey,
                   cycleWindowForwardShortcutChanged)
P_STORE_GET(QString, cycleWindowBackwardShortcut, shortcutsGlobalGroup, cycleWindowBackwardKey, QString)
P_STORE_SET_STRING(setCycleWindowBackwardShortcut, shortcutsGlobalGroup, cycleWindowBackwardKey,
                   cycleWindowBackwardShortcutChanged)
P_STORE_GET(QString, resnapToNewLayoutShortcut, shortcutsGlobalGroup, resnapToNewLayoutKey, QString)
P_STORE_SET_STRING(setResnapToNewLayoutShortcut, shortcutsGlobalGroup, resnapToNewLayoutKey,
                   resnapToNewLayoutShortcutChanged)
P_STORE_GET(QString, snapAllWindowsShortcut, shortcutsGlobalGroup, snapAllWindowsKey, QString)
P_STORE_SET_STRING(setSnapAllWindowsShortcut, shortcutsGlobalGroup, snapAllWindowsKey, snapAllWindowsShortcutChanged)
P_STORE_GET(QString, layoutPickerShortcut, shortcutsGlobalGroup, layoutPickerKey, QString)
P_STORE_SET_STRING(setLayoutPickerShortcut, shortcutsGlobalGroup, layoutPickerKey, layoutPickerShortcutChanged)
P_STORE_GET(QString, toggleLayoutLockShortcut, shortcutsGlobalGroup, toggleLayoutLockKey, QString)
P_STORE_SET_STRING(setToggleLayoutLockShortcut, shortcutsGlobalGroup, toggleLayoutLockKey,
                   toggleLayoutLockShortcutChanged)
P_STORE_GET(QString, swapVirtualScreenLeftShortcut, shortcutsGlobalGroup, swapVirtualScreenLeftKey, QString)
P_STORE_SET_STRING(setSwapVirtualScreenLeftShortcut, shortcutsGlobalGroup, swapVirtualScreenLeftKey,
                   swapVirtualScreenLeftShortcutChanged)
P_STORE_GET(QString, swapVirtualScreenRightShortcut, shortcutsGlobalGroup, swapVirtualScreenRightKey, QString)
P_STORE_SET_STRING(setSwapVirtualScreenRightShortcut, shortcutsGlobalGroup, swapVirtualScreenRightKey,
                   swapVirtualScreenRightShortcutChanged)
P_STORE_GET(QString, swapVirtualScreenUpShortcut, shortcutsGlobalGroup, swapVirtualScreenUpKey, QString)
P_STORE_SET_STRING(setSwapVirtualScreenUpShortcut, shortcutsGlobalGroup, swapVirtualScreenUpKey,
                   swapVirtualScreenUpShortcutChanged)
P_STORE_GET(QString, swapVirtualScreenDownShortcut, shortcutsGlobalGroup, swapVirtualScreenDownKey, QString)
P_STORE_SET_STRING(setSwapVirtualScreenDownShortcut, shortcutsGlobalGroup, swapVirtualScreenDownKey,
                   swapVirtualScreenDownShortcutChanged)
P_STORE_GET(QString, rotateVirtualScreensClockwiseShortcut, shortcutsGlobalGroup, rotateVirtualScreensClockwiseKey,
            QString)
P_STORE_SET_STRING(setRotateVirtualScreensClockwiseShortcut, shortcutsGlobalGroup, rotateVirtualScreensClockwiseKey,
                   rotateVirtualScreensClockwiseShortcutChanged)
P_STORE_GET(QString, rotateVirtualScreensCounterclockwiseShortcut, shortcutsGlobalGroup,
            rotateVirtualScreensCounterclockwiseKey, QString)
P_STORE_SET_STRING(setRotateVirtualScreensCounterclockwiseShortcut, shortcutsGlobalGroup,
                   rotateVirtualScreensCounterclockwiseKey, rotateVirtualScreensCounterclockwiseShortcutChanged)

// Tiling shortcuts.
P_STORE_GET(QString, autotileToggleShortcut, shortcutsTilingGroup, toggleKey, QString)
P_STORE_SET_STRING(setAutotileToggleShortcut, shortcutsTilingGroup, toggleKey, autotileToggleShortcutChanged)
P_STORE_GET(QString, autotileFocusMasterShortcut, shortcutsTilingGroup, focusMasterKey, QString)
P_STORE_SET_STRING(setAutotileFocusMasterShortcut, shortcutsTilingGroup, focusMasterKey,
                   autotileFocusMasterShortcutChanged)
P_STORE_GET(QString, autotileSwapMasterShortcut, shortcutsTilingGroup, swapMasterKey, QString)
P_STORE_SET_STRING(setAutotileSwapMasterShortcut, shortcutsTilingGroup, swapMasterKey,
                   autotileSwapMasterShortcutChanged)
P_STORE_GET(QString, autotileIncMasterRatioShortcut, shortcutsTilingGroup, incMasterRatioKey, QString)
P_STORE_SET_STRING(setAutotileIncMasterRatioShortcut, shortcutsTilingGroup, incMasterRatioKey,
                   autotileIncMasterRatioShortcutChanged)
P_STORE_GET(QString, autotileDecMasterRatioShortcut, shortcutsTilingGroup, decMasterRatioKey, QString)
P_STORE_SET_STRING(setAutotileDecMasterRatioShortcut, shortcutsTilingGroup, decMasterRatioKey,
                   autotileDecMasterRatioShortcutChanged)
P_STORE_GET(QString, autotileIncMasterCountShortcut, shortcutsTilingGroup, incMasterCountKey, QString)
P_STORE_SET_STRING(setAutotileIncMasterCountShortcut, shortcutsTilingGroup, incMasterCountKey,
                   autotileIncMasterCountShortcutChanged)
P_STORE_GET(QString, autotileDecMasterCountShortcut, shortcutsTilingGroup, decMasterCountKey, QString)
P_STORE_SET_STRING(setAutotileDecMasterCountShortcut, shortcutsTilingGroup, decMasterCountKey,
                   autotileDecMasterCountShortcutChanged)
P_STORE_GET(QString, autotileRetileShortcut, shortcutsTilingGroup, retileKey, QString)
P_STORE_SET_STRING(setAutotileRetileShortcut, shortcutsTilingGroup, retileKey, autotileRetileShortcutChanged)

// Editor shortcuts.
P_STORE_GET(QString, editorDuplicateShortcut, editorShortcutsGroup, duplicateKey, QString)
P_STORE_SET_STRING(setEditorDuplicateShortcut, editorShortcutsGroup, duplicateKey, editorDuplicateShortcutChanged)
P_STORE_GET(QString, editorSplitHorizontalShortcut, editorShortcutsGroup, splitHorizontalKey, QString)
P_STORE_SET_STRING(setEditorSplitHorizontalShortcut, editorShortcutsGroup, splitHorizontalKey,
                   editorSplitHorizontalShortcutChanged)
P_STORE_GET(QString, editorSplitVerticalShortcut, editorShortcutsGroup, splitVerticalKey, QString)
P_STORE_SET_STRING(setEditorSplitVerticalShortcut, editorShortcutsGroup, splitVerticalKey,
                   editorSplitVerticalShortcutChanged)
P_STORE_GET(QString, editorFillShortcut, editorShortcutsGroup, fillKey, QString)
P_STORE_SET_STRING(setEditorFillShortcut, editorShortcutsGroup, fillKey, editorFillShortcutChanged)

// Editor snapping + fill-on-drop toggles.
P_STORE_GET(bool, editorGridSnappingEnabled, editorSnappingGroup, gridEnabledKey, bool)
P_STORE_SET_BOOL(setEditorGridSnappingEnabled, editorSnappingGroup, gridEnabledKey, editorGridSnappingEnabledChanged)
P_STORE_GET(bool, editorEdgeSnappingEnabled, editorSnappingGroup, edgeEnabledKey, bool)
P_STORE_SET_BOOL(setEditorEdgeSnappingEnabled, editorSnappingGroup, edgeEnabledKey, editorEdgeSnappingEnabledChanged)
P_STORE_GET(qreal, editorSnapIntervalX, editorSnappingGroup, intervalXKey, double)
P_STORE_SET_DOUBLE(setEditorSnapIntervalX, editorSnappingGroup, intervalXKey, editorSnapIntervalXChanged)
P_STORE_GET(qreal, editorSnapIntervalY, editorSnappingGroup, intervalYKey, double)
P_STORE_SET_DOUBLE(setEditorSnapIntervalY, editorSnappingGroup, intervalYKey, editorSnapIntervalYChanged)
P_STORE_GET(int, editorSnapOverrideModifier, editorSnappingGroup, overrideModifierKey, int)
P_STORE_SET_INT(setEditorSnapOverrideModifier, editorSnappingGroup, overrideModifierKey,
                editorSnapOverrideModifierChanged)
P_STORE_GET(bool, fillOnDropEnabled, editorFillOnDropGroup, enabledKey, bool)
P_STORE_SET_BOOL(setFillOnDropEnabled, editorFillOnDropGroup, enabledKey, fillOnDropEnabledChanged)
P_STORE_GET(int, fillOnDropModifier, editorFillOnDropGroup, modifierKey, int)
P_STORE_SET_INT(setFillOnDropModifier, editorFillOnDropGroup, modifierKey, fillOnDropModifierChanged)
} // namespace PlasmaZones
