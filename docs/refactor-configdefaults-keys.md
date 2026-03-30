# Refactor: Extract Config Keys to ConfigDefaults

## Status: Proposed

## Problem

Config group names and key strings are hardcoded as `QStringLiteral("...")` throughout
`settings.cpp` and `settings/loadsave.cpp`. This creates:

- **Typo risk** — a misspelled key silently reads/writes the wrong value
- **Inconsistency** — `generalGroup()` and `renderingBackendKey()` exist in `ConfigDefaults`
  but the other ~130 keys are still inline strings
- **Grep friction** — finding all references to a config key requires searching for a raw string
  rather than a symbol the compiler can track

## Goal

Every config group and key string used in `settings.cpp` / `loadsave.cpp` should have a
`static QString` accessor in `ConfigDefaults`, following the existing pattern:

```cpp
// configdefaults.h
static QString activationGroup()   { return QStringLiteral("Activation"); }
static QString snappingEnabledKey() { return QStringLiteral("SnappingEnabled"); }
```

Then call sites become:

```cpp
auto activation = m_configBackend->group(ConfigDefaults::activationGroup());
m_snappingEnabled = activation.readBool(ConfigDefaults::snappingEnabledKey(),
                                        ConfigDefaults::snappingEnabled());
```

## Naming Convention

| Type  | Pattern                     | Example                        |
|-------|-----------------------------|--------------------------------|
| Group | `<name>Group()`             | `activationGroup()`            |
| Key   | `<settingName>Key()`        | `snappingEnabledKey()`         |

All accessors return `QString` (not `QLatin1String`) and use `QStringLiteral` internally
so they are zero-allocation after first use.

## Scope

### Groups (14 + 1 already done)

| Group String              | Accessor                      | Done |
|---------------------------|-------------------------------|------|
| `General`                 | `generalGroup()`              | Yes  |
| `Activation`              | `activationGroup()`           |      |
| `Display`                 | `displayGroup()`              |      |
| `Appearance`              | `appearanceGroup()`           |      |
| `Zones`                   | `zonesGroup()`                |      |
| `Behavior`                | `behaviorGroup()`             |      |
| `Exclusions`              | `exclusionsGroup()`           |      |
| `ZoneSelector`            | `zoneSelectorGroup()`         |      |
| `Shaders`                 | `shadersGroup()`              |      |
| `GlobalShortcuts`         | `globalShortcutsGroup()`      |      |
| `Autotiling`              | `autotilingGroup()`           |      |
| `AutotileShortcuts`       | `autotileShortcutsGroup()`    |      |
| `Animations`              | `animationsGroup()`           |      |
| `Updates`                 | `updatesGroup()`              |      |
| `Editor`                  | `editorGroup()`               |      |
| `TilingQuickLayoutSlots`  | `tilingQuickLayoutSlotsGroup()`|     |

### Keys by Group (~130 total)

#### Activation (11 keys)

| Key String                  | Accessor                          |
|-----------------------------|-----------------------------------|
| `DragActivationModifier`    | `dragActivationModifierKey()`     |
| `DragActivationMouseButton` | `dragActivationMouseButtonKey()`  |
| `DragActivationTriggers`    | `dragActivationTriggersKey()`     |
| `ToggleActivation`          | `toggleActivationKey()`           |
| `SnappingEnabled`           | `snappingEnabledKey()`            |
| `ZoneSpanEnabled`           | `zoneSpanEnabledKey()`            |
| `ZoneSpanModifier`          | `zoneSpanModifierKey()`           |
| `ZoneSpanTriggers`          | `zoneSpanTriggersKey()`           |
| `SnapAssistFeatureEnabled`  | `snapAssistFeatureEnabledKey()`   |
| `SnapAssistEnabled`         | `snapAssistEnabledKey()`          |
| `SnapAssistTriggers`        | `snapAssistTriggersKey()`         |

#### Display (10 keys)

| Key String                | Accessor                        |
|---------------------------|---------------------------------|
| `ShowOnAllMonitors`       | `showOnAllMonitorsKey()`        |
| `DisabledMonitors`        | `disabledMonitorsKey()`         |
| `DisabledDesktops`        | `disabledDesktopsKey()`         |
| `DisabledActivities`      | `disabledActivitiesKey()`       |
| `ShowNumbers`             | `showNumbersKey()`              |
| `FlashOnSwitch`           | `flashOnSwitchKey()`            |
| `ShowOsdOnLayoutSwitch`   | `showOsdOnLayoutSwitchKey()`    |
| `ShowNavigationOsd`       | `showNavigationOsdKey()`        |
| `OsdStyle`                | `osdStyleKey()`                 |
| `OverlayDisplayMode`      | `overlayDisplayModeKey()`       |

#### Appearance (16 keys)

| Key String              | Accessor                      |
|-------------------------|-------------------------------|
| `UseSystemColors`       | `useSystemColorsKey()`        |
| `HighlightColor`        | `highlightColorKey()`         |
| `InactiveColor`         | `inactiveColorKey()`          |
| `BorderColor`           | `borderColorKey()`            |
| `LabelFontColor`        | `labelFontColorKey()`         |
| `ActiveOpacity`         | `activeOpacityKey()`          |
| `InactiveOpacity`       | `inactiveOpacityKey()`        |
| `BorderWidth`           | `borderWidthKey()`            |
| `BorderRadius`          | `borderRadiusKey()`           |
| `EnableBlur`            | `enableBlurKey()`             |
| `LabelFontFamily`       | `labelFontFamilyKey()`        |
| `LabelFontSizeScale`    | `labelFontSizeScaleKey()`     |
| `LabelFontWeight`       | `labelFontWeightKey()`        |
| `LabelFontItalic`       | `labelFontItalicKey()`        |
| `LabelFontUnderline`    | `labelFontUnderlineKey()`     |
| `LabelFontStrikeout`    | `labelFontStrikeoutKey()`     |

#### Zones (11 keys)

| Key String                  | Accessor                          |
|-----------------------------|-----------------------------------|
| `Padding`                   | `zonePaddingKey()`                |
| `OuterGap`                  | `outerGapKey()`                   |
| `UsePerSideOuterGap`        | `usePerSideOuterGapKey()`         |
| `OuterGapTop`               | `outerGapTopKey()`                |
| `OuterGapBottom`            | `outerGapBottomKey()`             |
| `OuterGapLeft`              | `outerGapLeftKey()`               |
| `OuterGapRight`             | `outerGapRightKey()`              |
| `PollIntervalMs`            | `pollIntervalMsKey()`             |
| `MinimumZoneSizePx`         | `minimumZoneSizePxKey()`          |
| `MinimumZoneDisplaySizePx`  | `minimumZoneDisplaySizePxKey()`   |
| `AdjacentThreshold`         | `adjacentThresholdKey()`          |

#### Behavior (7 keys)

| Key String                      | Accessor                              |
|---------------------------------|---------------------------------------|
| `KeepOnResolutionChange`        | `keepOnResolutionChangeKey()`         |
| `MoveNewToLastZone`             | `moveNewToLastZoneKey()`              |
| `RestoreSizeOnUnsnap`           | `restoreSizeOnUnsnapKey()`            |
| `StickyWindowHandling`          | `stickyWindowHandlingKey()`           |
| `RestoreWindowsToZonesOnLogin`  | `restoreWindowsToZonesOnLoginKey()`   |
| `DefaultLayoutId`               | `defaultLayoutIdKey()`                |
| `FilterLayoutsByAspectRatio`    | `filterLayoutsByAspectRatioKey()`     |

#### Exclusions (5 keys)

| Key String                | Accessor                          |
|---------------------------|-----------------------------------|
| `Applications`            | `excludedApplicationsKey()`       |
| `WindowClasses`           | `excludedWindowClassesKey()`      |
| `ExcludeTransientWindows` | `excludeTransientWindowsKey()`    |
| `MinimumWindowWidth`      | `minimumWindowWidthKey()`         |
| `MinimumWindowHeight`     | `minimumWindowHeightKey()`        |

#### ZoneSelector (10 keys)

| Key String          | Accessor                            |
|---------------------|-------------------------------------|
| `Enabled`           | `zoneSelectorEnabledKey()`          |
| `TriggerDistance`   | `zoneSelectorTriggerDistanceKey()`  |
| `Position`          | `zoneSelectorPositionKey()`         |
| `LayoutMode`        | `zoneSelectorLayoutModeKey()`       |
| `PreviewWidth`      | `zoneSelectorPreviewWidthKey()`     |
| `PreviewHeight`     | `zoneSelectorPreviewHeightKey()`    |
| `PreviewLockAspect` | `zoneSelectorPreviewLockAspectKey()`|
| `GridColumns`       | `zoneSelectorGridColumnsKey()`      |
| `SizeMode`          | `zoneSelectorSizeModeKey()`         |
| `MaxRows`           | `zoneSelectorMaxRowsKey()`          |

#### General (1 key — already done)

| Key String          | Accessor                | Done |
|---------------------|-------------------------|------|
| `RenderingBackend`  | `renderingBackendKey()` | Yes  |

#### Shaders (4 keys)

| Key String              | Accessor                      |
|-------------------------|-------------------------------|
| `EnableShaderEffects`   | `enableShaderEffectsKey()`    |
| `ShaderFrameRate`       | `shaderFrameRateKey()`        |
| `EnableAudioVisualizer` | `enableAudioVisualizerKey()`  |
| `AudioSpectrumBarCount` | `audioSpectrumBarCountKey()`  |

#### GlobalShortcuts (31 keys)

| Key String                           | Accessor                                      |
|--------------------------------------|-----------------------------------------------|
| `OpenEditorShortcut`                 | `openEditorShortcutKey()`                     |
| `OpenSettingsShortcut`               | `openSettingsShortcutKey()`                   |
| `PreviousLayoutShortcut`             | `previousLayoutShortcutKey()`                 |
| `NextLayoutShortcut`                 | `nextLayoutShortcutKey()`                     |
| `QuickLayout1Shortcut` .. `9`        | `quickLayoutShortcutKey(int n)`               |
| `MoveWindowLeft`                     | `moveWindowLeftShortcutKey()`                 |
| `MoveWindowRight`                    | `moveWindowRightShortcutKey()`                |
| `MoveWindowUp`                       | `moveWindowUpShortcutKey()`                   |
| `MoveWindowDown`                     | `moveWindowDownShortcutKey()`                 |
| `SwapWindowLeft`                     | `swapWindowLeftShortcutKey()`                 |
| `SwapWindowRight`                    | `swapWindowRightShortcutKey()`                |
| `SwapWindowUp`                       | `swapWindowUpShortcutKey()`                   |
| `SwapWindowDown`                     | `swapWindowDownShortcutKey()`                 |
| `FocusZoneLeft`                      | `focusZoneLeftShortcutKey()`                  |
| `FocusZoneRight`                     | `focusZoneRightShortcutKey()`                 |
| `FocusZoneUp`                        | `focusZoneUpShortcutKey()`                    |
| `FocusZoneDown`                      | `focusZoneDownShortcutKey()`                  |
| `PushToEmptyZone`                    | `pushToEmptyZoneShortcutKey()`                |
| `RestoreWindowSize`                  | `restoreWindowSizeShortcutKey()`              |
| `ToggleWindowFloat`                  | `toggleWindowFloatShortcutKey()`              |
| `RotateWindowsClockwise`            | `rotateWindowsClockwiseShortcutKey()`         |
| `RotateWindowsCounterclockwise`     | `rotateWindowsCounterclockwiseShortcutKey()`  |
| `CycleWindowForward`                | `cycleWindowForwardShortcutKey()`             |
| `CycleWindowBackward`               | `cycleWindowBackwardShortcutKey()`            |
| `SnapToZone1` .. `9`                | `snapToZoneShortcutKey(int n)`                |
| `ResnapToNewLayoutShortcut`          | `resnapToNewLayoutShortcutKey()`              |
| `SnapAllWindowsShortcut`            | `snapAllWindowsShortcutKey()`                 |
| `LayoutPickerShortcut`              | `layoutPickerShortcutKey()`                   |
| `ToggleLayoutLockShortcut`          | `toggleLayoutLockShortcutKey()`               |

#### Autotiling (27 keys)

| Key String                             | Accessor                                  |
|----------------------------------------|-------------------------------------------|
| `AutotileEnabled`                      | `autotileEnabledKey()`                    |
| `DefaultAutotileAlgorithm`             | `defaultAutotileAlgorithmKey()`           |
| `AutotileSplitRatio`                   | `autotileSplitRatioKey()`                 |
| `AutotileMasterCount`                  | `autotileMasterCountKey()`                |
| `AutotilePerAlgorithmSettings`         | `autotilePerAlgorithmSettingsKey()`       |
| `AutotileCenteredMasterSplitRatio`     | `autotileCenteredMasterSplitRatioKey()`   |
| `AutotileCenteredMasterMasterCount`    | `autotileCenteredMasterMasterCountKey()`  |
| `AutotileInnerGap`                     | `autotileInnerGapKey()`                   |
| `AutotileOuterGap`                     | `autotileOuterGapKey()`                   |
| `AutotileUsePerSideOuterGap`           | `autotileUsePerSideOuterGapKey()`         |
| `AutotileOuterGapTop`                  | `autotileOuterGapTopKey()`                |
| `AutotileOuterGapBottom`               | `autotileOuterGapBottomKey()`             |
| `AutotileOuterGapLeft`                 | `autotileOuterGapLeftKey()`               |
| `AutotileOuterGapRight`                | `autotileOuterGapRightKey()`              |
| `AutotileFocusNewWindows`              | `autotileFocusNewWindowsKey()`            |
| `AutotileSmartGaps`                    | `autotileSmartGapsKey()`                  |
| `AutotileMaxWindows`                   | `autotileMaxWindowsKey()`                 |
| `AutotileInsertPosition`               | `autotileInsertPositionKey()`             |
| `AutotileFocusFollowsMouse`            | `autotileFocusFollowsMouseKey()`          |
| `AutotileRespectMinimumSize`           | `autotileRespectMinimumSizeKey()`         |
| `AutotileHideTitleBars`                | `autotileHideTitleBarsKey()`              |
| `AutotileShowBorder`                   | `autotileShowBorderKey()`                 |
| `AutotileBorderWidth`                  | `autotileBorderWidthKey()`                |
| `AutotileBorderRadius`                 | `autotileBorderRadiusKey()`               |
| `AutotileBorderColor`                  | `autotileBorderColorKey()`                |
| `AutotileInactiveBorderColor`          | `autotileInactiveBorderColorKey()`        |
| `AutotileUseSystemBorderColors`        | `autotileUseSystemBorderColorsKey()`      |
| `LockedScreens`                        | `lockedScreensKey()`                      |

#### AutotileShortcuts (8 keys)

| Key String                  | Accessor                              |
|-----------------------------|---------------------------------------|
| `ToggleShortcut`            | `autotileToggleShortcutKey()`         |
| `FocusMasterShortcut`       | `autotileFocusMasterShortcutKey()`    |
| `SwapMasterShortcut`        | `autotileSwapMasterShortcutKey()`     |
| `IncMasterRatioShortcut`    | `autotileIncMasterRatioShortcutKey()` |
| `DecMasterRatioShortcut`    | `autotileDecMasterRatioShortcutKey()` |
| `IncMasterCountShortcut`    | `autotileIncMasterCountShortcutKey()` |
| `DecMasterCountShortcut`    | `autotileDecMasterCountShortcutKey()` |
| `RetileShortcut`            | `autotileRetileShortcutKey()`         |

#### Animations (6 keys)

| Key String                | Accessor                          |
|---------------------------|-----------------------------------|
| `AnimationsEnabled`       | `animationsEnabledKey()`          |
| `AnimationDuration`       | `animationDurationKey()`          |
| `AnimationSequenceMode`   | `animationSequenceModeKey()`      |
| `AnimationStaggerInterval`| `animationStaggerIntervalKey()`   |
| `AnimationEasingCurve`    | `animationEasingCurveKey()`       |
| `AnimationMinDistance`     | `animationMinDistanceKey()`       |

#### Editor (12 keys)

| Key String                      | Accessor                              |
|---------------------------------|---------------------------------------|
| `EditorDuplicateShortcut`       | `editorDuplicateShortcutKey()`        |
| `EditorSplitHorizontalShortcut` | `editorSplitHorizontalShortcutKey()`  |
| `EditorSplitVerticalShortcut`   | `editorSplitVerticalShortcutKey()`    |
| `EditorFillShortcut`            | `editorFillShortcutKey()`             |
| `GridSnappingEnabled`           | `editorGridSnappingEnabledKey()`      |
| `EdgeSnappingEnabled`           | `editorEdgeSnappingEnabledKey()`      |
| `SnapIntervalX`                 | `editorSnapIntervalXKey()`            |
| `SnapIntervalY`                 | `editorSnapIntervalYKey()`            |
| `SnapInterval`                  | `editorSnapIntervalKey()` (legacy)    |
| `SnapOverrideModifier`          | `editorSnapOverrideModifierKey()`     |
| `FillOnDropEnabled`             | `fillOnDropEnabledKey()`              |
| `FillOnDropModifier`            | `fillOnDropModifierKey()`             |

#### TilingQuickLayoutSlots (numbered 1-9)

Use a parameterized accessor:

```cpp
static QString tilingQuickLayoutSlotKey(int n)
{
    return QString::number(n);
}
```

## Implementation Notes

### File changes

| File                          | Change                                    |
|-------------------------------|-------------------------------------------|
| `src/config/configdefaults.h` | Add ~145 static `QString` accessors       |
| `src/config/settings.cpp`     | Replace inline strings with accessors     |
| `src/config/settings/loadsave.cpp` | Replace inline strings with accessors |

### Guidelines

- All accessors are `static` inline in the header — no `.cpp` changes needed
- Return `QString` (not `QLatin1String`) to match Qt6 API expectations
- Group accessors end with `Group()`, key accessors end with `Key()`
- Parameterized keys (QuickLayout1-9, SnapToZone1-9) use `int` parameter
- Keep the existing default-value accessors (`snappingEnabled()`, etc.) —
  the new `Key()` accessors are complementary, not replacements
- Do NOT touch `settingsadaptor.cpp` — its REGISTER macros use D-Bus property
  names (camelCase), not config file keys (PascalCase)

### Phased rollout

| Phase | Groups                                        | Keys | Risk  |
|-------|-----------------------------------------------|------|-------|
| 1     | All 15 groups                                 | 15   | Minimal — group names are simple |
| 2     | Shaders, General, Behavior, Activation        | ~27  | Low — high-traffic settings      |
| 3     | Appearance, Zones, Display, Exclusions        | ~42  | Low — core UI settings           |
| 4     | GlobalShortcuts, AutotileShortcuts, Editor    | ~51  | Low — string-heavy but mechanical|
| 5     | Autotiling, Animations, ZoneSelector, Slots   | ~51  | Low — remaining settings         |

Each phase is a single atomic commit. Tests should pass after each phase since
the refactor is purely mechanical (string identity is preserved).
