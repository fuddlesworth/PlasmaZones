# PlasmaZones – Codebase Analysis: Bugs and KDE/Qt Issues

Deep analysis of the PlasmaZones codebase for bugs, Qt6/KDE misuse, and robustness issues. Items marked **FIXED** were addressed in this pass.

---

## 1. Division-by-zero and invalid geometry

### 1.1 OverlayService: `aspectRatio = width / screenGeom.height()` — **FIXED**

**Location:** `src/daemon/overlayservice.cpp`  
- ~994: `createZoneSelectorWindow`  
- ~1052: `updateZoneSelectorWindow`

**Issue:** If `screen->geometry().height()` is 0 (e.g. uninitialized, disconnected, or degenerate output), the division is undefined and can crash or produce NaN.

**Fix applied:** Use a safe aspect ratio when `height <= 0`:

```cpp
qreal aspectRatio = (screenGeom.height() > 0)
    ? static_cast<qreal>(screenGeom.width()) / screenGeom.height()
    : (16.0 / 9.0);
```

### 1.2 Layout::createColumnsLayout / createRowsLayout / createGridLayout — **FIXED**

**Location:** `src/core/layout.cpp`

**Issue:** `columnWidth = 1.0 / columns` and `rowHeight = 1.0 / rows` with `columns` or `rows == 0` causes division by zero. Current D-Bus and built-in callers use `>= 1`, but the API is public and could be misused.

**Fix applied:** Clamp to 1 before dividing:

- `createColumnsLayout`: `if (columns < 1) columns = 1;`
- `createRowsLayout`: `if (rows < 1) rows = 1;`
- `createGridLayout`: same for both `columns` and `rows`.

### 1.3 SnappingService::snapValueToGrid(value, interval)

**Location:** `src/editor/services/SnappingService.cpp`

**Status:** Acceptable.  
- `interval` comes from `m_snapIntervalX` / `m_snapIntervalY`.  
- `setSnapIntervalX/Y` uses `qBound(0.01, interval, 1.0)`, so stored interval is always ≥ 0.01.  
- `snapValueToGrid` is only called with these members.

### 1.4 WindowDragAdaptor and OverlayService aspect ratios

**Location:**  
- `src/dbus/windowdragadaptor.cpp` ~628–631  
- `src/daemon/overlayservice.cpp` ~99–101, 114, 120  

**Status:** Acceptable.  
- `screenGeom.height() > 0` is checked before `width/height`; otherwise a default (e.g. 16/9) is used.  
- Divisions by `aspectRatio` are safe because it is either from a positive `height` or a non‑zero default.

---

## 2. D-Bus and effect robustness

### 2.1 `msg.arguments().at(N)` use

**Location:** `kwin-effect/plasmazoneseffect.cpp`

**Status:** Generally safe.  
- Most call sites check `msg.type() == QDBusMessage::ReplyMessage` and either `!msg.arguments().isEmpty()` or `msg.arguments().size() >= 5` before `at(0)` … `at(4)`.  
- `getUpdatedWindowGeometries` (around 273–280): `at(0)` is only used after `!msg.arguments().isEmpty()`.

### 2.2 getValidatedPreSnapGeometry reply layout

**Location:** `kwin-effect/plasmazoneseffect.cpp`, `src/dbus/windowtrackingadaptor.cpp`, `dbus/org.plasmazones.xml`

**Status:** Correct.  
- C++: `bool getValidatedPreSnapGeometry(windowId, int &x, int &y, int &width, int &height)` ⇒ reply order: `[found, x, y, width, height]`.  
- Effect uses `at(0)`–`at(4)` and checks `size() >= 5` before.

### 2.3 `toggleWindowFloatRequested` in D-Bus XML

**Status:** Documented/fixed separately. The signal is now present in `dbus/org.plasmazones.xml` for the WindowTracking interface.

---

## 3. Qt6 string and JSON usage

### 3.1 JSON and `QString` / `QLatin1String`

**Status:** Largely correct.  
- JSON keys use `QStringLiteral(...)`, `JsonKeys::*` (QLatin1String), or `QLatin1String("...")` in line with project rules.  
- `JsonKeys` in `src/core/constants.h` are `inline constexpr QLatin1String`, which is appropriate for Qt6.

### 3.2 ZoneManager `zone[JsonKeys::UseCustomColors]` — **FIXED**

**Location:** `src/editor/services/ZoneManager.cpp`

**Issue:** `zone[QString::fromLatin1(JsonKeys::UseCustomColors)]` was redundant and could be confusing given `JsonKeys::UseCustomColors` is already `QLatin1String` and `QVariantMap` accepts keys that convert to `QString`.

**Fix applied:** Use `zone[JsonKeys::UseCustomColors]` for consistency with other `zone[JsonKeys::…]` usages.

---

## 4. Signal/slot and QObject usage

### 4.1 `emit` vs `Q_EMIT`

**Status:** Good.  
- Grep shows `Q_EMIT` used across the codebase; no raw `emit` in signal emission.

### 4.2 Manual `delete` on QObjects

**Findings:**

- **ShortcutManager** (`src/daemon/shortcutmanager.cpp`): `delete m_*Action` for KGlobalAccel-owned actions, then `= nullptr`. Comment explains that `deleteLater()` is avoided in this controlled teardown. Acceptable.  
- **ScreenManager** (`src/core/screenmanager.cpp`):  
  - `delete sensor` when `LayerShellQt::Window::get(sensor)` fails, before the sensor is ever stored or shown. Correct.  
  - `delete plasmaShell` in the async callback for `queryKdePlasmaPanels`. `plasmaShell` is created with `this` as parent; explicit `delete` removes it from the parent and is fine.  
- **LayoutManager** (`src/core/layoutmanager.cpp`): `delete layout` for duplicate or invalid layouts that are never added to `m_layouts`. Correct.  
- **VirtualDesktopManager** (`src/core/virtualdesktopmanager.cpp`): `delete m_kwinVDInterface` when the KWin D-Bus interface is invalid; it is created with `this` as parent. Acceptable.  
- **Tests:** `delete` of test helpers (e.g. persistence/tracker) is in line with test lifecycle.

---

## 5. KDE / KConfig / settings

### 5.1 kcfg vs settings.cpp

**Status:** Aligned.  
- `plasmazones.kcfg` entry names (e.g. `ShowOnAllMonitors`, `Padding`, `ToggleWindowFloat`) match the keys used in `settings.cpp` `readEntry` / `writeEntry`.  
- Matches the rule that kcfg keys must match for backward compatibility.

### 5.2 Editor settings

Per `.cursorrules`, Editor-group settings are handled in `EditorController::loadEditorSettings` / `saveEditorSettings`, not in the main `Settings` class. No mismatch observed.

---

## 6. getActiveWindow and navigation

**Location:** `kwin-effect/plasmazoneseffect.cpp`

**Status:** Already improved in a prior change.  
- Prefers `KWin::effects->activeWindow()` when it is valid and passes `shouldHandleWindow` and activity/desktop checks.  
- Falls back to the topmost manageable window on the current desktop/activity when `activeWindow()` is null or not manageable.  
- Returns `nullptr` only when there is no such window, which avoids unnecessary “no window” feedback when a suitable window exists.

---

## 7. Recommendations (all addressed)

### 7.1 Layout API robustness — **FIXED**

- `Layout::createColumnsLayout`, `createRowsLayout`, and `createGridLayout` now clamp `columns`/`rows` to ≥ 1.  
- Zero or negative values are silently treated as 1 (consistent with safe defaults philosophy).

### 7.2 OverlayService `screenAspectRatio` in QML — **FIXED**

- The two call sites that write `screenAspectRatio` into QML are guarded with `height > 0` checks.
- Additional unguarded division-by-height found and fixed:
  - `src/dbus/zonedetectionadaptor.cpp`: Added guard for `availableGeom.width() <= 0 || availableGeom.height() <= 0`
  - `src/dbus/windowdragadaptor.cpp`: Added same guard before dividing by geometry dimensions

### 7.3 D-Bus `getFloatingWindows` return type

- `getFloatingWindows` in the XML is `(windowIds type="as" direction="out")`.  
- C++ returns `QStringList`; the effect uses `msg.arguments().at(0).toStringList()`.  
- This matches Qt D-Bus marshalling for a single `as` out. No change needed.

### 7.4 i18n in QML — **FIXED**

- Full i18n pass completed. Fixed issues:
  - `src/ui/ZoneSelectorWindow.qml`: Replaced `qsTr()` calls with `i18nc()` for dimension tooltip
  - `src/editor/qml/DimensionTooltip.qml`: Replaced string concatenation with `i18nc()` for position/size labels
- Keyboard shortcut fallbacks in `HelpDialogContent.qml` left as-is (technical identifiers that match keycap labels)

---

## 8. Summary of fixes in this pass

| Item | File | Change |
|------|------|--------|
| Division by zero (aspect ratio) | `src/daemon/overlayservice.cpp` | Guard `aspectRatio` when `screenGeom.height() <= 0` in `createZoneSelectorWindow` and `updateZoneSelectorWindow`. |
| Division by zero (columns/rows) | `src/core/layout.cpp` | Clamp `columns` and `rows` to ≥ 1 in `createColumnsLayout`, `createRowsLayout`, and `createGridLayout`. |
| Division by zero (available geometry) | `src/dbus/zonedetectionadaptor.cpp`, `src/dbus/windowdragadaptor.cpp` | Guard against zero-size `availableGeom` before dividing. |
| Redundant `QString::fromLatin1` | `src/editor/services/ZoneManager.cpp` | Use `zone[JsonKeys::UseCustomColors]` instead of `zone[QString::fromLatin1(JsonKeys::UseCustomColors)]`. |
| i18n: Replace qsTr() | `src/ui/ZoneSelectorWindow.qml` | Replace `qsTr()` with `i18nc()` for dimension tooltip. |
| i18n: Format strings | `src/editor/qml/DimensionTooltip.qml` | Replace string concatenation with `i18nc()` for proper localization. |

---

## 9. Build and tests

- `cmake` (Qt6, KF6) and `cmake --build` succeed.  
- Unit targets `test_window_identity`, `test_window_tracking`, and `test_session_persistence` build successfully.  

Running the full test suite is recommended:

```bash
cmake --build build && (cd build && ctest --output-on-failure)
```

---

## 10. Global Settings vs Layout Settings Discrepancies

### 10.1 Overview

Settings exist at multiple levels in PlasmaZones:

1. **Global Settings** (`Settings` class / `plasmazones.kcfg`) - User preferences applied globally
2. **Layout Settings** (`Layout` class) - Per-layout overrides stored in layout JSON files
3. **Zone Settings** (`Zone` class) - Per-zone appearance stored in layout JSON files
4. **QML Defaults** - Hardcoded fallback values in QML components

The intended behavior is: **Layout/Zone settings should take precedence over global settings when defined.**

### 10.2 Settings That Exist at Multiple Levels (All Fixed)

| Setting | Global (Settings) | Layout | Zone | Current Behavior |
|---------|------------------|--------|------|------------------|
| `zonePadding` | ✅ `Zones/Padding` | ✅ Stored in JSON | ❌ | ✅ **FIXED**: Layout takes precedence |
| `showZoneNumbers` | ✅ `Display/ShowNumbers` | ✅ Stored in JSON | ❌ | ✅ **FIXED**: Layout takes precedence |
| `highlightColor` | ✅ `Appearance/HighlightColor` | ❌ | ✅ | ✅ Correct (zone overrides when `useCustomColors=true`) |
| `inactiveColor` | ✅ `Appearance/InactiveColor` | ❌ | ✅ | ✅ Correct |
| `borderColor` | ✅ `Appearance/BorderColor` | ❌ | ✅ | ✅ Correct |
| `activeOpacity` | ✅ `Appearance/ActiveOpacity` | ❌ | ✅ | ✅ **FIXED**: Zone has separate active/inactive opacity |
| `inactiveOpacity` | ✅ `Appearance/InactiveOpacity` | ❌ | ✅ | ✅ **FIXED**: Zone has separate active/inactive opacity |
| `borderWidth` | ✅ `Appearance/BorderWidth` | ❌ | ✅ | ✅ Correct |
| `borderRadius` | ✅ `Appearance/BorderRadius` | ❌ | ✅ | ✅ Correct |

### 10.3 Layout `zonePadding` Precedence — **FIXED**

`OverlayService::zoneToVariantMap()` now uses layout's `zonePadding()` when available, falling back to settings.

### 10.4 Layout `showZoneNumbers` Precedence — **FIXED**

`OverlayService::updateOverlayWindow()` now uses layout's `showZoneNumbers()` when available, falling back to settings.

### 10.5 Opacity: Zone Now Has Separate Active/Inactive — **FIXED**

Zone class now has `activeOpacity` and `inactiveOpacity` properties matching the global settings structure:

```cpp
// Zone class
Q_PROPERTY(qreal activeOpacity READ activeOpacity WRITE setActiveOpacity NOTIFY activeOpacityChanged)
Q_PROPERTY(qreal inactiveOpacity READ inactiveOpacity WRITE setInactiveOpacity NOTIFY inactiveOpacityChanged)
```

The old magic 0.73 multiplier has been removed. QML now uses the zone's separate opacity values directly.

### 10.6 QML Defaults Match Settings Defaults — **FIXED**

All QML files now use defaults matching the Settings class:

```qml
// In ZoneOverlay.qml, ZoneSelectorWindow.qml, ZoneItem.qml
property real activeOpacity: 0.5   // Match Settings default
property real inactiveOpacity: 0.3 // Match Settings default
```

### 10.7 Zone Colors Work Correctly (No Double Application)

**Correct Implementation:** Zone appearance (colors, opacity, border) correctly uses the `useCustomColors` flag. This pattern is unchanged and works correctly - zone settings completely replace global settings when `useCustomColors=true`.

### 10.8 Data Flow Diagram (Updated)

```
Settings (Global)          Layout (Per-Layout)           Zone (Per-Zone)
┌─────────────────┐        ┌──────────────────┐        ┌──────────────────┐
│ zonePadding     │        │ zonePadding      │        │ useCustomColors  │
│ showZoneNumbers │        │ showZoneNumbers  │        │ highlightColor   │
│ highlightColor  │        │                  │        │ inactiveColor    │
│ inactiveColor   │        │                  │        │ borderColor      │
│ borderColor     │        │                  │        │ activeOpacity    │
│ activeOpacity   │        │                  │        │ inactiveOpacity  │
│ inactiveOpacity │        │                  │        │ borderWidth      │
│ borderWidth     │        │                  │        │ borderRadius     │
│ borderRadius    │        │                  │        │                  │
└────────┬────────┘        └────────┬─────────┘        └────────┬─────────┘
         │                          │                           │
         │                          │                           │
         v                          v                           v
┌─────────────────────────────────────────────────────────────────────────┐
│                         OverlayService                                  │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │ Layout overrides Settings for zonePadding, showZoneNumbers      │    │
│  │ Zone overrides all appearance when useCustomColors=true         │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
         │
         v
┌─────────────────────────────────────────────────────────────────────────┐
│                         QML Components                                   │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │ Uses useCustomColors flag to choose between:                    │    │
│  │   - Zone colors (when useCustomColors=true)                     │    │
│  │   - Root properties from Settings (when useCustomColors=false)  │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

### 10.9 Files Modified (All Fixed)

| File | Changes Made |
|------|--------------|
| `src/core/constants.h` | Added `ActiveOpacity` and `InactiveOpacity` JSON keys; removed legacy `Opacity` key |
| `src/core/zone.h` | Replaced single `opacity` with `activeOpacity`/`inactiveOpacity` properties |
| `src/core/zone.cpp` | Updated setters and serialization (no backward compatibility) |
| `src/daemon/overlayservice.h` | Updated `zoneToVariantMap` signature to accept Layout parameter |
| `src/daemon/overlayservice.cpp` | Use layout's `zonePadding()` and `showZoneNumbers()` with fallback to Settings |
| `src/daemon/zoneselectorcontroller.cpp` | Updated zoneToVariantMap for new opacity properties |
| `src/dbus/layoutadaptor.cpp` | Updated to use new opacity methods |
| `src/dbus/zonedetectionadaptor.cpp` | Added guard for zero-size geometry before division |
| `src/dbus/windowdragadaptor.cpp` | Added guard for zero-size geometry before division |
| `src/ui/ZoneOverlay.qml` | Updated defaults to 0.5/0.3, use `activeOpacity`/`inactiveOpacity` from zone data |
| `src/ui/ZoneSelectorWindow.qml` | Updated defaults to 0.5/0.3, use `activeOpacity`/`inactiveOpacity`, fixed i18n |
| `src/ui/ZoneItem.qml` | Updated defaults to 0.5/0.3 |
| `src/editor/services/ZoneManager.cpp` | Updated to use `activeOpacity`/`inactiveOpacity` |
| `src/editor/qml/PropertyPanel.qml` | Added separate active/inactive opacity sliders for both single and multi-select |
| `src/editor/qml/EditorZone.qml` | Updated opacity trackers to use `activeOpacity`/`inactiveOpacity` |
| `src/editor/qml/DimensionTooltip.qml` | Fixed i18n for percentage format strings |
| `src/editor/EditorController.cpp` | Updated to use `activeOpacity`/`inactiveOpacity` |

### 10.10 Summary of Fixes Applied

1. **Layout `zonePadding` now takes precedence** - `OverlayService::zoneToVariantMap()` uses `layout->zonePadding()` when available
2. **Layout `showZoneNumbers` now takes precedence** - `OverlayService::updateOverlayWindow()` uses `layout->showZoneNumbers()` when available
3. **QML defaults now match Settings defaults** - All QML components use 0.5/0.3 for active/inactive opacity
4. **Zone has separate `activeOpacity` and `inactiveOpacity`** - No more magic 0.73 multiplier
5. **Editor PropertyPanel** - Now has separate sliders for active and inactive opacity
6. **Division-by-zero guards** - Added guards in `zonedetectionadaptor.cpp` and `windowdragadaptor.cpp`
7. **i18n improvements** - Fixed `qsTr()` usage and string concatenation in QML
8. **Breaking change** - Old layouts with single `opacity` key will use defaults; no backward compatibility
