// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorProtocol/ServiceConstants.h>
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
// All five are rows in PerScreenPathResolver's prefix→category mapping table,
// which is what makes their groups resolve to a nested path under the
// "PerScreen" container instead of falling back to a dot-path orphan at the
// JSON root. Four of the five also carry a *GroupPrefix accessor below that
// appends the ':' (zoneSelectorGroupPrefix, autotileScreenGroupPrefix,
// scrollingScreenGroupPrefix, scrollingZoneSelectorGroupPrefix); the
// snapping prefix has none, because
// per-monitor snapping state is unified into the autotile store and nothing
// looks its group up by name — it stays in the resolver table so an older
// build's leftover groups still resolve and can be swept. Defining the
// spellings here keeps whichever consumers exist in lockstep instead of
// silently desyncing the JSON path resolver from the group accessors.
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
#define P_PER_SCREEN_PREFIX_SCROLLING "ScrollingScreen"
#define P_PER_SCREEN_PREFIX_SCROLLING_ZONE_SELECTOR "ScrollingZoneSelector"

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
    P_CONFIG_GROUP(shadersAudioGroup, "Shaders.Audio")
    P_CONFIG_GROUP(animationsGroup, "Animations")
    P_CONFIG_GROUP(shortcutsGlobalGroup, "Shortcuts.Global")
    P_CONFIG_GROUP(shortcutsTilingGroup, "Shortcuts.Tiling")
    P_CONFIG_GROUP(shortcutsScrollingGroup, "Shortcuts.Scrolling")
    P_CONFIG_GROUP(orderingGroup, "Ordering")
    P_CONFIG_GROUP(updatesGroup, "Updates")

    // Window decoration appearance (tiled/snapped window border + title bar).
    // Mode-neutral top-level group — the values apply to both the snapping and
    // tiling engines, so it sits outside Snapping.* / Tiling.*.
    P_CONFIG_GROUP(windowsAppearanceGroup, "Windows")

    // Shared inner/outer gap model used by BOTH snapping and tiling. Mode-neutral
    // top-level group; the gap values are read by both engines.
    P_CONFIG_GROUP(gapsGroup, "Gaps")

    // Workspaces — dynamic per-monitor workspaces (mode-neutral top-level
    // group: the feature is a layer below all three placement modes).
    P_CONFIG_GROUP(workspacesBehaviorGroup, "Workspaces.Behavior")
    P_CONFIG_GROUP(workspacesNamedGroup, "Workspaces.Named")

    // Snapping sub-groups
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
    // shared inner/outer gap values live in the top-level Gaps group (gapsGroup)
    // and are read through Settings' gap getters.
    P_CONFIG_GROUP(snappingGapsGroup, "Snapping.Gaps")

    // Display (mode-neutral) — per-mode disable lists. Lives outside Snapping.*
    // because the values gate the whole product (snap + autotile), not just
    // snapping. v3 schema; in v2 these were under Snapping.Behavior.Display.
    P_CONFIG_GROUP(displayGroup, "Display")

    // Animations sub-groups
    P_CONFIG_GROUP(animationsWindowFilteringGroup, "Animations.WindowFiltering")

    // Decorations sub-groups — window filtering for the KWin effect's border /
    // decoration pass. Independent of the Animations and Exclusions groups so a
    // user can tune which windows get a border separately from which windows
    // snap or animate. Reuses the shared leaf keys (transientWindowsKey,
    // minimumWindowWidthKey, minimumWindowHeightKey); only the group differs.
    P_CONFIG_GROUP(decorationsWindowFilteringGroup, "Decorations.WindowFiltering")

    // Tiling sub-groups
    P_CONFIG_GROUP(tilingAlgorithmGroup, "Tiling.Algorithm")
    P_CONFIG_GROUP(tilingBehaviorGroup, "Tiling.Behavior")
    P_CONFIG_GROUP(tilingGapsGroup, "Tiling.Gaps")
    P_CONFIG_GROUP(scrollingGroup, "Scrolling")
    P_CONFIG_GROUP(scrollingBehaviorGroup, "Scrolling.Behavior")
    // Scrolling.Wheel.* — the two wheel chords ("scroll keys"): which chord
    // moves column FOCUS along the strip, and which pans the VIEW without
    // changing focus. One group each, carrying the shared Triggers leaf, so
    // each reads like the other trigger-bearing nodes in the tree.
    P_CONFIG_GROUP(scrollingWheelFocusGroup, "Scrolling.Wheel.Focus")
    P_CONFIG_GROUP(scrollingWheelViewGroup, "Scrolling.Wheel.View")
    // Scrolling.Behavior.DragScroll — edge auto-scroll during a drag
    // re-insert (niri's dnd-edge-view-scroll). Its own subtree rather than
    // four DragScroll*-prefixed leaves on Scrolling.Behavior, so the card,
    // the per-page reset manifest and the search catalog address one node.
    P_CONFIG_GROUP(scrollingDragScrollGroup, "Scrolling.Behavior.DragScroll")
    // Scrolling.TabIndicator — the whole appearance and placement family for
    // the indicator drawn alongside a tabbed column. Its own group rather than
    // a dozen Tab*-prefixed leaves on Scrolling, so the settings page, the
    // per-page reset manifest and the rule slots all address one subtree.
    P_CONFIG_GROUP(scrollingTabIndicatorGroup, "Scrolling.TabIndicator")
    // Scrolling.DropIndicator — the drop-target highlight painted during a
    // drag re-insert. Its own group for the same reason as TabIndicator: the
    // settings page, the per-page reset manifest and any later rule slots all
    // address one subtree.
    P_CONFIG_GROUP(scrollingDropIndicatorGroup, "Scrolling.DropIndicator")
    // Scrolling.ZoneSelector — the strip-mode drag popup on scrolling
    // screens. Mirrors Snapping.ZoneSelector minus the grid-arrangement
    // keys (LayoutMode / GridColumns / MaxRows): the strip popup is a
    // single card row along the strip by construction.
    P_CONFIG_GROUP(scrollingZoneSelectorGroup, "Scrolling.ZoneSelector")

    // Decorations — per-surface decoration tree (DecorationProfileTree:
    // shader-pack chain + per-pack parameters, keyed on a dot-path surface
    // namespace; any border appearance rides as the border pack's parameters).
    // The DecorationProfileTree blob is a leaf key directly under this group,
    // mirroring how the animation ShaderProfileTree sits under Animations; the
    // Decorations.WindowFiltering sub-group is the border-pass window filter.
    P_CONFIG_GROUP(decorationsGroup, "Decorations")

    // Decorations.Performance — what the decoration chain is allowed to keep
    // redrawing. An animated pack (a drifting mote layer, an orbiting gleam)
    // repaints every window carrying it on every vsync, which never lets the GPU
    // leave its top performance state: measured at ~110 W and +12 C over an idle
    // desktop with the effect unloaded, on hardware that is only ~45% busy. The
    // cost is not the work per frame, it is that there IS work every frame — so
    // most knobs here bound WHEN the chain animates. The group also carries one
    // per-frame-cost knob, BlurScaleMultiplier, which scales how much blur work
    // each frame does rather than gating when frames happen.
    P_CONFIG_GROUP(decorationsPerformanceGroup, "Decorations.Performance")

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

    // Snap mode — last used zone info. Only the zone id is persisted (see
    // windowtrackingadaptor/saveload.cpp); the companion screen / class /
    // desktop keys had no reader and no writer left and are gone.
    P_CONFIG_KEY(lastUsedZoneIdKey, "LastUsedZoneId")

    // User-snapped classes
    P_CONFIG_KEY(userSnappedClassesKey, "UserSnappedClasses")

    // Unified, engine-agnostic per-window placement record (WindowPlacementStore) —
    // the SOLE persisted per-window restore key for both snap and autotile.
    P_CONFIG_KEY(windowPlacementsKey, "WindowPlacements")

    // Scrolling strip-structure snapshots (per-context column groupings,
    // tabbed flags, sizes, focus, view anchor) — ScrollEngine::serializeStripState
    // blob, restored into the engine's arrival-restore stash on load.
    P_CONFIG_KEY(scrollStripsKey, "ScrollStrips")

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
    P_CONFIG_KEY(gpuKey, "Gpu")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping (top-level)
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(enabledKey, "Enabled")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Workspaces.Behavior
    // ═══════════════════════════════════════════════════════════════════════════

    // Consent latch for writing KWin's PerOutputVirtualDesktops kwinrc key on
    // the user's behalf (dynamic workspaces enable flow).
    P_CONFIG_KEY(manageKWinPerOutputKey, "ManageKWinPerOutputSetting")
    // Owner-wins snap-back OSD hint toggle.
    P_CONFIG_KEY(snapBackOsdHintKey, "SnapBackOsdHint")
    // Take over KWin's stock desktop-switch chords while the feature is on.
    P_CONFIG_KEY(rebindKWinShortcutsKey, "RebindKWinDesktopShortcuts")
    // Named-workspace declarations (Workspaces.Named): JSON array of
    // {name, output, position, focusShortcut, moveShortcut} maps.
    P_CONFIG_KEY(entriesKey, "Entries")

    // Indexed workspace quick-shortcut slots 1..9 (Shortcuts.Global leaves;
    // defaults unset). Same builder shape and range contract as quickLayoutKey.
    P_CONFIG_KEY(workspaceMoveSlotKeyPattern, "WorkspaceMoveSlot%1")
    static QString workspaceMoveSlotKey(int n)
    {
        if (n < 1 || n > 9) {
            qFatal("workspaceMoveSlotKey: n out of range: %d", n);
        }
        return workspaceMoveSlotKeyPattern().arg(n);
    }

    // Workspace verb shortcuts (Shortcuts.Global leaves).
    P_CONFIG_KEY(workspaceFocusUpKey, "WorkspaceFocusUp")
    P_CONFIG_KEY(workspaceFocusDownKey, "WorkspaceFocusDown")
    P_CONFIG_KEY(workspaceMoveWindowUpKey, "WorkspaceMoveWindowUp")
    P_CONFIG_KEY(workspaceMoveWindowDownKey, "WorkspaceMoveWindowDown")
    P_CONFIG_KEY(workspaceMoveColumnUpKey, "WorkspaceMoveColumnUp")
    P_CONFIG_KEY(workspaceMoveColumnDownKey, "WorkspaceMoveColumnDown")
    P_CONFIG_KEY(workspaceReorderUpKey, "WorkspaceReorderUp")
    P_CONFIG_KEY(workspaceReorderDownKey, "WorkspaceReorderDown")
    P_CONFIG_KEY(workspaceMoveToMonitorLeftKey, "WorkspaceMoveToMonitorLeft")
    P_CONFIG_KEY(workspaceMoveToMonitorRightKey, "WorkspaceMoveToMonitorRight")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.Behavior
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(triggersKey, "Triggers")
    P_CONFIG_KEY(toggleActivationKey, "ToggleActivation")
    // Hold-mode release grace in ms; generic like toggleActivationKey and
    // shared by Snapping.Behavior, Snapping.Behavior.ZoneSpan, Tiling.Behavior
    // and Scrolling.Behavior.
    P_CONFIG_KEY(releaseGraceMsKey, "ReleaseGraceMs")

    // Snapping.Behavior.ZoneSpan
    // (uses enabledKey, modifierKey, triggersKey, toggleActivationKey and
    //  releaseGraceMsKey)

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
    // Shared by Snapping.Behavior.WindowHandling, Tiling.Behavior and
    // Scrolling.Behavior — restore a FLOATED (unsnapped / untiled) window to its
    // previous position on reopen.
    P_CONFIG_KEY(restoreFloatedOnLoginKey, "RestoreFloatedOnLogin")
    P_CONFIG_KEY(unfloatFallbackToZoneKey, "UnfloatFallbackToZone")
    // Shared by Snapping.Behavior.WindowHandling, Tiling.Behavior and
    // Scrolling.Behavior: stack the mode's floated windows above the windows it
    // places (keep-above, applied by the KWin effect beneath any SetWindowLayer rule).
    P_CONFIG_KEY(keepFloatingAboveKey, "KeepFloatingAbove")
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

    P_CONFIG_KEY(showNumbersKey, "ShowNumbers")
    P_CONFIG_KEY(flashOnSwitchKey, "FlashOnSwitch")
    P_CONFIG_KEY(osdOnLayoutSwitchKey, "OsdOnLayoutSwitch")
    P_CONFIG_KEY(osdOnDesktopSwitchKey, "OsdOnDesktopSwitch")
    P_CONFIG_KEY(navigationOsdKey, "NavigationOsd")
    P_CONFIG_KEY(osdStyleKey, "OsdStyle")
    P_CONFIG_KEY(overlayDisplayModeKey, "OverlayDisplayMode")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Snapping.ZoneSelector / Scrolling.ZoneSelector
    // ═══════════════════════════════════════════════════════════════════════════

    // SHARED leaves: Scrolling.ZoneSelector (the strip selector) reuses
    // enabledKey, triggerDistanceKey, positionKey, sizeModeKey,
    // previewWidthKey, previewHeightKey and previewLockAspectKey from this
    // block, disambiguated by group — renaming one here renames it for BOTH
    // families. Only layoutModeKey / maxRowsKey / gridColumnsKey are
    // snapping-only (the strip popup has no grid arrangement).
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
    // Config Keys — Windows (window decoration appearance)
    //
    // Border width / radius REUSE the generic widthKey() / radiusKey() accessors
    // above (the windowsAppearanceGroup context disambiguates them from the
    // Snapping.Zones.Border keys of the same spelling).
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(showBorderKey, "ShowBorder")
    P_CONFIG_KEY(borderScopeKey, "BorderScope")
    P_CONFIG_KEY(hideTitleBarsKey, "HideTitleBars")
    P_CONFIG_KEY(titleBarScopeKey, "TitleBarScope")
    P_CONFIG_KEY(borderColorActiveKey, "BorderColorActive")
    P_CONFIG_KEY(borderColorInactiveKey, "BorderColorInactive")
    P_CONFIG_KEY(focusFadeDurationKey, "FocusFadeDuration")
    // Plain opacity+tint layer (rendered by the built-in "opacity-tint"
    // surface pack), the opacity analogue of ShowBorder/BorderScope above.
    P_CONFIG_KEY(showOpacityTintKey, "ShowOpacityTint")
    P_CONFIG_KEY(opacityTintScopeKey, "OpacityTintScope")
    P_CONFIG_KEY(opacityKey, "Opacity")
    P_CONFIG_KEY(tintStrengthKey, "TintStrength")
    P_CONFIG_KEY(tintColorKey, "TintColor")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Gaps (shared inner/outer gap model)
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(innerGapKey, "Inner")
    P_CONFIG_KEY(outerGapKey, "Outer")
    P_CONFIG_KEY(usePerSideOuterGapKey, "UsePerSide")
    P_CONFIG_KEY(outerGapTopKey, "Top")
    P_CONFIG_KEY(outerGapBottomKey, "Bottom")
    P_CONFIG_KEY(outerGapLeftKey, "Left")
    P_CONFIG_KEY(outerGapRightKey, "Right")

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
    // Config Keys — Scrolling
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(centerFocusedColumnKey, "CenterFocusedColumn")
    P_CONFIG_KEY(stripAxisKey, "StripAxis")
    P_CONFIG_KEY(alwaysCenterSingleColumnKey, "AlwaysCenterSingleColumn")
    P_CONFIG_KEY(cropStraddlersKey, "CropStraddlers")
    P_CONFIG_KEY(defaultColumnWidthKindKey, "DefaultColumnWidthKind")
    P_CONFIG_KEY(defaultColumnWidthValueKey, "DefaultColumnWidthValue")
    P_CONFIG_KEY(defaultColumnDisplayKey, "DefaultColumnDisplay")
    P_CONFIG_KEY(presetColumnWidthsKey, "PresetColumnWidths")
    P_CONFIG_KEY(presetWindowHeightsKey, "PresetWindowHeights")
    P_CONFIG_KEY(defaultColumnWidthPresetIndexKey, "DefaultColumnWidthPresetIndex")
    P_CONFIG_KEY(defaultWindowHeightKindKey, "DefaultWindowHeightKind")
    P_CONFIG_KEY(defaultWindowHeightValueKey, "DefaultWindowHeightValue")
    P_CONFIG_KEY(defaultWindowHeightPresetIndexKey, "DefaultWindowHeightPresetIndex")
    P_CONFIG_KEY(defaultTemplateKey, "DefaultTemplate")
    P_CONFIG_KEY(wheelFocusEnabledKey, "WheelFocusEnabled")
    P_CONFIG_KEY(wheelFocusInvertedKey, "WheelFocusInverted")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Scrolling.TabIndicator
    // (also uses enabledKey, widthKey, positionKey — shared leaf names,
    // disambiguated by group)
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(styleKey, "Style")
    P_CONFIG_KEY(hideWhenSingleTabKey, "HideWhenSingleTab")
    P_CONFIG_KEY(placeWithinColumnKey, "PlaceWithinColumn")
    P_CONFIG_KEY(gapKey, "Gap")
    P_CONFIG_KEY(lengthProportionKey, "LengthProportion")
    P_CONFIG_KEY(gapsBetweenTabsKey, "GapsBetweenTabs")
    P_CONFIG_KEY(cornerRadiusKey, "CornerRadius")
    P_CONFIG_KEY(activeColorKey, "ActiveColor")
    P_CONFIG_KEY(inactiveColorKey, "InactiveColor")
    P_CONFIG_KEY(urgentColorKey, "UrgentColor")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Scrolling.DropIndicator
    // Reuses the shared enabledKey / opacityKey / widthKey / radiusKey
    // leaves above, the same way Snapping.Zones.Border spells its border as
    // Width + Radius. Only the fill and border colours need names of their
    // own.
    // ═══════════════════════════════════════════════════════════════════════════
    P_CONFIG_KEY(colorKey, "Color")
    P_CONFIG_KEY(borderColorKey, "BorderColor")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Scrolling.Behavior
    // (also uses focusNewWindowsKey, focusFollowsMouseKey,
    // respectMinimumSizeKey, stickyWindowHandlingKey, insertPositionKey,
    // restoreOnLoginKey, restoreFloatedOnLoginKey, keepFloatingAboveKey —
    // shared leaf names, disambiguated by group)
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(columnWidthStepPercentKey, "ColumnWidthStepPercent")
    P_CONFIG_KEY(windowHeightStepPercentKey, "WindowHeightStepPercent")
    P_CONFIG_KEY(viewScrollStepPercentKey, "ViewScrollStepPercent")
    P_CONFIG_KEY(focusFollowsMouseMaxScrollKey, "FocusFollowsMouseMaxScroll")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Scrolling.Behavior.DragScroll
    // (also uses enabledKey — shared leaf name, disambiguated by group)
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(triggerWidthKey, "TriggerWidth")
    P_CONFIG_KEY(delayMsKey, "DelayMs")
    P_CONFIG_KEY(maxSpeedKey, "MaxSpeed")

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
    // Config Keys — Decorations
    // ═══════════════════════════════════════════════════════════════════════════

    // DecorationProfileTree JSON blob — the user-applied per-surface decoration
    // (shader-pack chain). Mirrors the animation ShaderProfileTree blob under
    // Animations, persisted as a nested JSON object under the Decorations group —
    // with one materialization difference: the decoration schema default is the
    // serialized empty tree ({"baseline":…,"overrides":[]}, non-empty as a
    // map), whereas the animation default is a bare {}.
    P_CONFIG_KEY(decorationProfileTreeKey, "DecorationProfileTree")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Decorations.Performance
    // ═══════════════════════════════════════════════════════════════════════════
    P_CONFIG_KEY(animateFocusedOnlyKey, "AnimateFocusedOnly")
    P_CONFIG_KEY(pauseWhenIdleKey, "PauseWhenIdle")
    P_CONFIG_KEY(idleTimeoutSecKey, "IdleTimeoutSec")
    P_CONFIG_KEY(blurScaleMultiplierKey, "BlurScaleMultiplier")

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
    // into Application-subject Rules — the migration reads from
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

    P_CONFIG_KEY(frameRateKey, "FrameRate")

    // Audio spectrum (Shaders.Audio group; the on/off toggle uses enabledKey).
    // The flat Shaders.AudioVisualizer / Shaders.AudioSpectrumBarCount keys
    // moved here in the v5 migration (migrateV4ToV5).
    P_CONFIG_KEY(barsKey, "Bars")
    P_CONFIG_KEY(autosensKey, "Autosens")
    P_CONFIG_KEY(sensitivityKey, "Sensitivity")
    P_CONFIG_KEY(noiseReductionKey, "NoiseReduction")
    P_CONFIG_KEY(lowerCutoffHzKey, "LowerCutoffHz")
    P_CONFIG_KEY(higherCutoffHzKey, "HigherCutoffHz")
    P_CONFIG_KEY(monstercatKey, "Monstercat")
    P_CONFIG_KEY(wavesKey, "Waves")
    P_CONFIG_KEY(channelModeKey, "ChannelMode")
    P_CONFIG_KEY(reverseKey, "Reverse")
    P_CONFIG_KEY(extraSmoothingKey, "ExtraSmoothing")
    P_CONFIG_KEY(inputMethodKey, "InputMethod")
    P_CONFIG_KEY(inputSourceKey, "InputSource")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Animations
    // ═══════════════════════════════════════════════════════════════════════════

    // (uses enabledKey)
    // Phase 4 sub-commit 6: animation fields migrated from 5 per-field
    // keys (duration / easingCurve / minDistance / sequenceMode /
    // staggerInterval) to a single Profile JSON blob under animationProfileKey.
    // Those v1 spellings are folded into the Profile blob by `migrateV1ToV2`,
    // which reads them through the frozen `v1Animation*Key` accessors in the
    // Legacy section below, not through this one — the live per-field accessors
    // that used to sit here had no caller at all and are gone. The per-field accessor surface on
    // Settings (animationDuration / etc.) is unaffected: it projects through the
    // Profile blob at read/write time for QML Q_PROPERTY binding compatibility.
    P_CONFIG_KEY(animationProfileKey, "Profile")
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
    P_CONFIG_KEY(toggleCheatsheetKey, "ToggleCheatsheet")

    // Parameterized — uses the pattern accessor to avoid duplication.
    // The range mirrors quickLayoutN() in the enum surface; out-of-range
    // values would round-trip as e.g. "QuickLayout100" and ghost the
    // config namespace.
    P_CONFIG_KEY(quickLayoutKeyPattern, "QuickLayout%1")
    static QString quickLayoutKey(int n)
    {
        // qFatal aborts unambiguously in both debug and release builds —
        // the contract is "n in range, no exceptions". A bare Q_ASSERT_X
        // would compile out in release and let an out-of-range value
        // silently yield "QuickLayout100" (or similar), ghosting the
        // config namespace.
        //
        // Bounded by the protocol constant the daemon validates against
        // (layoutadaptor.cpp) rather than a hardcoded 9, so raising the slot
        // count cannot leave this guard rejecting keys the rest of the tree
        // considers legal.
        if (n < 1 || n > PhosphorProtocol::Service::QuickLayoutSlotCount) {
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
    P_CONFIG_KEY(switchFocusFloatTilingKey, "SwitchFocusFloatTiling")
    P_CONFIG_KEY(swapWindowLeftKey, "SwapWindowLeft")
    P_CONFIG_KEY(swapWindowRightKey, "SwapWindowRight")
    P_CONFIG_KEY(swapWindowUpKey, "SwapWindowUp")
    P_CONFIG_KEY(swapWindowDownKey, "SwapWindowDown")
    P_CONFIG_KEY(spanWindowLeftKey, "SpanWindowLeft")
    P_CONFIG_KEY(spanWindowRightKey, "SpanWindowRight")
    P_CONFIG_KEY(spanWindowUpKey, "SpanWindowUp")
    P_CONFIG_KEY(spanWindowDownKey, "SpanWindowDown")

    // Parameterized — uses the pattern accessor to avoid duplication.
    // The range mirrors snapToZoneN() in the enum surface, which is the same
    // digit row the quick-layout slots use.
    P_CONFIG_KEY(snapToZoneKeyPattern, "SnapToZone%1")
    static QString snapToZoneKey(int n)
    {
        // See quickLayoutKey above for the rationale on the qFatal guard and on
        // bounding by the protocol constant.
        if (n < 1 || n > PhosphorProtocol::Service::QuickLayoutSlotCount) {
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

    // Shortcuts.Scrolling keys live in configkeys_scrolling.h
    // (ConfigKeysScrolling, the next link in the inheritance chain) — split
    // by concern when this file hit its size ceiling.

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
    P_CONFIG_KEY(scrollingTemplateOrderKey, "ScrollingTemplateOrder")

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-Screen Config Group Prefixes
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_GROUP(zoneSelectorGroupPrefix, P_PER_SCREEN_PREFIX_ZONE_SELECTOR ":")
    P_CONFIG_GROUP(autotileScreenGroupPrefix, P_PER_SCREEN_PREFIX_AUTOTILE ":")
    P_CONFIG_GROUP(scrollingScreenGroupPrefix, P_PER_SCREEN_PREFIX_SCROLLING ":")
    P_CONFIG_GROUP(scrollingZoneSelectorGroupPrefix, P_PER_SCREEN_PREFIX_SCROLLING_ZONE_SELECTOR ":")

    // ═══════════════════════════════════════════════════════════════════════════
    // Legacy v1/v2/v3/v4 accessors — for migration code, plus the two
    // documented non-migration readers (configdefaults.cpp's
    // v1RenderingBackendKey read and settings.cpp's v4 stash-key list).
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
        // v2 Animations destination — group plus the two leaf keys migrateV1ToV2
        // writes ("Enabled" bool and the stringified "Profile" blob). Frozen at
        // their v2 literals for the same reason as the sibling groups above: a
        // future rename of the live `animationsGroup()` / `enabledKey()` /
        // `animationProfileKey()` accessors must not silently retarget the
        // v1→v2 step to a path no v2 config ever had on disk.
        P_CONFIG_GROUP(v2AnimationsGroup, "Animations")
        P_CONFIG_KEY(v2AnimationsEnabledKey, "Enabled")
        P_CONFIG_KEY(v2AnimationProfileKey, "Profile")
        // Parameterised v2 destinations: v1→v2 renames v1's
        // `QuickLayout%1Shortcut` to v2's `QuickLayout%1` and preserves
        // v1's `SnapToZone%1` verbatim. Frozen accessors pin the v2
        // wire-format names so a future rename of the matching live
        // pattern accessors stays isolated from migration.
        P_CONFIG_KEY(v1QuickLayoutShortcutKeyPattern, "QuickLayout%1Shortcut")
        P_CONFIG_KEY(v2QuickLayoutKeyPattern, "QuickLayout%1")
        P_CONFIG_KEY(v2SnapToZoneKeyPattern, "SnapToZone%1")
        // The hardcoded 9 below is FROZEN at the v1/v2 wire formats, which
        // only ever had nine slots — do NOT retarget these bounds at
        // QuickLayoutSlotCount like the live builders above: if that constant
        // is ever raised, these must keep generating exactly the keys a
        // historical config could hold.
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
        // and removes them as the values move into rules.json. They no
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
        // finalizeV4Conversion to convert into Rules, then removes the key
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
        // Exclude` Rule. Same purge-protection semantics as the two
        // sibling stash keys above.
        P_CONFIG_KEY(v4ExclusionStashKey, "_v4ExclusionStash")
        // Fourth v4 scratch-root key — set on the root by `migrateV3ToV4`
        // from the legacy `Animations.WindowFiltering.{Applications,WindowClasses}`
        // lists and consumed by `finalizeV4Conversion`, which converts each
        // surviving pattern into a `DesktopFile`/`WindowClass Contains
        // <pattern> → ExcludeAnimations` Rule (preserving the
        // legacy effect-bridge match-field split). Same purge-protection
        // semantics as the three sibling stash keys above.
        P_CONFIG_KEY(v4AnimationExclusionStashKey, "_v4AnimationExclusionStash")

        // v3 frozen group accessor — used ONLY by migrateV3ToV4. Mirrors the
        // live `displayGroup` accessor but is frozen at its v3 literal so a
        // future runtime rename cannot silently retarget the migration to a
        // path no v3 config ever had on disk.
        P_CONFIG_GROUP(v3DisplayGroup, "Display")

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
    // own ephemeral state: last window geometry and the last-seen what's-new
    // version. Centralised here so the CLAUDE.md "no inline QStringLiteral for
    // config keys" rule applies uniformly.
    // ═══════════════════════════════════════════════════════════════════════════
    // Group for the window-geometry entries: the bare "x"/"y"/"width"/
    // "height" keys collide with any other writer at the file root.
    P_CONFIG_GROUP(settingsAppWindowGroup, "Window")
    P_CONFIG_KEY(settingsAppWindowXKey, "x")
    P_CONFIG_KEY(settingsAppWindowYKey, "y")
    P_CONFIG_KEY(settingsAppWindowWidthKey, "width")
    P_CONFIG_KEY(settingsAppWindowHeightKey, "height")
    P_CONFIG_KEY(settingsAppLastSeenWhatsNewVersionKey, "lastSeenWhatsNewVersion")

    // ═══════════════════════════════════════════════════════════════════════════
    // Filesystem paths under XDG_DATA_HOME
    //
    // Daemon, settings app, and editor all read/write the same per-user
    // layouts directory. Hoisted into one accessor so a rename only touches one
    // site. The sibling "plasmazones" and "plasmazones/algorithms" spellings
    // used to sit here for the same reason, but neither had a caller: the
    // algorithms directory is spelled by Constants::ScriptedAlgorithmSubdir
    // (core/types/constants.h), which is what every consumer actually uses.
    // ═══════════════════════════════════════════════════════════════════════════
    P_CONFIG_KEY(layoutsSubdir, "plasmazones/layouts")

private:
    // Non-instantiable
    ConfigKeys() = delete;
};

// ─── Disable-rule label helpers ─────────────────────────────────────────────
// Shared between the live Settings disable-list writer
// (Settings::writeDisableEntries) and the v3→v4 migration's disable-rule
// builders. Both call sites must produce the same `Rule::name` string for
// a given (mode, screen, desktop, activity) tuple so that resaving an existing
// disable list (e.g. after a UI edit) doesn't fork into two slightly different
// labels for what is otherwise the same rule.
//
// These are NOT translated. `Rule::name` is the persisted identity
// surface in rules.json; running the app under different locales must
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

/// Persistent label-prefix for the Rule::name field of a per-mode
/// disable rule. Exhaustive switch — a future `Mode` enum value added in
/// `AssignmentEntry.h` without an entry here fires a `Q_UNREACHABLE`
/// diagnostic rather than silently producing an empty prefix that lands
/// in the persisted `Rule::name` as bare ` · DP-1` (parseable but
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
