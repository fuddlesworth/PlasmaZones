// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorZones/AssignmentEntry.h>

#include <QString>

// Macro to define a static config key accessor returning a QStringLiteral.
// Usage: PZ_CONFIG_KEY(snappingEnabledKey, "SnappingEnabled")
// Expands to: static QString snappingEnabledKey() { return QStringLiteral("SnappingEnabled"); }
#define PZ_CONFIG_KEY(name, str)                                                                                       \
    static QString name()                                                                                              \
    {                                                                                                                  \
        return QStringLiteral(str);                                                                                    \
    }

// Alias for group-name accessors — same body as PZ_CONFIG_KEY, single
// definition so a future tweak to PZ_CONFIG_KEY (e.g. attribute
// annotation) automatically applies to groups too. Separate macro
// name preserved for readability at the call sites.
#define PZ_CONFIG_GROUP(name, str) PZ_CONFIG_KEY(name, str)

namespace PlasmaZones {

/**
 * @brief Static config group names, key strings, and JSON field name accessors.
 *
 * v2 schema: groups use dot-separated paths that mirror the settings UI hierarchy.
 * e.g. "Snapping.Behavior.ZoneSpan" → settings JSON: { "Snapping": { "Behavior": { "ZoneSpan": { ... } } } }
 *
 * ConfigDefaults inherits from ConfigKeys so all call sites
 * (e.g. ConfigDefaults::enabledKey()) continue to work.
 */
class ConfigKeys
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Config Group Names — v2 dot-path hierarchy
    // ═══════════════════════════════════════════════════════════════════════════

    // Schema version key (stored at JSON root)
    PZ_CONFIG_KEY(versionKey, "_version")

    // Top-level groups
    PZ_CONFIG_GROUP(generalGroup, "General")
    PZ_CONFIG_GROUP(snappingGroup, "Snapping")
    PZ_CONFIG_GROUP(tilingGroup, "Tiling")
    PZ_CONFIG_GROUP(exclusionsGroup, "Exclusions")
    PZ_CONFIG_GROUP(performanceGroup, "Performance")
    PZ_CONFIG_GROUP(renderingGroup, "Rendering")
    PZ_CONFIG_GROUP(shadersGroup, "Shaders")
    PZ_CONFIG_GROUP(animationsGroup, "Animations")
    PZ_CONFIG_GROUP(shortcutsGlobalGroup, "Shortcuts.Global")
    PZ_CONFIG_GROUP(shortcutsTilingGroup, "Shortcuts.Tiling")
    PZ_CONFIG_GROUP(orderingGroup, "Ordering")
    PZ_CONFIG_GROUP(updatesGroup, "Updates")

    // Snapping sub-groups
    PZ_CONFIG_GROUP(snappingAppearanceGroup, "Snapping.Appearance")
    PZ_CONFIG_GROUP(snappingBehaviorGroup, "Snapping.Behavior")
    PZ_CONFIG_GROUP(snappingBehaviorZoneSpanGroup, "Snapping.Behavior.ZoneSpan")
    PZ_CONFIG_GROUP(snappingBehaviorSnapAssistGroup, "Snapping.Behavior.SnapAssist")
    PZ_CONFIG_GROUP(snappingBehaviorDisplayGroup, "Snapping.Behavior.Display")
    PZ_CONFIG_GROUP(snappingBehaviorWindowHandlingGroup, "Snapping.Behavior.WindowHandling")
    PZ_CONFIG_GROUP(snappingAppearanceColorsGroup, "Snapping.Appearance.Colors")
    PZ_CONFIG_GROUP(snappingAppearanceOpacityGroup, "Snapping.Appearance.Opacity")
    PZ_CONFIG_GROUP(snappingAppearanceBorderGroup, "Snapping.Appearance.Border")
    PZ_CONFIG_GROUP(snappingAppearanceLabelsGroup, "Snapping.Appearance.Labels")
    PZ_CONFIG_GROUP(snappingEffectsGroup, "Snapping.Effects")
    PZ_CONFIG_GROUP(snappingZoneSelectorGroup, "Snapping.ZoneSelector")
    PZ_CONFIG_GROUP(snappingGapsGroup, "Snapping.Gaps")

    // Display (mode-neutral) — per-mode disable lists. Lives outside Snapping.*
    // because the values gate the whole product (snap + autotile), not just
    // snapping. v3 schema; in v2 these were under Snapping.Behavior.Display.
    PZ_CONFIG_GROUP(displayGroup, "Display")

    // Animations sub-groups
    PZ_CONFIG_GROUP(animationsWindowFilteringGroup, "Animations.WindowFiltering")

    // Tiling sub-groups
    PZ_CONFIG_GROUP(tilingAppearanceGroup, "Tiling.Appearance")
    PZ_CONFIG_GROUP(tilingAlgorithmGroup, "Tiling.Algorithm")
    PZ_CONFIG_GROUP(tilingBehaviorGroup, "Tiling.Behavior")
    PZ_CONFIG_GROUP(tilingBehaviorTriggersGroup, "Tiling.Behavior.Triggers")
    PZ_CONFIG_GROUP(tilingAppearanceColorsGroup, "Tiling.Appearance.Colors")
    PZ_CONFIG_GROUP(tilingAppearanceDecorationsGroup, "Tiling.Appearance.Decorations")
    PZ_CONFIG_GROUP(tilingAppearanceBordersGroup, "Tiling.Appearance.Borders")
    PZ_CONFIG_GROUP(tilingGapsGroup, "Tiling.Gaps")

    // Parent groups (for purge enumeration — covers all sub-groups)
    PZ_CONFIG_GROUP(shortcutsGroup, "Shortcuts")
    PZ_CONFIG_GROUP(editorGroup, "Editor")

    // Editor sub-groups
    PZ_CONFIG_GROUP(editorShortcutsGroup, "Editor.Shortcuts")
    PZ_CONFIG_GROUP(editorSnappingGroup, "Editor.Snapping")
    PZ_CONFIG_GROUP(editorFillOnDropGroup, "Editor.FillOnDrop")

    // Unmanaged groups (not purged by save(), written independently)
    PZ_CONFIG_GROUP(tilingQuickLayoutSlotsGroup, "TilingQuickLayoutSlots")
    PZ_CONFIG_GROUP(windowTrackingGroup, "WindowTracking")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Window Tracking (session.json, written by WTA)
    //
    // The WindowTracking group stores ephemeral per-session window state that is
    // NOT part of user preferences. It lives in session.json (separate from
    // config.json) to avoid write contention with user preference saves.
    // Owned by WindowTrackingAdaptor and saved via its debounced save cycle.
    //
    // Autotile data is split across three locations:
    //   1. Tiling.Algorithm group (Settings, config.json) — global defaults:
    //      algorithm, splitRatio, masterCount, maxWindows, splitRatioStep,
    //      perAlgorithmSettings
    //   2. AutotileScreen:<id> groups (Settings, config.json) — per-screen
    //      overrides for masterCount, splitRatio, algorithm
    //   3. WindowTracking.AutotileWindowOrders (WTA, session.json) — per-context
    //      window order and floating state
    //
    // Settings (1, 2) are user preferences. WindowTracking (3) is session state.
    // Settings::reset() deletes session.json and per-screen groups.
    // ═══════════════════════════════════════════════════════════════════════════

    // Snap mode — zone assignments and restore queues
    PZ_CONFIG_KEY(activeLayoutIdKey, "ActiveLayoutId")
    PZ_CONFIG_KEY(windowZoneAssignmentsFullKey, "WindowZoneAssignmentsFull")
    PZ_CONFIG_KEY(pendingRestoreQueuesKey, "PendingRestoreQueues")

    // Snap mode — pre-tile geometry (for float-toggle restore)
    PZ_CONFIG_KEY(preTileGeometriesFullKey, "PreTileGeometriesFull")
    PZ_CONFIG_KEY(preTileGeometriesKey, "PreTileGeometries")

    // Snap mode — last used zone info
    PZ_CONFIG_KEY(lastUsedZoneIdKey, "LastUsedZoneId")
    PZ_CONFIG_KEY(lastUsedScreenNameKey, "LastUsedScreenName")
    PZ_CONFIG_KEY(lastUsedZoneClassKey, "LastUsedZoneClass")
    PZ_CONFIG_KEY(lastUsedDesktopKey, "LastUsedDesktop")

    // Snap mode — pre-float state (for unfloating after session restore)
    PZ_CONFIG_KEY(preFloatZoneAssignmentsKey, "PreFloatZoneAssignments")
    PZ_CONFIG_KEY(preFloatScreenAssignmentsKey, "PreFloatScreenAssignments")

    // Snap mode — user-snapped classes
    PZ_CONFIG_KEY(userSnappedClassesKey, "UserSnappedClasses")

    // Autotile mode — per-context window order and floating state
    PZ_CONFIG_KEY(autotileWindowOrdersKey, "AutotileWindowOrders")
    // Autotile mode — pending restore queue for close/reopen window preservation
    PZ_CONFIG_KEY(autotilePendingRestoresKey, "AutotilePendingRestores")

    // Obsolete keys (cleaned up on save to prevent stale data)
    PZ_CONFIG_KEY(obsoleteFloatingWindowsKey, "FloatingWindows")
    PZ_CONFIG_KEY(obsoletePendingWindowScreenAssignmentsKey, "PendingWindowScreenAssignments")
    PZ_CONFIG_KEY(obsoletePendingWindowDesktopAssignmentsKey, "PendingWindowDesktopAssignments")
    PZ_CONFIG_KEY(obsoletePendingWindowLayoutAssignmentsKey, "PendingWindowLayoutAssignments")
    PZ_CONFIG_KEY(obsoletePendingWindowZoneNumbersKey, "PendingWindowZoneNumbers")
    PZ_CONFIG_KEY(obsoleteWindowZoneAssignmentsKey, "WindowZoneAssignments")
    PZ_CONFIG_KEY(obsoleteWindowScreenAssignmentsKey, "WindowScreenAssignments")
    PZ_CONFIG_KEY(obsoleteWindowDesktopAssignmentsKey, "WindowDesktopAssignments")

    // ═══════════════════════════════════════════════════════════════════════════
    // Trigger JSON Field Names
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(triggerModifierField, "modifier")
    PZ_CONFIG_KEY(triggerMouseButtonField, "mouseButton")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Rendering
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(backendKey, "Backend")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping (top-level)
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(enabledKey, "Enabled")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Behavior
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(triggersKey, "Triggers")
    PZ_CONFIG_KEY(toggleActivationKey, "ToggleActivation")

    // Snapping.Behavior.ZoneSpan
    // (uses enabledKey and triggersKey)

    // Snapping.Behavior.SnapAssist
    PZ_CONFIG_KEY(featureEnabledKey, "FeatureEnabled")
    // (also uses enabledKey and triggersKey)

    // Snapping.Behavior.Display
    PZ_CONFIG_KEY(showOnAllMonitorsKey, "ShowOnAllMonitors")
    PZ_CONFIG_KEY(filterByAspectRatioKey, "FilterByAspectRatio")

    // Snapping.Behavior.WindowHandling
    PZ_CONFIG_KEY(keepOnResolutionChangeKey, "KeepOnResolutionChange")
    PZ_CONFIG_KEY(moveNewToLastZoneKey, "MoveNewToLastZone")
    PZ_CONFIG_KEY(restoreOnUnsnapKey, "RestoreOnUnsnap")
    PZ_CONFIG_KEY(restoreOnLoginKey, "RestoreOnLogin")
    PZ_CONFIG_KEY(autoAssignAllLayoutsKey, "AutoAssignAllLayouts")
    PZ_CONFIG_KEY(stickyWindowHandlingKey, "StickyWindowHandling")
    PZ_CONFIG_KEY(defaultLayoutIdKey, "DefaultLayoutId")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Appearance.Colors
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(useSystemKey, "UseSystem")
    PZ_CONFIG_KEY(highlightKey, "Highlight")
    PZ_CONFIG_KEY(inactiveKey, "Inactive")
    PZ_CONFIG_KEY(borderKey, "Border")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Appearance.Opacity
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(activeKey, "Active")
    // (also uses inactiveKey)

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Appearance.Border
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(widthKey, "Width")
    PZ_CONFIG_KEY(radiusKey, "Radius")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Appearance.Labels
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(fontColorKey, "FontColor")
    PZ_CONFIG_KEY(fontFamilyKey, "FontFamily")
    PZ_CONFIG_KEY(fontSizeScaleKey, "FontSizeScale")
    PZ_CONFIG_KEY(fontWeightKey, "FontWeight")
    PZ_CONFIG_KEY(fontItalicKey, "FontItalic")
    PZ_CONFIG_KEY(fontUnderlineKey, "FontUnderline")
    PZ_CONFIG_KEY(fontStrikeoutKey, "FontStrikeout")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Effects
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(blurKey, "Blur")
    PZ_CONFIG_KEY(showNumbersKey, "ShowNumbers")
    PZ_CONFIG_KEY(flashOnSwitchKey, "FlashOnSwitch")
    PZ_CONFIG_KEY(osdOnLayoutSwitchKey, "OsdOnLayoutSwitch")
    PZ_CONFIG_KEY(osdOnDesktopSwitchKey, "OsdOnDesktopSwitch")
    PZ_CONFIG_KEY(navigationOsdKey, "NavigationOsd")
    PZ_CONFIG_KEY(osdStyleKey, "OsdStyle")
    PZ_CONFIG_KEY(overlayDisplayModeKey, "OverlayDisplayMode")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.ZoneSelector
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)
    PZ_CONFIG_KEY(triggerDistanceKey, "TriggerDistance")
    PZ_CONFIG_KEY(positionKey, "Position")
    PZ_CONFIG_KEY(layoutModeKey, "LayoutMode")
    PZ_CONFIG_KEY(sizeModeKey, "SizeMode")
    PZ_CONFIG_KEY(maxRowsKey, "MaxRows")
    PZ_CONFIG_KEY(previewWidthKey, "PreviewWidth")
    PZ_CONFIG_KEY(previewHeightKey, "PreviewHeight")
    PZ_CONFIG_KEY(previewLockAspectKey, "PreviewLockAspect")
    PZ_CONFIG_KEY(gridColumnsKey, "GridColumns")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Gaps
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(innerKey, "Inner")
    PZ_CONFIG_KEY(outerKey, "Outer")
    PZ_CONFIG_KEY(usePerSideKey, "UsePerSide")
    PZ_CONFIG_KEY(topKey, "Top")
    PZ_CONFIG_KEY(bottomKey, "Bottom")
    PZ_CONFIG_KEY(leftKey, "Left")
    PZ_CONFIG_KEY(rightKey, "Right")
    PZ_CONFIG_KEY(adjacentThresholdKey, "AdjacentThreshold")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling (top-level)
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Algorithm
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(defaultKey, "Default")
    PZ_CONFIG_KEY(splitRatioKey, "SplitRatio")
    PZ_CONFIG_KEY(splitRatioStepKey, "SplitRatioStep")
    PZ_CONFIG_KEY(masterCountKey, "MasterCount")
    PZ_CONFIG_KEY(maxWindowsKey, "MaxWindows")
    PZ_CONFIG_KEY(perAlgorithmSettingsKey, "PerAlgorithmSettings")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Behavior
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(insertPositionKey, "InsertPosition")
    PZ_CONFIG_KEY(focusNewWindowsKey, "FocusNewWindows")
    PZ_CONFIG_KEY(focusFollowsMouseKey, "FocusFollowsMouse")
    PZ_CONFIG_KEY(respectMinimumSizeKey, "RespectMinimumSize")
    // (also uses stickyWindowHandlingKey)
    PZ_CONFIG_KEY(dragBehaviorKey, "DragBehavior")
    PZ_CONFIG_KEY(overflowBehaviorKey, "OverflowBehavior")
    PZ_CONFIG_KEY(lockedScreensKey, "LockedScreens")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Appearance.Colors
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses useSystemKey, activeKey, inactiveKey)

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Appearance.Decorations
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(hideTitleBarsKey, "HideTitleBars")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Appearance.Borders
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(showBorderKey, "ShowBorder")
    // (also uses widthKey, radiusKey)

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Gaps
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses innerKey, outerKey, usePerSideKey, topKey, bottomKey, leftKey, rightKey)
    PZ_CONFIG_KEY(smartGapsKey, "SmartGaps")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Exclusions
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(transientWindowsKey, "TransientWindows")
    PZ_CONFIG_KEY(notificationsAndOsdKey, "NotificationsAndOsd")
    PZ_CONFIG_KEY(minimumWindowWidthKey, "MinimumWindowWidth")
    PZ_CONFIG_KEY(minimumWindowHeightKey, "MinimumWindowHeight")
    // Note: the per-list `Applications` / `WindowClasses` leaf-key accessors
    // were retired with the v4 fold of exclusion lists into Application-
    // subject WindowRules — the migration reads from `v3ExcludedApplicationsKey`
    // / `v3ExcludedWindowClassesKey` below, and no live config path remains.

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Performance
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(pollIntervalMsKey, "PollIntervalMs")
    PZ_CONFIG_KEY(minimumZoneSizePxKey, "MinimumZoneSizePx")
    PZ_CONFIG_KEY(minimumZoneDisplaySizePxKey, "MinimumZoneDisplaySizePx")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Shaders
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)
    PZ_CONFIG_KEY(frameRateKey, "FrameRate")
    PZ_CONFIG_KEY(audioVisualizerKey, "AudioVisualizer")
    PZ_CONFIG_KEY(audioSpectrumBarCountKey, "AudioSpectrumBarCount")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Animations
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)
    // Phase 4 sub-commit 6: animation fields migrated from 5 per-field
    // keys (duration / easingCurve / minDistance / sequenceMode /
    // staggerInterval) to a single Profile JSON blob under animationProfileKey.
    // The v1 keys are folded into the Profile blob by `migrateV1ToV2`
    // (see `configmigration.cpp`). The v1 key accessors are retained for
    // that migration function only; no separate backwards-compat code
    // exists within v2. The per-field accessor surface on Settings
    // (animationDuration / etc.) is preserved and projects through the
    // Profile blob at read/write time for QML Q_PROPERTY binding
    // compatibility.
    PZ_CONFIG_KEY(animationProfileKey, "Profile")
    PZ_CONFIG_KEY(durationKey, "Duration")
    PZ_CONFIG_KEY(easingCurveKey, "EasingCurve")
    PZ_CONFIG_KEY(minDistanceKey, "MinDistance")
    PZ_CONFIG_KEY(sequenceModeKey, "SequenceMode")
    PZ_CONFIG_KEY(staggerIntervalKey, "StaggerInterval")

    // Phase 6: ShaderProfileTree JSON blob — per-event shader effect
    // selection layered alongside the motion Profile (separate tree,
    // same dot-path namespace — see design doc decision AA).
    PZ_CONFIG_KEY(shaderProfileTreeKey, "ShaderProfileTree")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Shortcuts.Global
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(openEditorKey, "OpenEditor")
    PZ_CONFIG_KEY(openSettingsKey, "OpenSettings")
    PZ_CONFIG_KEY(previousLayoutKey, "PreviousLayout")
    PZ_CONFIG_KEY(nextLayoutKey, "NextLayout")

    // Parameterized — uses the pattern accessor to avoid duplication.
    // 1..9 mirrors quickLayoutN() in the enum surface; out-of-range
    // values would round-trip as e.g. "QuickLayout100" and ghost the
    // config namespace.
    PZ_CONFIG_KEY(quickLayoutKeyPattern, "QuickLayout%1")
    static QString quickLayoutKey(int n)
    {
        // qFatal aborts unambiguously in both debug and release builds —
        // the contract is "n in 1..9, no exceptions". A bare Q_ASSERT_X
        // would compile out in release and let an out-of-range value
        // silently yield "QuickLayout100" (or similar), ghosting the
        // config namespace.
        if (n < 1 || n > 9) {
            qFatal("quickLayoutKey: n out of range: %d", n);
        }
        return quickLayoutKeyPattern().arg(n);
    }

    PZ_CONFIG_KEY(moveWindowLeftKey, "MoveWindowLeft")
    PZ_CONFIG_KEY(moveWindowRightKey, "MoveWindowRight")
    PZ_CONFIG_KEY(moveWindowUpKey, "MoveWindowUp")
    PZ_CONFIG_KEY(moveWindowDownKey, "MoveWindowDown")
    PZ_CONFIG_KEY(focusZoneLeftKey, "FocusZoneLeft")
    PZ_CONFIG_KEY(focusZoneRightKey, "FocusZoneRight")
    PZ_CONFIG_KEY(focusZoneUpKey, "FocusZoneUp")
    PZ_CONFIG_KEY(focusZoneDownKey, "FocusZoneDown")
    PZ_CONFIG_KEY(pushToEmptyZoneKey, "PushToEmptyZone")
    PZ_CONFIG_KEY(restoreWindowSizeKey, "RestoreWindowSize")
    PZ_CONFIG_KEY(toggleWindowFloatKey, "ToggleWindowFloat")
    PZ_CONFIG_KEY(swapWindowLeftKey, "SwapWindowLeft")
    PZ_CONFIG_KEY(swapWindowRightKey, "SwapWindowRight")
    PZ_CONFIG_KEY(swapWindowUpKey, "SwapWindowUp")
    PZ_CONFIG_KEY(swapWindowDownKey, "SwapWindowDown")

    // Parameterized — uses the pattern accessor to avoid duplication.
    // 1..9 mirrors snapToZoneN() in the enum surface.
    PZ_CONFIG_KEY(snapToZoneKeyPattern, "SnapToZone%1")
    static QString snapToZoneKey(int n)
    {
        // See quickLayoutKey above for the rationale on the qFatal guard.
        if (n < 1 || n > 9) {
            qFatal("snapToZoneKey: n out of range: %d", n);
        }
        return snapToZoneKeyPattern().arg(n);
    }

    PZ_CONFIG_KEY(rotateWindowsClockwiseKey, "RotateWindowsClockwise")
    PZ_CONFIG_KEY(rotateWindowsCounterclockwiseKey, "RotateWindowsCounterclockwise")
    PZ_CONFIG_KEY(cycleWindowForwardKey, "CycleWindowForward")
    PZ_CONFIG_KEY(cycleWindowBackwardKey, "CycleWindowBackward")
    PZ_CONFIG_KEY(resnapToNewLayoutKey, "ResnapToNewLayout")
    PZ_CONFIG_KEY(snapAllWindowsKey, "SnapAllWindows")
    PZ_CONFIG_KEY(layoutPickerKey, "LayoutPicker")
    PZ_CONFIG_KEY(toggleLayoutLockKey, "ToggleLayoutLock")
    PZ_CONFIG_KEY(swapVirtualScreenLeftKey, "SwapVirtualScreenLeft")
    PZ_CONFIG_KEY(swapVirtualScreenRightKey, "SwapVirtualScreenRight")
    PZ_CONFIG_KEY(swapVirtualScreenUpKey, "SwapVirtualScreenUp")
    PZ_CONFIG_KEY(swapVirtualScreenDownKey, "SwapVirtualScreenDown")
    PZ_CONFIG_KEY(rotateVirtualScreensClockwiseKey, "RotateVirtualScreensClockwise")
    PZ_CONFIG_KEY(rotateVirtualScreensCounterclockwiseKey, "RotateVirtualScreensCounterclockwise")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Shortcuts.Tiling
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(toggleKey, "Toggle")
    PZ_CONFIG_KEY(focusMasterKey, "FocusMaster")
    PZ_CONFIG_KEY(swapMasterKey, "SwapMaster")
    PZ_CONFIG_KEY(incMasterRatioKey, "IncMasterRatio")
    PZ_CONFIG_KEY(decMasterRatioKey, "DecMasterRatio")
    PZ_CONFIG_KEY(incMasterCountKey, "IncMasterCount")
    PZ_CONFIG_KEY(decMasterCountKey, "DecMasterCount")
    PZ_CONFIG_KEY(retileKey, "Retile")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Editor.Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(duplicateKey, "Duplicate")
    PZ_CONFIG_KEY(splitHorizontalKey, "SplitHorizontal")
    PZ_CONFIG_KEY(splitVerticalKey, "SplitVertical")
    PZ_CONFIG_KEY(fillKey, "Fill")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Editor.Snapping
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(gridEnabledKey, "GridEnabled")
    PZ_CONFIG_KEY(edgeEnabledKey, "EdgeEnabled")
    PZ_CONFIG_KEY(intervalKey, "Interval")
    PZ_CONFIG_KEY(intervalXKey, "IntervalX")
    PZ_CONFIG_KEY(intervalYKey, "IntervalY")
    PZ_CONFIG_KEY(overrideModifierKey, "OverrideModifier")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Editor.FillOnDrop
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)
    PZ_CONFIG_KEY(modifierKey, "Modifier")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Virtual Screens
    // The "VirtualScreen:" prefix is intentional — parsing must use
    // groupName.mid(prefix.size()) to extract the physical screen ID,
    // NOT split on ':',  because screen IDs themselves may contain colons
    // (e.g. "BNQ:BenQ PD3220U:serial").
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(virtualScreenGroupPrefix, "VirtualScreen:")
    PZ_CONFIG_KEY(virtualScreenCountKey, "count")
    PZ_CONFIG_KEY(virtualScreenXKey, "x")
    PZ_CONFIG_KEY(virtualScreenYKey, "y")
    PZ_CONFIG_KEY(virtualScreenWidthKey, "width")
    PZ_CONFIG_KEY(virtualScreenHeightKey, "height")
    PZ_CONFIG_KEY(virtualScreenNameKey, "name")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Ordering
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_KEY(snappingLayoutOrderKey, "SnappingLayoutOrder")
    PZ_CONFIG_KEY(tilingAlgorithmOrderKey, "TilingAlgorithmOrder")

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-Screen Config Group Prefixes
    // ═══════════════════════════════════════════════════════════════════════════

    PZ_CONFIG_GROUP(zoneSelectorGroupPrefix, "ZoneSelector:")
    PZ_CONFIG_GROUP(autotileScreenGroupPrefix, "AutotileScreen:")
    PZ_CONFIG_GROUP(snappingScreenGroupPrefix, "SnappingScreen:")

    // ═══════════════════════════════════════════════════════════════════════════
    // Legacy v1/v2/v3/v4 accessors — used ONLY by migration code.
    //
    // Wrapped in a nested `Legacy` struct so a stray
    // ConfigKeys::v1ActivationGroup() call outside configmigration.cpp
    // fails at the read-time `Legacy::` prefix lookup, surfacing the
    // accidental dependence at code-review time rather than as a
    // silent regression. configmigration.cpp uses the qualified form
    // `ConfigKeys::Legacy::v1*` throughout.
    // ═══════════════════════════════════════════════════════════════════════════

    struct Legacy
    {
        // Some v1 names are identical to their v2 counterparts (marked "= v2")
        // because the group name didn't change — only the keys inside were
        // restructured. They exist as separate accessors so migration code
        // reads unambiguously as "reading from v1 source" vs "writing to v2
        // destination".
        PZ_CONFIG_GROUP(v1ActivationGroup, "Activation")
        PZ_CONFIG_GROUP(v1DisplayGroup, "Display")
        PZ_CONFIG_GROUP(v1AppearanceGroup, "Appearance")
        PZ_CONFIG_GROUP(v1ZonesGroup, "Zones")
        PZ_CONFIG_GROUP(v1BehaviorGroup, "Behavior")
        PZ_CONFIG_GROUP(v1ExclusionsGroup, "Exclusions") // = v2 exclusionsGroup
        PZ_CONFIG_GROUP(v1ZoneSelectorGroup, "ZoneSelector")
        PZ_CONFIG_GROUP(v1AutotilingGroup, "Autotiling")
        PZ_CONFIG_GROUP(v1AutotileShortcutsGroup, "AutotileShortcuts")
        PZ_CONFIG_GROUP(v1AnimationsGroup, "Animations") // = v2 animationsGroup
        // v1 animation per-field keys — Phase-4 migration packs these into the
        // v2 `Profile` JSON blob (see configmigration.cpp::migrateV1ToV2).
        // The accessors exist solely so migration code is unambiguous about
        // "reading legacy field" vs "reading new blob"; production reads after
        // migration go through `Profile::JsonField*` constants.
        PZ_CONFIG_KEY(v1AnimationsEnabledKey, "AnimationsEnabled")
        PZ_CONFIG_KEY(v1AnimationDurationKey, "AnimationDuration")
        PZ_CONFIG_KEY(v1AnimationEasingCurveKey, "AnimationEasingCurve")
        PZ_CONFIG_KEY(v1AnimationMinDistanceKey, "AnimationMinDistance")
        PZ_CONFIG_KEY(v1AnimationSequenceModeKey, "AnimationSequenceMode")
        PZ_CONFIG_KEY(v1AnimationStaggerIntervalKey, "AnimationStaggerInterval")
        /// v1 INI key for the rendering backend selection — both the v1→v2 migration
        /// step and the v1 INI dispatcher consume this through one accessor so a
        /// future rename of the literal can't drift one site behind the other.
        PZ_CONFIG_KEY(v1RenderingBackendKey, "RenderingBackend")
        PZ_CONFIG_GROUP(v1GlobalShortcutsGroup, "GlobalShortcuts")
        PZ_CONFIG_GROUP(v1EditorGroup, "Editor") // = v2 editorGroup
        PZ_CONFIG_GROUP(v1OrderingGroup, "Ordering") // = v2 orderingGroup
        PZ_CONFIG_GROUP(v1RenderingGroup, "Rendering") // = v2 renderingGroup
        PZ_CONFIG_GROUP(v1ShadersGroup, "Shaders") // = v2 shadersGroup
        // v1 WindowTracking group — only read in the v1→v2 step where it's
        // moved out to session.json. The live runtime accessor
        // `ConfigKeys::windowTrackingGroup()` happens to return the same
        // "WindowTracking" string today, but a future rename of the live
        // accessor must not silently retarget this read at a path no v1
        // INI ever held — that would drop user session state.
        PZ_CONFIG_GROUP(v1WindowTrackingGroup, "WindowTracking")

        // v2 legacy keys — used ONLY by migrateV2ToV3.
        // The v2 group itself (Snapping.Behavior.Display) lives on past v3 — it
        // still holds ShowOnAllMonitors and FilterByAspectRatio. Only the three
        // disabled-* keys move out, so we name the keys with a v2 prefix.
        // The migration uses the frozen `v2SnappingBehaviorDisplayGroup` group
        // accessor to descend the JSON tree — a future rename of the LIVE
        // `snappingBehaviorDisplayGroup()` accessor must NOT silently retarget
        // the v2→v3 step to a path no v2 config ever had on disk (the same
        // freeze-policy hazard the v3→v4 step avoids by using `v3DisplayGroup`).
        PZ_CONFIG_GROUP(v2SnappingBehaviorDisplayGroup, "Snapping.Behavior.Display")
        PZ_CONFIG_KEY(v2DisabledMonitorsKey, "DisabledMonitors")
        PZ_CONFIG_KEY(v2DisabledDesktopsKey, "DisabledDesktops")
        PZ_CONFIG_KEY(v2DisabledActivitiesKey, "DisabledActivities")

        // v2 destination group names — used as both v1→v2 destinations
        // (in `migrateV1ToV2`) and v2 source coordinates. The frozen
        // accessors mirror the v3→v4 step's `v3DisplayGroup` pattern
        // and ensure a future rename of the matching live
        // `snappingGroup()` / `tilingGroup()` / etc. accessor does not
        // silently retarget the migration to a path no v2 config ever
        // had on disk.
        PZ_CONFIG_GROUP(v2SnappingGroup, "Snapping")
        PZ_CONFIG_GROUP(v2TilingGroup, "Tiling")
        PZ_CONFIG_GROUP(v2PerformanceGroup, "Performance")
        PZ_CONFIG_GROUP(v2ExclusionsGroup, "Exclusions")
        PZ_CONFIG_GROUP(v2RenderingGroup, "Rendering")
        PZ_CONFIG_GROUP(v2ShadersGroup, "Shaders")
        PZ_CONFIG_GROUP(v2ShortcutsGroup, "Shortcuts")
        PZ_CONFIG_GROUP(v2EditorGroup, "Editor")
        PZ_CONFIG_GROUP(v2OrderingGroup, "Ordering")
        // Parameterised v2 destinations: v1→v2 renames v1's
        // `QuickLayout%1Shortcut` to v2's `QuickLayout%1` and preserves
        // v1's `SnapToZone%1` verbatim. Frozen accessors pin the v2
        // wire-format names so a future rename of the matching live
        // pattern accessors stays isolated from migration.
        PZ_CONFIG_KEY(v1QuickLayoutShortcutKeyPattern, "QuickLayout%1Shortcut")
        PZ_CONFIG_KEY(v2QuickLayoutKeyPattern, "QuickLayout%1")
        PZ_CONFIG_KEY(v2SnapToZoneKeyPattern, "SnapToZone%1")
        static QString v1QuickLayoutShortcutKey(int n)
        {
            if (n < 1 || n > 9) {
                qFatal("Legacy::v1QuickLayoutShortcutKey: n out of range: %d", n);
            }
            return v1QuickLayoutShortcutKeyPattern().arg(n);
        }
        static QString v2QuickLayoutKey(int n)
        {
            if (n < 1 || n > 9) {
                qFatal("Legacy::v2QuickLayoutKey: n out of range: %d", n);
            }
            return v2QuickLayoutKeyPattern().arg(n);
        }
        static QString v2SnapToZoneKey(int n)
        {
            if (n < 1 || n > 9) {
                qFatal("Legacy::v2SnapToZoneKey: n out of range: %d", n);
            }
            return v2SnapToZoneKeyPattern().arg(n);
        }

        // v3 legacy keys/groups — used ONLY by migration code.
        //
        // Per-mode disable keys (`v3*DisabledMonitorsKey` etc.) lived in the v3
        // Display group; migrateV2ToV3 wrote them there and migrateV3ToV4 reads
        // and removes them as the values move into windowrules.json. They no
        // longer exist on disk at runtime (v4+) — Settings::disableEntriesFor /
        // writeDisableEntries route through the rule store instead.
        //
        // Group/prefix accessors (`v3assignmentGroupPrefix`, `v3quickLayoutsGroup`,
        // `v3modeTrackingGroup`) describe the assignments.json layout migrateV1ToV2
        // produced and finalizeV4Conversion drains. Runtime no longer touches
        // these on-disk shapes — LayoutRegistry reads the unified rule store via
        // m_ruleStore->load().
        PZ_CONFIG_KEY(v3snappingDisabledMonitorsKey, "SnappingDisabledMonitors")
        PZ_CONFIG_KEY(v3autotileDisabledMonitorsKey, "AutotileDisabledMonitors")
        PZ_CONFIG_KEY(v3snappingDisabledDesktopsKey, "SnappingDisabledDesktops")
        PZ_CONFIG_KEY(v3autotileDisabledDesktopsKey, "AutotileDisabledDesktops")
        PZ_CONFIG_KEY(v3snappingDisabledActivitiesKey, "SnappingDisabledActivities")
        PZ_CONFIG_KEY(v3autotileDisabledActivitiesKey, "AutotileDisabledActivities")
        PZ_CONFIG_GROUP(v3assignmentGroupPrefix, "Assignment:")
        PZ_CONFIG_GROUP(v3quickLayoutsGroup, "QuickLayouts")
        PZ_CONFIG_GROUP(v3modeTrackingGroup, "ModeTracking")

        // v4 legacy keys/groups — used ONLY by migration code.
        //
        // The `Animations.AnimationAppRules` array carried per-window animation
        // overrides up through v4. migrateV3ToV4 stashes that array for
        // finalizeV4Conversion to convert into WindowRules, then removes the key
        // permanently. The group name `Animations` is unchanged at runtime (it
        // still hosts ShaderProfileTree), but the key accessor is migration-only:
        // it lives here so the migration is the sole remaining reader of the v4
        // wire format — the live ConfigDefaults accessors for the key and its
        // default value have been removed.
        //
        // `v4AnimationsGroup` deliberately duplicates the live `animationsGroup()`
        // accessor's literal ("Animations") rather than aliasing it. The migration
        // reads from the FROZEN v4 on-disk name; a future rename of
        // `animationsGroup()` (v5+ runtime) MUST NOT silently retarget the v4
        // migration to a path that never existed on disk in v4-and-earlier
        // configs. Do not consolidate these two accessors.
        //
        // The same freeze policy applies to every accessor in this `Legacy`
        // struct: each one names a v1/v2/v3/v4 on-disk shape the runtime no
        // longer touches. The `v3*` group/key accessors below duplicate live
        // ConfigDefaults literals on purpose — the v3→v4 migration must read
        // from the path that existed in v3 configs on disk, even if a future
        // schema bump renames the live accessor. Consolidating Legacy accessors
        // with their live counterparts would silently retarget the migration on
        // the next rename. Do not do it.
        PZ_CONFIG_GROUP(v4AnimationsGroup, "Animations")
        PZ_CONFIG_KEY(v4AnimationAppRulesKey, "AnimationAppRules")

        // v4 migration scratch-root keys — set on the root by `migrateV3ToV4`
        // and consumed by `finalizeV4Conversion`. `Settings::purgeStaleKeys`
        // (src/config/settings.cpp) ALSO references these to preserve them
        // across save() cycles when the chain stalls (see the stalled-chain
        // gate in `finalizeV4Conversion`); routing both call sites through
        // the same frozen accessor stops a future rename in one file from
        // silently breaking the protection in the other.
        PZ_CONFIG_KEY(v4DisableStashKey, "_v4DisableStash")
        PZ_CONFIG_KEY(v4AnimationRulesStashKey, "_v4AnimationRulesStash")
        // Third v4 scratch-root key — set on the root by `migrateV3ToV4` from
        // the legacy `Exclusions.{Applications,WindowClasses}` lists and
        // consumed by `finalizeV4Conversion`, which converts each surviving
        // pattern into an Application-subject `AppId AppIdMatches <pattern>
        // Exclude` WindowRule. Same purge-protection semantics as the two
        // sibling stash keys above.
        PZ_CONFIG_KEY(v4ExclusionStashKey, "_v4ExclusionStash")
        // Fourth v4 scratch-root key — set on the root by `migrateV3ToV4`
        // from the legacy `Animations.WindowFiltering.{Applications,WindowClasses}`
        // lists and consumed by `finalizeV4Conversion`, which converts each
        // surviving pattern into a `DesktopFile`/`WindowClass Contains
        // <pattern> → ExcludeAnimations` WindowRule (preserving the
        // legacy effect-bridge match-field split). Same purge-protection
        // semantics as the three sibling stash keys above.
        PZ_CONFIG_KEY(v4AnimationExclusionStashKey, "_v4AnimationExclusionStash")

        // v3 frozen group/key accessors — used ONLY by migrateV3ToV4 and
        // finalizeV4Conversion. These mirror the live `displayGroup`,
        // `defaultLayoutIdKey`, `snappingBehaviorWindowHandlingGroup`,
        // `tilingAlgorithmGroup`, and `defaultKey` accessors but are frozen
        // at their v3 literal so a future runtime rename cannot silently
        // retarget the migration to a path no v3 config ever had on disk.
        PZ_CONFIG_GROUP(v3DisplayGroup, "Display")
        PZ_CONFIG_KEY(v3DefaultLayoutIdKey, "DefaultLayoutId")
        PZ_CONFIG_GROUP(v3SnappingBehaviorWindowHandlingGroup, "Snapping.Behavior.WindowHandling")
        PZ_CONFIG_GROUP(v3TilingAlgorithmGroup, "Tiling.Algorithm")
        PZ_CONFIG_KEY(v3DefaultKey, "Default")

        // v3 Exclusions group + comma-joined pattern keys — frozen at their
        // v3 literal for the same reason the disable-list group/keys above
        // are: migrateV3ToV4 reads them from a v3 config on disk, and a
        // future runtime rename of the live `exclusionsGroup` accessor must
        // NOT silently retarget the migration to a path no v3 config ever
        // had on disk. (The per-list `Applications` / `WindowClasses` leaf
        // accessors were retired with the v4 fold — no live accessor exists
        // to drift from now, but the v3 literals stay pinned here.)
        PZ_CONFIG_GROUP(v3ExclusionsGroup, "Exclusions")
        PZ_CONFIG_KEY(v3ExcludedApplicationsKey, "Applications")
        PZ_CONFIG_KEY(v3ExcludedWindowClassesKey, "WindowClasses")
        // The animation exclusion lists live at
        // `Animations.WindowFiltering.{Applications,WindowClasses}` — same
        // leaf keys as the snapping Exclusions group above, just under a
        // different dot-path. The two segments ("Animations" and
        // "WindowFiltering") aren't wrapped in their own frozen accessors
        // because migrateV3ToV4 walks the path inline; the segment
        // literals are spelled out next to v4AnimationsGroup's freeze
        // comment in the migration body for the same forensic-readability
        // reason.

        // v3 assignments.json field names — frozen literals from the dead
        // v3 assignments.json schema. finalizeV4Conversion is the sole
        // remaining reader; these are NOT live config keys.
        PZ_CONFIG_KEY(v3AssignmentMode, "Mode")
        PZ_CONFIG_KEY(v3AssignmentLayout, "SnappingLayout")
        PZ_CONFIG_KEY(v3AssignmentAlgorithm, "TilingAlgorithm")
    };

    // ═══════════════════════════════════════════════════════════════════════════
    // Settings-app session keys
    //
    // These do NOT live in the main config JSON — they're per-organization
    // QSettings entries (~/.config/<org>/<app>.conf) for the settings UI's
    // own ephemeral state: last window geometry, dismissed update banner,
    // last-seen what's-new version. Centralised here so the CLAUDE.md "no
    // inline QStringLiteral for config keys" rule applies uniformly.
    // ═══════════════════════════════════════════════════════════════════════════
    PZ_CONFIG_KEY(settingsAppWindowXKey, "x")
    PZ_CONFIG_KEY(settingsAppWindowYKey, "y")
    PZ_CONFIG_KEY(settingsAppWindowWidthKey, "width")
    PZ_CONFIG_KEY(settingsAppWindowHeightKey, "height")
    PZ_CONFIG_KEY(settingsAppDismissedUpdateVersionKey, "dismissedUpdateVersion")
    PZ_CONFIG_KEY(settingsAppLastSeenWhatsNewVersionKey, "lastSeenWhatsNewVersion")

    // ═══════════════════════════════════════════════════════════════════════════
    // Filesystem paths under XDG_DATA_HOME
    //
    // Daemon, settings app, and editor all read/write the same per-user
    // layouts and algorithms directories. Hoisted into one accessor each so
    // a rename only touches one site.
    // ═══════════════════════════════════════════════════════════════════════════
    PZ_CONFIG_KEY(userDataSubdir, "plasmazones")
    PZ_CONFIG_KEY(layoutsSubdir, "plasmazones/layouts")
    PZ_CONFIG_KEY(algorithmsSubdir, "plasmazones/algorithms")

private:
    // Non-instantiable
    ConfigKeys() = delete;
};

// ─── Disable-rule label helpers ─────────────────────────────────────────────
// Shared between the live Settings disable-list writer
// (Settings::writeDisableEntries) and the v3→v4 migration's disable-rule
// builders. Both call sites must produce the same `WindowRule::name` string for
// a given (mode, screen, desktop, activity) tuple so that resaving an existing
// disable list (e.g. after a UI edit) doesn't fork into two slightly different
// labels for what is otherwise the same rule.
//
// These are NOT translated. `WindowRule::name` is the persisted identity
// surface in windowrules.json; running the app under different locales must
// not change its on-disk text. The rule editor surfaces the name verbatim,
// matching the historic behaviour.
inline QString autotileDisableRulePrefix()
{
    return QStringLiteral("Autotile off · ");
}

inline QString snappingDisableRulePrefix()
{
    return QStringLiteral("Snapping off · ");
}

inline QString scrollingDisableRulePrefix()
{
    return QStringLiteral("Scrolling off · ");
}

/// Persistent label-prefix for the WindowRule::name field of a per-mode
/// disable rule. Exhaustive switch — a future `Mode` enum value added in
/// `AssignmentEntry.h` without an entry here fires a `Q_UNREACHABLE`
/// diagnostic rather than silently producing an empty prefix that lands
/// in the persisted `WindowRule::name` as bare ` · DP-1` (parseable but
/// anonymous, and identical across modes — losing the screen→mode
/// affinity that makes the rule editor scannable).
inline QString disableRulePrefixFor(PhosphorZones::AssignmentEntry::Mode mode)
{
    switch (mode) {
    case PhosphorZones::AssignmentEntry::Snapping:
        return snappingDisableRulePrefix();
    case PhosphorZones::AssignmentEntry::Autotile:
        return autotileDisableRulePrefix();
    case PhosphorZones::AssignmentEntry::Scrolling:
        return scrollingDisableRulePrefix();
    }
    Q_UNREACHABLE();
    return QString();
}

inline QString disableRuleDesktopSuffix(int desktop)
{
    return QStringLiteral(" · Desktop ") + QString::number(desktop);
}

inline QString disableRuleActivitySuffix()
{
    return QStringLiteral(" · Activity");
}

} // namespace PlasmaZones

#undef PZ_CONFIG_KEY
#undef PZ_CONFIG_GROUP
