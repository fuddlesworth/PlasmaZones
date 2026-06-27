// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorZones/AssignmentEntry.h>

#include <QString>

// Macro to define a static config key accessor returning a QStringLiteral.
// Usage: P_CONFIG_KEY(snappingEnabledKey, "SnappingEnabled")
// Expands to: static QString snappingEnabledKey() { return QStringLiteral("SnappingEnabled"); }
#define P_CONFIG_KEY(name, str)                                                                                        \
    static QString name()                                                                                              \
    {                                                                                                                  \
        return QStringLiteral(str);                                                                                    \
    }

// Alias for group-name accessors — same body as P_CONFIG_KEY, single
// definition so a future tweak to P_CONFIG_KEY (e.g. attribute
// annotation) automatically applies to groups too. Separate macro
// name preserved for readability at the call sites.
#define P_CONFIG_GROUP(name, str) P_CONFIG_KEY(name, str)

// Single definition point for the per-screen group prefix spellings.
// Shared by the *GroupPrefix accessors below (which append the ':') and
// PerScreenPathResolver's prefix→category mapping table — a rename here
// updates both in lockstep instead of silently desyncing the JSON path
// resolver from the group accessors.
//
// MIGRATION-FROZEN: configmigration.cpp's v1→v2 INI migration matches
// per-screen groups through the live resolver (and therefore through these
// spellings). Renaming any of them would retarget the v1 migration to
// names no historical INI ever held, silently dropping per-screen
// overrides on migration. A rename therefore requires a schema-version
// bump with frozen Legacy copies of the old spellings — same policy as
// the ConfigKeys::Legacy accessors.
#define P_PER_SCREEN_PREFIX_ZONE_SELECTOR "ZoneSelector"
#define P_PER_SCREEN_PREFIX_AUTOTILE "AutotileScreen"
#define P_PER_SCREEN_PREFIX_SNAPPING "SnappingScreen"

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
    P_CONFIG_KEY(versionKey, "_version")

    // Top-level groups
    P_CONFIG_GROUP(generalGroup, "General")
    P_CONFIG_GROUP(snappingGroup, "Snapping")
    P_CONFIG_GROUP(tilingGroup, "Tiling")
    P_CONFIG_GROUP(exclusionsGroup, "Exclusions")
    P_CONFIG_GROUP(performanceGroup, "Performance")
    P_CONFIG_GROUP(renderingGroup, "Rendering")
    P_CONFIG_GROUP(shadersGroup, "Shaders")
    P_CONFIG_GROUP(animationsGroup, "Animations")
    P_CONFIG_GROUP(shortcutsGlobalGroup, "Shortcuts.Global")
    P_CONFIG_GROUP(shortcutsTilingGroup, "Shortcuts.Tiling")
    P_CONFIG_GROUP(orderingGroup, "Ordering")
    P_CONFIG_GROUP(updatesGroup, "Updates")

    // Snapping sub-groups
    P_CONFIG_GROUP(snappingZonesGroup, "Snapping.Zones")
    P_CONFIG_GROUP(snappingBehaviorGroup, "Snapping.Behavior")
    P_CONFIG_GROUP(snappingBehaviorZoneSpanGroup, "Snapping.Behavior.ZoneSpan")
    P_CONFIG_GROUP(snappingBehaviorSnapAssistGroup, "Snapping.Behavior.SnapAssist")
    P_CONFIG_GROUP(snappingBehaviorDisplayGroup, "Snapping.Behavior.Display")
    P_CONFIG_GROUP(snappingBehaviorWindowHandlingGroup, "Snapping.Behavior.WindowHandling")
    P_CONFIG_GROUP(snappingZonesColorsGroup, "Snapping.Zones.Colors")
    P_CONFIG_GROUP(snappingZonesOpacityGroup, "Snapping.Zones.Opacity")
    P_CONFIG_GROUP(snappingZonesBorderGroup, "Snapping.Zones.Border")
    P_CONFIG_GROUP(snappingZonesLabelsGroup, "Snapping.Zones.Labels")
    P_CONFIG_GROUP(snappingEffectsGroup, "Snapping.Effects")
    P_CONFIG_GROUP(snappingZoneSelectorGroup, "Snapping.ZoneSelector")
    // Snapping.Gaps holds only the snapping-specific adjacency threshold. The
    // inner/outer gap values are no longer stored in config at all: they live on
    // the managed baseline appearance rule and are read back through Settings'
    // gap getters.
    P_CONFIG_GROUP(snappingGapsGroup, "Snapping.Gaps")

    // Display (mode-neutral) — per-mode disable lists. Lives outside Snapping.*
    // because the values gate the whole product (snap + autotile), not just
    // snapping. v3 schema; in v2 these were under Snapping.Behavior.Display.
    P_CONFIG_GROUP(displayGroup, "Display")

    // Animations sub-groups
    P_CONFIG_GROUP(animationsWindowFilteringGroup, "Animations.WindowFiltering")

    // Tiling sub-groups
    P_CONFIG_GROUP(tilingAlgorithmGroup, "Tiling.Algorithm")
    P_CONFIG_GROUP(tilingBehaviorGroup, "Tiling.Behavior")
    P_CONFIG_GROUP(tilingBehaviorTriggersGroup, "Tiling.Behavior.Triggers")
    P_CONFIG_GROUP(tilingGapsGroup, "Tiling.Gaps")

    // Parent groups (for purge enumeration — covers all sub-groups)
    P_CONFIG_GROUP(shortcutsGroup, "Shortcuts")
    P_CONFIG_GROUP(editorGroup, "Editor")

    // Editor sub-groups
    P_CONFIG_GROUP(editorShortcutsGroup, "Editor.Shortcuts")
    P_CONFIG_GROUP(editorSnappingGroup, "Editor.Snapping")
    P_CONFIG_GROUP(editorFillOnDropGroup, "Editor.FillOnDrop")

    // Unmanaged groups (not purged by save(), written independently)
    P_CONFIG_GROUP(windowTrackingGroup, "WindowTracking")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Window Tracking (session.json, written by WTA)
    //
    // The WindowTracking group stores ephemeral per-session window state that is
    // NOT part of user preferences. It lives in session.json (separate from
    // config.json) to avoid write contention with user preference saves.
    // Owned by WindowTrackingAdaptor and saved via its debounced save cycle.
    //
    // Autotile preferences live in two Settings (config.json) locations:
    //   1. Tiling.Algorithm group — global defaults: algorithm, splitRatio,
    //      masterCount, maxWindows, splitRatioStep, perAlgorithmSettings
    //   2. AutotileScreen:<id> groups — per-screen overrides for masterCount,
    //      splitRatio, algorithm
    // Both are user preferences. Per-WINDOW autotile restore state (tiled position,
    // floated geometry) is NOT here — it lives in WindowTracking.WindowPlacements
    // alongside snap restore state (see below). Settings::reset() deletes
    // session.json and per-screen groups.
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(activeLayoutIdKey, "ActiveLayoutId")

    // Snap mode — last used zone info
    P_CONFIG_KEY(lastUsedZoneIdKey, "LastUsedZoneId")
    P_CONFIG_KEY(lastUsedScreenNameKey, "LastUsedScreenName")
    P_CONFIG_KEY(lastUsedZoneClassKey, "LastUsedZoneClass")
    P_CONFIG_KEY(lastUsedDesktopKey, "LastUsedDesktop")

    // User-snapped classes
    P_CONFIG_KEY(userSnappedClassesKey, "UserSnappedClasses")

    // Unified, engine-agnostic per-window placement record (WindowPlacementStore) —
    // the SOLE persisted per-window restore key for both snap and autotile.
    P_CONFIG_KEY(windowPlacementsKey, "WindowPlacements")

    // Legacy per-window restore keys — superseded by WindowPlacements. Retained
    // ONLY so saveState() can deleteKey() them, scrubbing them from any session.json
    // written by an older build. Never written, never read.
    P_CONFIG_KEY(windowZoneAssignmentsFullKey, "WindowZoneAssignmentsFull")
    P_CONFIG_KEY(pendingRestoreQueuesKey, "PendingRestoreQueues")
    P_CONFIG_KEY(preTileGeometriesFullKey, "PreTileGeometriesFull")
    P_CONFIG_KEY(preTileGeometriesKey, "PreTileGeometries")
    P_CONFIG_KEY(preFloatZoneAssignmentsKey, "PreFloatZoneAssignments")
    P_CONFIG_KEY(preFloatScreenAssignmentsKey, "PreFloatScreenAssignments")
    P_CONFIG_KEY(autotileWindowOrdersKey, "AutotileWindowOrders")
    P_CONFIG_KEY(autotilePendingRestoresKey, "AutotilePendingRestores")
    P_CONFIG_KEY(floatRestoreQueuesKey, "FloatRestoreQueues")

    // Obsolete keys (cleaned up on save to prevent stale data)
    P_CONFIG_KEY(obsoleteFloatingWindowsKey, "FloatingWindows")
    P_CONFIG_KEY(obsoletePendingWindowScreenAssignmentsKey, "PendingWindowScreenAssignments")
    P_CONFIG_KEY(obsoletePendingWindowDesktopAssignmentsKey, "PendingWindowDesktopAssignments")
    P_CONFIG_KEY(obsoletePendingWindowLayoutAssignmentsKey, "PendingWindowLayoutAssignments")
    P_CONFIG_KEY(obsoletePendingWindowZoneNumbersKey, "PendingWindowZoneNumbers")
    P_CONFIG_KEY(obsoleteWindowZoneAssignmentsKey, "WindowZoneAssignments")
    P_CONFIG_KEY(obsoleteWindowScreenAssignmentsKey, "WindowScreenAssignments")
    P_CONFIG_KEY(obsoleteWindowDesktopAssignmentsKey, "WindowDesktopAssignments")

    // ═══════════════════════════════════════════════════════════════════════════
    // Trigger JSON Field Names
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(triggerModifierField, "modifier")
    P_CONFIG_KEY(triggerMouseButtonField, "mouseButton")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Rendering
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(backendKey, "Backend")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping (top-level)
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(enabledKey, "Enabled")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Behavior
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(triggersKey, "Triggers")
    P_CONFIG_KEY(toggleActivationKey, "ToggleActivation")

    // Snapping.Behavior.ZoneSpan
    // (uses enabledKey, modifierKey, triggersKey and toggleActivationKey)

    // Snapping.Behavior.SnapAssist
    P_CONFIG_KEY(featureEnabledKey, "FeatureEnabled")
    // (also uses enabledKey and triggersKey)

    // Snapping.Behavior.Display
    P_CONFIG_KEY(showOnAllMonitorsKey, "ShowOnAllMonitors")
    P_CONFIG_KEY(filterByAspectRatioKey, "FilterByAspectRatio")

    // Snapping.Behavior.WindowHandling
    P_CONFIG_KEY(keepOnResolutionChangeKey, "KeepOnResolutionChange")
    P_CONFIG_KEY(moveNewToLastZoneKey, "MoveNewToLastZone")
    P_CONFIG_KEY(restoreOnUnsnapKey, "RestoreOnUnsnap")
    P_CONFIG_KEY(restoreOnLoginKey, "RestoreOnLogin")
    // Shared by Snapping.Behavior.WindowHandling + Tiling.Behavior — restore a
    // FLOATED (unsnapped / untiled) window to its previous position on reopen.
    P_CONFIG_KEY(restoreFloatedOnLoginKey, "RestoreFloatedOnLogin")
    P_CONFIG_KEY(unfloatFallbackToZoneKey, "UnfloatFallbackToZone")
    P_CONFIG_KEY(autoAssignAllLayoutsKey, "AutoAssignAllLayouts")
    P_CONFIG_KEY(stickyWindowHandlingKey, "StickyWindowHandling")
    P_CONFIG_KEY(defaultLayoutIdKey, "DefaultLayoutId")
    // Mode-neutral: suppresses the synthesized level-1 default for BOTH the
    // snapping and tiling engines (the default is a single mode-carrying
    // AssignmentEntry). Stored in this group alongside the other
    // default-assignment keys; surfaced mode-neutrally in the General UI page.
    P_CONFIG_KEY(suppressDefaultLayoutAssignmentKey, "SuppressDefaultLayoutAssignment")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Zones.Colors
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(useSystemKey, "UseSystem")
    P_CONFIG_KEY(highlightKey, "Highlight")
    P_CONFIG_KEY(inactiveKey, "Inactive")
    P_CONFIG_KEY(borderKey, "Border")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Zones.Opacity
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(activeKey, "Active")
    // (also uses inactiveKey)

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Zones.Border
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(widthKey, "Width")
    P_CONFIG_KEY(radiusKey, "Radius")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Zones.Labels
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(fontColorKey, "FontColor")
    P_CONFIG_KEY(fontFamilyKey, "FontFamily")
    P_CONFIG_KEY(fontSizeScaleKey, "FontSizeScale")
    P_CONFIG_KEY(fontWeightKey, "FontWeight")
    P_CONFIG_KEY(fontItalicKey, "FontItalic")
    P_CONFIG_KEY(fontUnderlineKey, "FontUnderline")
    P_CONFIG_KEY(fontStrikeoutKey, "FontStrikeout")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Effects
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(blurKey, "Blur")
    P_CONFIG_KEY(showNumbersKey, "ShowNumbers")
    P_CONFIG_KEY(flashOnSwitchKey, "FlashOnSwitch")
    P_CONFIG_KEY(osdOnLayoutSwitchKey, "OsdOnLayoutSwitch")
    P_CONFIG_KEY(osdOnDesktopSwitchKey, "OsdOnDesktopSwitch")
    P_CONFIG_KEY(navigationOsdKey, "NavigationOsd")
    P_CONFIG_KEY(osdStyleKey, "OsdStyle")
    P_CONFIG_KEY(overlayDisplayModeKey, "OverlayDisplayMode")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.ZoneSelector
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)
    P_CONFIG_KEY(triggerDistanceKey, "TriggerDistance")
    P_CONFIG_KEY(positionKey, "Position")
    P_CONFIG_KEY(layoutModeKey, "LayoutMode")
    P_CONFIG_KEY(sizeModeKey, "SizeMode")
    P_CONFIG_KEY(maxRowsKey, "MaxRows")
    P_CONFIG_KEY(previewWidthKey, "PreviewWidth")
    P_CONFIG_KEY(previewHeightKey, "PreviewHeight")
    P_CONFIG_KEY(previewLockAspectKey, "PreviewLockAspect")
    P_CONFIG_KEY(gridColumnsKey, "GridColumns")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Gaps
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(adjacentThresholdKey, "AdjacentThreshold")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling (top-level)
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Algorithm
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(defaultKey, "Default")
    P_CONFIG_KEY(splitRatioKey, "SplitRatio")
    P_CONFIG_KEY(splitRatioStepKey, "SplitRatioStep")
    P_CONFIG_KEY(masterCountKey, "MasterCount")
    P_CONFIG_KEY(maxWindowsKey, "MaxWindows")
    P_CONFIG_KEY(perAlgorithmSettingsKey, "PerAlgorithmSettings")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Behavior
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(insertPositionKey, "InsertPosition")
    P_CONFIG_KEY(focusNewWindowsKey, "FocusNewWindows")
    P_CONFIG_KEY(focusFollowsMouseKey, "FocusFollowsMouse")
    P_CONFIG_KEY(respectMinimumSizeKey, "RespectMinimumSize")
    // (also uses stickyWindowHandlingKey)
    P_CONFIG_KEY(dragBehaviorKey, "DragBehavior")
    P_CONFIG_KEY(overflowBehaviorKey, "OverflowBehavior")
    P_CONFIG_KEY(lockedScreensKey, "LockedScreens")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Tiling.Gaps
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(smartGapsKey, "SmartGaps")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Exclusions
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(transientWindowsKey, "TransientWindows")
    P_CONFIG_KEY(minimumWindowWidthKey, "MinimumWindowWidth")
    P_CONFIG_KEY(minimumWindowHeightKey, "MinimumWindowHeight")
    // `notificationsAndOsdKey` is consumed exclusively by the
    // Animations.WindowFiltering schema (no equivalent in the Exclusions
    // group), so it is declared with the rest of the animation keys below
    // rather than here. Note: the per-list `Applications` / `WindowClasses`
    // leaf-key accessors were retired with the v4 fold of exclusion lists
    // into Application-subject WindowRules — the migration reads from
    // `v3ExcludedApplicationsKey` / `v3ExcludedWindowClassesKey` below,
    // and no live config path remains.

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Performance
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(pollIntervalMsKey, "PollIntervalMs")
    P_CONFIG_KEY(minimumZoneSizePxKey, "MinimumZoneSizePx")
    P_CONFIG_KEY(minimumZoneDisplaySizePxKey, "MinimumZoneDisplaySizePx")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Shaders
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)
    P_CONFIG_KEY(frameRateKey, "FrameRate")
    P_CONFIG_KEY(audioVisualizerKey, "AudioVisualizer")
    P_CONFIG_KEY(audioSpectrumBarCountKey, "AudioSpectrumBarCount")

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
    P_CONFIG_KEY(animationProfileKey, "Profile")
    P_CONFIG_KEY(durationKey, "Duration")
    P_CONFIG_KEY(easingCurveKey, "EasingCurve")
    P_CONFIG_KEY(minDistanceKey, "MinDistance")
    P_CONFIG_KEY(sequenceModeKey, "SequenceMode")
    P_CONFIG_KEY(staggerIntervalKey, "StaggerInterval")
    // Animations.WindowFiltering knob — distinct from the snapping
    // `Exclusions` group above (which has no equivalent NotificationsAndOsd
    // axis). Consumed by `Settings::animationExcludeNotificationsAndOsd` and
    // the Animations.WindowFiltering schema in `settingsschema.cpp`.
    P_CONFIG_KEY(notificationsAndOsdKey, "NotificationsAndOsd")

    // Phase 6: ShaderProfileTree JSON blob — per-event shader effect
    // selection layered alongside the motion Profile (separate tree,
    // same dot-path namespace — see design doc decision AA).
    P_CONFIG_KEY(shaderProfileTreeKey, "ShaderProfileTree")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Shortcuts.Global
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(openEditorKey, "OpenEditor")
    P_CONFIG_KEY(openSettingsKey, "OpenSettings")
    P_CONFIG_KEY(previousLayoutKey, "PreviousLayout")
    P_CONFIG_KEY(nextLayoutKey, "NextLayout")

    // Parameterized — uses the pattern accessor to avoid duplication.
    // 1..9 mirrors quickLayoutN() in the enum surface; out-of-range
    // values would round-trip as e.g. "QuickLayout100" and ghost the
    // config namespace.
    P_CONFIG_KEY(quickLayoutKeyPattern, "QuickLayout%1")
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

    P_CONFIG_KEY(moveWindowLeftKey, "MoveWindowLeft")
    P_CONFIG_KEY(moveWindowRightKey, "MoveWindowRight")
    P_CONFIG_KEY(moveWindowUpKey, "MoveWindowUp")
    P_CONFIG_KEY(moveWindowDownKey, "MoveWindowDown")
    P_CONFIG_KEY(focusZoneLeftKey, "FocusZoneLeft")
    P_CONFIG_KEY(focusZoneRightKey, "FocusZoneRight")
    P_CONFIG_KEY(focusZoneUpKey, "FocusZoneUp")
    P_CONFIG_KEY(focusZoneDownKey, "FocusZoneDown")
    P_CONFIG_KEY(pushToEmptyZoneKey, "PushToEmptyZone")
    P_CONFIG_KEY(restoreWindowSizeKey, "RestoreWindowSize")
    P_CONFIG_KEY(toggleWindowFloatKey, "ToggleWindowFloat")
    P_CONFIG_KEY(swapWindowLeftKey, "SwapWindowLeft")
    P_CONFIG_KEY(swapWindowRightKey, "SwapWindowRight")
    P_CONFIG_KEY(swapWindowUpKey, "SwapWindowUp")
    P_CONFIG_KEY(swapWindowDownKey, "SwapWindowDown")

    // Parameterized — uses the pattern accessor to avoid duplication.
    // 1..9 mirrors snapToZoneN() in the enum surface.
    P_CONFIG_KEY(snapToZoneKeyPattern, "SnapToZone%1")
    static QString snapToZoneKey(int n)
    {
        // See quickLayoutKey above for the rationale on the qFatal guard.
        if (n < 1 || n > 9) {
            qFatal("snapToZoneKey: n out of range: %d", n);
        }
        return snapToZoneKeyPattern().arg(n);
    }

    P_CONFIG_KEY(rotateWindowsClockwiseKey, "RotateWindowsClockwise")
    P_CONFIG_KEY(rotateWindowsCounterclockwiseKey, "RotateWindowsCounterclockwise")
    P_CONFIG_KEY(cycleWindowForwardKey, "CycleWindowForward")
    P_CONFIG_KEY(cycleWindowBackwardKey, "CycleWindowBackward")
    P_CONFIG_KEY(resnapToNewLayoutKey, "ResnapToNewLayout")
    P_CONFIG_KEY(snapAllWindowsKey, "SnapAllWindows")
    P_CONFIG_KEY(layoutPickerKey, "LayoutPicker")
    P_CONFIG_KEY(toggleLayoutLockKey, "ToggleLayoutLock")
    P_CONFIG_KEY(swapVirtualScreenLeftKey, "SwapVirtualScreenLeft")
    P_CONFIG_KEY(swapVirtualScreenRightKey, "SwapVirtualScreenRight")
    P_CONFIG_KEY(swapVirtualScreenUpKey, "SwapVirtualScreenUp")
    P_CONFIG_KEY(swapVirtualScreenDownKey, "SwapVirtualScreenDown")
    P_CONFIG_KEY(rotateVirtualScreensClockwiseKey, "RotateVirtualScreensClockwise")
    P_CONFIG_KEY(rotateVirtualScreensCounterclockwiseKey, "RotateVirtualScreensCounterclockwise")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Shortcuts.Tiling
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(toggleKey, "Toggle")
    P_CONFIG_KEY(focusMasterKey, "FocusMaster")
    P_CONFIG_KEY(swapMasterKey, "SwapMaster")
    P_CONFIG_KEY(incMasterRatioKey, "IncMasterRatio")
    P_CONFIG_KEY(decMasterRatioKey, "DecMasterRatio")
    P_CONFIG_KEY(incMasterCountKey, "IncMasterCount")
    P_CONFIG_KEY(decMasterCountKey, "DecMasterCount")
    P_CONFIG_KEY(retileKey, "Retile")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Editor.Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(duplicateKey, "Duplicate")
    P_CONFIG_KEY(splitHorizontalKey, "SplitHorizontal")
    P_CONFIG_KEY(splitVerticalKey, "SplitVertical")
    P_CONFIG_KEY(fillKey, "Fill")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Editor.Snapping
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(gridEnabledKey, "GridEnabled")
    P_CONFIG_KEY(edgeEnabledKey, "EdgeEnabled")
    P_CONFIG_KEY(intervalKey, "Interval")
    P_CONFIG_KEY(intervalXKey, "IntervalX")
    P_CONFIG_KEY(intervalYKey, "IntervalY")
    P_CONFIG_KEY(overrideModifierKey, "OverrideModifier")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Editor.FillOnDrop
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)
    P_CONFIG_KEY(modifierKey, "Modifier")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Virtual Screens
    // The "VirtualScreen:" prefix is intentional — parsing must use
    // groupName.mid(prefix.size()) to extract the physical screen ID,
    // NOT split on ':',  because screen IDs themselves may contain colons
    // (e.g. "BNQ:BenQ PD3220U:serial").
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(virtualScreenGroupPrefix, "VirtualScreen:")
    P_CONFIG_KEY(virtualScreenCountKey, "count")
    P_CONFIG_KEY(virtualScreenXKey, "x")
    P_CONFIG_KEY(virtualScreenYKey, "y")
    P_CONFIG_KEY(virtualScreenWidthKey, "width")
    P_CONFIG_KEY(virtualScreenHeightKey, "height")
    P_CONFIG_KEY(virtualScreenNameKey, "name")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Ordering
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(snappingLayoutOrderKey, "SnappingLayoutOrder")
    P_CONFIG_KEY(tilingAlgorithmOrderKey, "TilingAlgorithmOrder")

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-Screen Config Group Prefixes
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_GROUP(zoneSelectorGroupPrefix, P_PER_SCREEN_PREFIX_ZONE_SELECTOR ":")
    P_CONFIG_GROUP(autotileScreenGroupPrefix, P_PER_SCREEN_PREFIX_AUTOTILE ":")

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
        P_CONFIG_GROUP(v1ActivationGroup, "Activation")
        P_CONFIG_GROUP(v1DisplayGroup, "Display")
        P_CONFIG_GROUP(v1AppearanceGroup, "Appearance")
        P_CONFIG_GROUP(v1ZonesGroup, "Zones")
        P_CONFIG_GROUP(v1BehaviorGroup, "Behavior")
        P_CONFIG_GROUP(v1ExclusionsGroup, "Exclusions") // = v2 exclusionsGroup
        P_CONFIG_GROUP(v1ZoneSelectorGroup, "ZoneSelector")
        P_CONFIG_GROUP(v1AutotilingGroup, "Autotiling")
        P_CONFIG_GROUP(v1AutotileShortcutsGroup, "AutotileShortcuts")
        P_CONFIG_GROUP(v1AnimationsGroup, "Animations") // = v2 animationsGroup
        // v1 animation per-field keys — Phase-4 migration packs these into the
        // v2 `Profile` JSON blob (see configmigration.cpp::migrateV1ToV2).
        // The accessors exist solely so migration code is unambiguous about
        // "reading legacy field" vs "reading new blob"; production reads after
        // migration go through `Profile::JsonField*` constants.
        P_CONFIG_KEY(v1AnimationsEnabledKey, "AnimationsEnabled")
        P_CONFIG_KEY(v1AnimationDurationKey, "AnimationDuration")
        P_CONFIG_KEY(v1AnimationEasingCurveKey, "AnimationEasingCurve")
        P_CONFIG_KEY(v1AnimationMinDistanceKey, "AnimationMinDistance")
        P_CONFIG_KEY(v1AnimationSequenceModeKey, "AnimationSequenceMode")
        P_CONFIG_KEY(v1AnimationStaggerIntervalKey, "AnimationStaggerInterval")
        /// v1 INI key for the rendering backend selection — both the v1→v2 migration
        /// step and the v1 INI dispatcher consume this through one accessor so a
        /// future rename of the literal can't drift one site behind the other.
        P_CONFIG_KEY(v1RenderingBackendKey, "RenderingBackend")
        P_CONFIG_GROUP(v1GlobalShortcutsGroup, "GlobalShortcuts")
        P_CONFIG_GROUP(v1EditorGroup, "Editor") // = v2 editorGroup
        P_CONFIG_GROUP(v1OrderingGroup, "Ordering") // = v2 orderingGroup
        P_CONFIG_GROUP(v1RenderingGroup, "Rendering") // = v2 renderingGroup
        P_CONFIG_GROUP(v1ShadersGroup, "Shaders") // = v2 shadersGroup
        // v1 WindowTracking group — only read in the v1→v2 step where it's
        // moved out to session.json. The live runtime accessor
        // `ConfigKeys::windowTrackingGroup()` happens to return the same
        // "WindowTracking" string today, but a future rename of the live
        // accessor must not silently retarget this read at a path no v1
        // INI ever held — that would drop user session state.
        P_CONFIG_GROUP(v1WindowTrackingGroup, "WindowTracking")

        // v2 legacy keys — used ONLY by migrateV2ToV3.
        // The v2 group itself (Snapping.Behavior.Display) lives on past v3 — it
        // still holds ShowOnAllMonitors and FilterByAspectRatio. Only the three
        // disabled-* keys move out, so we name the keys with a v2 prefix.
        // The migration uses the frozen `v2SnappingBehaviorDisplayGroup` group
        // accessor to descend the JSON tree — a future rename of the LIVE
        // `snappingBehaviorDisplayGroup()` accessor must NOT silently retarget
        // the v2→v3 step to a path no v2 config ever had on disk (the same
        // freeze-policy hazard the v3→v4 step avoids by using `v3DisplayGroup`).
        P_CONFIG_GROUP(v2SnappingBehaviorDisplayGroup, "Snapping.Behavior.Display")
        P_CONFIG_KEY(v2DisabledMonitorsKey, "DisabledMonitors")
        P_CONFIG_KEY(v2DisabledDesktopsKey, "DisabledDesktops")
        P_CONFIG_KEY(v2DisabledActivitiesKey, "DisabledActivities")

        // v2 destination group names — used as both v1→v2 destinations
        // (in `migrateV1ToV2`) and v2 source coordinates. The frozen
        // accessors mirror the v3→v4 step's `v3DisplayGroup` pattern
        // and ensure a future rename of the matching live
        // `snappingGroup()` / `tilingGroup()` / etc. accessor does not
        // silently retarget the migration to a path no v2 config ever
        // had on disk.
        P_CONFIG_GROUP(v2SnappingGroup, "Snapping")
        P_CONFIG_GROUP(v2TilingGroup, "Tiling")
        P_CONFIG_GROUP(v2PerformanceGroup, "Performance")
        P_CONFIG_GROUP(v2ExclusionsGroup, "Exclusions")
        P_CONFIG_GROUP(v2RenderingGroup, "Rendering")
        P_CONFIG_GROUP(v2ShadersGroup, "Shaders")
        P_CONFIG_GROUP(v2ShortcutsGroup, "Shortcuts")
        P_CONFIG_GROUP(v2EditorGroup, "Editor")
        P_CONFIG_GROUP(v2OrderingGroup, "Ordering")
        // Parameterised v2 destinations: v1→v2 renames v1's
        // `QuickLayout%1Shortcut` to v2's `QuickLayout%1` and preserves
        // v1's `SnapToZone%1` verbatim. Frozen accessors pin the v2
        // wire-format names so a future rename of the matching live
        // pattern accessors stays isolated from migration.
        P_CONFIG_KEY(v1QuickLayoutShortcutKeyPattern, "QuickLayout%1Shortcut")
        P_CONFIG_KEY(v2QuickLayoutKeyPattern, "QuickLayout%1")
        P_CONFIG_KEY(v2SnapToZoneKeyPattern, "SnapToZone%1")
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
        P_CONFIG_KEY(v3snappingDisabledMonitorsKey, "SnappingDisabledMonitors")
        P_CONFIG_KEY(v3autotileDisabledMonitorsKey, "AutotileDisabledMonitors")
        P_CONFIG_KEY(v3snappingDisabledDesktopsKey, "SnappingDisabledDesktops")
        P_CONFIG_KEY(v3autotileDisabledDesktopsKey, "AutotileDisabledDesktops")
        P_CONFIG_KEY(v3snappingDisabledActivitiesKey, "SnappingDisabledActivities")
        P_CONFIG_KEY(v3autotileDisabledActivitiesKey, "AutotileDisabledActivities")
        P_CONFIG_GROUP(v3assignmentGroupPrefix, "Assignment:")
        P_CONFIG_GROUP(v3quickLayoutsGroup, "QuickLayouts")
        P_CONFIG_GROUP(v3modeTrackingGroup, "ModeTracking")

        // v3 zone-overlay groups — renamed to Snapping.Zones.* by
        // migrateV3ToV4; frozen OLD paths the migration reads from.
        P_CONFIG_GROUP(v3SnappingAppearanceColorsGroup, "Snapping.Appearance.Colors")
        P_CONFIG_GROUP(v3SnappingAppearanceOpacityGroup, "Snapping.Appearance.Opacity")
        P_CONFIG_GROUP(v3SnappingAppearanceBorderGroup, "Snapping.Appearance.Border")
        P_CONFIG_GROUP(v3SnappingAppearanceLabelsGroup, "Snapping.Appearance.Labels")

        // v4 zone-overlay destination paths — frozen NEW paths migrateV3ToV4
        // writes to. Frozen (not the live ConfigDefaults::snappingZones*Group()
        // accessors) so a future rename of those live accessors can't silently
        // retarget this historical migration step to a path no migrated config
        // ever had on disk — same freeze policy as the v2→v3 step's write site.
        P_CONFIG_GROUP(v4SnappingZonesColorsGroup, "Snapping.Zones.Colors")
        P_CONFIG_GROUP(v4SnappingZonesOpacityGroup, "Snapping.Zones.Opacity")
        P_CONFIG_GROUP(v4SnappingZonesBorderGroup, "Snapping.Zones.Border")
        P_CONFIG_GROUP(v4SnappingZonesLabelsGroup, "Snapping.Zones.Labels")

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
        P_CONFIG_GROUP(v4AnimationsGroup, "Animations")
        P_CONFIG_KEY(v4AnimationAppRulesKey, "AnimationAppRules")

        // v4 migration scratch-root keys — set on the root by `migrateV3ToV4`
        // and consumed by `finalizeV4Conversion`. `Settings::purgeStaleKeys`
        // (src/config/settings.cpp) ALSO references these to preserve them
        // across save() cycles when the chain stalls (see the stalled-chain
        // gate in `finalizeV4Conversion`); routing both call sites through
        // the same frozen accessor stops a future rename in one file from
        // silently breaking the protection in the other.
        P_CONFIG_KEY(v4DisableStashKey, "_v4DisableStash")
        P_CONFIG_KEY(v4AnimationRulesStashKey, "_v4AnimationRulesStash")
        // Third v4 scratch-root key — set on the root by `migrateV3ToV4` from
        // the legacy `Exclusions.{Applications,WindowClasses}` lists and
        // consumed by `finalizeV4Conversion`, which converts each surviving
        // pattern into an Application-subject `AppId AppIdMatches <pattern>
        // Exclude` WindowRule. Same purge-protection semantics as the two
        // sibling stash keys above.
        P_CONFIG_KEY(v4ExclusionStashKey, "_v4ExclusionStash")
        // Fourth v4 scratch-root key — set on the root by `migrateV3ToV4`
        // from the legacy `Animations.WindowFiltering.{Applications,WindowClasses}`
        // lists and consumed by `finalizeV4Conversion`, which converts each
        // surviving pattern into a `DesktopFile`/`WindowClass Contains
        // <pattern> → ExcludeAnimations` WindowRule (preserving the
        // legacy effect-bridge match-field split). Same purge-protection
        // semantics as the three sibling stash keys above.
        P_CONFIG_KEY(v4AnimationExclusionStashKey, "_v4AnimationExclusionStash")

        // v5 migration scratch-root key — set on the root by `migrateV4ToV5`
        // from the deleted per-mode appearance / gap groups (and per-screen
        // gap subsets) and consumed by `finalizeV5Conversion`, which converts
        // each differing value into a non-managed override WindowRule. Same
        // purge-protection semantics as the v4 sibling stash keys above: it is
        // listed in `Settings::purgeStaleKeys`' preserved set so a save() cycle
        // can't drop it while the conversion is still pending.
        P_CONFIG_KEY(v5AppearanceStashKey, "_v5AppearanceStash")

        // v3 frozen group/key accessors — used ONLY by migrateV3ToV4 and
        // finalizeV4Conversion. These mirror the live `displayGroup`,
        // `defaultLayoutIdKey`, `snappingBehaviorWindowHandlingGroup`,
        // `tilingAlgorithmGroup`, and `defaultKey` accessors but are frozen
        // at their v3 literal so a future runtime rename cannot silently
        // retarget the migration to a path no v3 config ever had on disk.
        P_CONFIG_GROUP(v3DisplayGroup, "Display")
        P_CONFIG_KEY(v3DefaultLayoutIdKey, "DefaultLayoutId")
        P_CONFIG_GROUP(v3SnappingBehaviorWindowHandlingGroup, "Snapping.Behavior.WindowHandling")
        P_CONFIG_GROUP(v3TilingAlgorithmGroup, "Tiling.Algorithm")
        P_CONFIG_KEY(v3DefaultKey, "Default")

        // v3 Exclusions group + comma-joined pattern keys — frozen at their
        // v3 literal for the same reason the disable-list group/keys above
        // are: migrateV3ToV4 reads them from a v3 config on disk, and a
        // future runtime rename of the live `exclusionsGroup` accessor must
        // NOT silently retarget the migration to a path no v3 config ever
        // had on disk. (The per-list `Applications` / `WindowClasses` leaf
        // accessors were retired with the v4 fold — no live accessor exists
        // to drift from now, but the v3 literals stay pinned here.)
        P_CONFIG_GROUP(v3ExclusionsGroup, "Exclusions")
        P_CONFIG_KEY(v3ExcludedApplicationsKey, "Applications")
        P_CONFIG_KEY(v3ExcludedWindowClassesKey, "WindowClasses")
        // The animation exclusion lists live at
        // `Animations.WindowFiltering.{Applications,WindowClasses}` — same
        // leaf keys as the snapping Exclusions group above, just under a
        // different dot-path. The "Animations" segment routes through
        // `v4AnimationsGroup` above; "WindowFiltering" is the bare leaf
        // segment frozen here. The live `animationsWindowFilteringGroup()`
        // accessor returns the FULL dot-path "Animations.WindowFiltering"
        // (not the bare segment), so it can't be reused by the migration
        // which walks the path one segment at a time. Freezing the segment
        // here keeps the migration's read-path symmetric with the other
        // Legacy:: accessors and gives a future rename a single chokepoint.
        P_CONFIG_KEY(v4WindowFilteringSegment, "WindowFiltering")

        // v3 assignments.json field names — frozen literals from the dead
        // v3 assignments.json schema. finalizeV4Conversion is the sole
        // remaining reader; these are NOT live config keys.
        P_CONFIG_KEY(v3AssignmentMode, "Mode")
        P_CONFIG_KEY(v3AssignmentLayout, "SnappingLayout")
        P_CONFIG_KEY(v3AssignmentAlgorithm, "TilingAlgorithm")
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
    P_CONFIG_KEY(settingsAppWindowXKey, "x")
    P_CONFIG_KEY(settingsAppWindowYKey, "y")
    P_CONFIG_KEY(settingsAppWindowWidthKey, "width")
    P_CONFIG_KEY(settingsAppWindowHeightKey, "height")
    P_CONFIG_KEY(settingsAppDismissedUpdateVersionKey, "dismissedUpdateVersion")
    P_CONFIG_KEY(settingsAppLastSeenWhatsNewVersionKey, "lastSeenWhatsNewVersion")

    // ═══════════════════════════════════════════════════════════════════════════
    // Filesystem paths under XDG_DATA_HOME
    //
    // Daemon, settings app, and editor all read/write the same per-user
    // layouts and algorithms directories. Hoisted into one accessor each so
    // a rename only touches one site.
    // ═══════════════════════════════════════════════════════════════════════════
    P_CONFIG_KEY(userDataSubdir, "plasmazones")
    P_CONFIG_KEY(layoutsSubdir, "plasmazones/layouts")
    P_CONFIG_KEY(algorithmsSubdir, "plasmazones/algorithms")

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

#undef P_CONFIG_KEY
#undef P_CONFIG_GROUP
// P_PER_SCREEN_PREFIX_* deliberately NOT undef'd: perscreenresolver.cpp
// consumes them after including this header (the single-definition-point
// contract above). Do not "clean up" by undef'ing them here.
