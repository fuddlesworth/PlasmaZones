# PlasmaZones Logging Analysis

Analysis of log messages for consistency, gaps, and issues after migrating to the centralized logging utility (`src/core/logging.h`). **Main finding:** Category names (e.g. `plasmazones.daemon`) already identify the component, but many messages still include a class/component prefix (e.g. `Daemon:`, `ZoneManager:`), producing redundant output like:

```
plasmazones.daemon: Daemon: Set default active layout: Columns
```

---

## 1. Duplicate / Redundant Information

### 1.1 Category + class prefix

Qt’s default format is `category: message`. If the message starts with `ClassName:`, the category repeats the same information:

| Category | Redundant prefix in message | Example |
|----------|-----------------------------|---------|
| `plasmazones.daemon` | `Daemon:` | `Daemon: Set default active layout:` |
| `plasmazones.daemon.shortcuts` | `ShortcutManager:` | `ShortcutManager: Quick layout shortcut triggered` |
| `plasmazones.daemon.overlay` | `OverlayService:`, `ZoneSelectorController:` | `ZoneSelectorController: State changed to` |
| `plasmazones.editor` | `EditorController:`, `DBusLayoutService:`, `TemplateService:` | `EditorController: Zone not found for deletion:` |
| `plasmazones.editor.zone` | `ZoneManager:` | `ZoneManager: Empty zone ID for geometry update` |
| `plasmazones.editor.undo` | `UpdateZoneGeometryCommand:`, `SplitZoneCommand:`, etc. | `UpdateZoneGeometryCommand: Zone not found for undo:` |
| `plasmazones.editor.snapping` | `SnappingService:` | `SnappingService: Rejected non-finite geometry` |
| `plasmazones.dbus` | `LayoutAdaptor:`, `SettingsAdaptor:`, `WindowDragAdaptor:`, `ZoneDetectionAdaptor:`, `WindowTrackingAdaptor:`, `OverlayAdaptor:`, `ScreenAdaptor:` | `LayoutAdaptor: Layout not found:` |
| `plasmazones.dbus.layout` | `LayoutAdaptor:` | (same as above; `lcDbusLayout` used for LayoutAdaptor) |
| `plasmazones.core` | `VirtualDesktopManager:` | `VirtualDesktopManager: KWin D-Bus interface not available` |
| `plasmazones.core.layout` | `Layout::`, `LayoutManager` (implicit in some) | `Layout::recalculateZoneGeometries for` |
| `plasmazones.core.screen` | `ScreenManager:`, `ScreenManager::` | `ScreenManager::screens() called before QGuiApplication` |
| `plasmazones.core.zone` | `ZoneDetector:`, `ZoneDetector::` | `ZoneDetector: Layout set with N zones` |
| `plasmazones.config` | `Settings:` | `Settings: Loaded DragActivationModifier=` |

**Rule (from `docs/LOGGING_STANDARDS.md`):** *“Don’t: Redundant class name (category already shows this).”*  
**Fix:** Drop the `ClassName:` / `Class::` prefix from the message; the category is sufficient.

### 1.2 main.cpp: app name + category + “Daemon”

In `src/daemon/main.cpp`:

- `qCCritical(…::lcDaemon) << "PlasmaZones: Failed to initialize daemon"`
- `qCInfo(…::lcDaemon) << "PlasmaZones: Daemon started successfully"`
- `qCDebug(…::lcDaemon) << "PlasmaZones: Daemon already running - activation request ignored"`

`plasmazones.daemon` already implies “PlasmaZones daemon”. “PlasmaZones:” and “Daemon” are both redundant.

**Fix:** e.g.  
`"Failed to initialize daemon"`,  
`"Started successfully"`,  
`"Already running - activation request ignored"`.

---

## 2. Inconsistent Prefix Styles

Different styles are used in the message string:

| Style | Example | Where |
|-------|---------|-------|
| `Class:` | `ZoneManager: Zone not found` | Most |
| `Class::method` | `Layout::recalculateZoneGeometries`, `ScreenManager::screens()`, `ZoneDetectionAdaptor::detectZoneAtPosition` | layout, screenmanager, zonedetectionadaptor |
| `function:` | `getZonesSharingEdge: zone not found` | ZoneManager |
| `zonesToVariantList:` | `zonesToVariantList: encountered null zone` | zoneselectorcontroller |
| None | `"Invalid highlight color, using default"` | settings.cpp |

**Recommendation:** Standardize on **no** class/function prefix in the message. Use the category for component, and keep the message to the event/error and context (ids, values, paths).

---

## 3. Gaps: Components Not Using the Logging Utility

### 3.1 KWin effect (`kwin-effect/plasmazoneseffect.cpp`)

- Uses **raw `qDebug()` and `qWarning()`** only; no `qCDebug`/`qCWarning`/`qCInfo`/`qCCritical`.
- All messages are prefixed with `"PlasmaZonesEffect: "`.
- KWin effect does **not** link to `plasmazones_core`, so it cannot use `lcEffect` from `logging.h`.

**Impact:**  
- No `plasmazones.effect` in output; cannot filter effect logs via `QT_LOGGING_RULES`.  
- Inconsistent with the rest of the project.

**Options:**  
1. **Add a local category** in the effect, e.g. `Q_LOGGING_CATEGORY(lcEffect, "plasmazones.effect")` in the effect’s .cpp (and a small header or `Q_DECLARE_LOGGING_CATEGORY` if needed), and replace `qDebug`/`qWarning` with `qCDebug(lcEffect)`/`qCWarning(lcEffect)`.  
2. **Link the effect to `plasmazones_core`** and use `lcEffect` from `logging.h`. This may pull in more than needed; evaluate deps.

In both cases, **remove the `"PlasmaZonesEffect: "` prefix** from the message text.

### 3.2 KCM (`kcm/kcm_plasmazones.cpp`)

- Uses **raw `qWarning()`** only.
- KCM links to `plasmazones_core`, so it **can** use `logging.h` and e.g. an `lcKcm` or `lcConfig`-style category.

**Current messages:**

- `"KCM save: " << errorCount << "D-Bus call(s) failed..."`
- `"Failed to start plasmazonesd daemon"`
- `"Failed to get quick layout slots:" << ...`
- `"D-Bus call failed:" << interface << "::" << method << ...`
- `"KCM: Cannot assign layout - empty screen name"`
- `"KCM: Cannot clear assignment - empty screen name"`

**Gaps:**  
- No logging category; not filterable under `plasmazones.*`.  
- Inconsistent with the rest of the codebase.  
- Two messages use a `"KCM: "` prefix; the rest don’t.

**Recommendation:**  
- Add a category, e.g. `lcKcm` → `"plasmazones.kcm"` (and wire it in `logging.h`/`logging.cpp` if we keep categories centralized), or reuse something like `lcConfig` if KCM is considered “config” for logging.  
- Replace `qWarning()` with `qCWarning(lcKcm)` (or chosen category).  
- Drop `"KCM: "` from the message text.

---

## 4. Unused / Mismatched Categories

Defined in `src/core/logging.cpp` but **not used**:

- `lcDbusSettings` (`plasmazones.dbus.settings`) – `SettingsAdaptor` uses `lcDbus`.
- `lcDbusWindow` (`plasmazones.dbus.window`) – `WindowTrackingAdaptor` and `WindowDragAdaptor` use `lcDbus`.

So we have finer D-Bus categories defined but the corresponding adaptors log under the generic `lcDbus`.

**Recommendation:**  
- Either **use** `lcDbusSettings` in `SettingsAdaptor` and `lcDbusWindow` in `WindowTrackingAdaptor` and `WindowDragAdaptor`, **or**  
- Remove `lcDbusSettings` and `lcDbusWindow` if we prefer a single `lcDbus` for all non-layout D-Bus.  
- Document the chosen mapping in `LOGGING_STANDARDS.md`.

---

## 5. Message Pattern Not Set

`docs/LOGGING_STANDARDS.md` suggests:

```cpp
qSetMessagePattern("[%{time yyyy-MM-dd hh:mm:ss.zzz}] [%{type}] [%{category}] %{message}");
```

`qSetMessagePattern` (or `QT_MESSAGE_PATTERN`) is **not** set in:

- `src/daemon/main.cpp`
- `src/editor/main.cpp`
- KCM / KWin effect entrypoints

So we rely on Qt’s default format. The default includes the category, which is why `plasmazones.daemon: Daemon: ...` is so visible.

**Recommendation:**  
- If we want structured, consistent logs: set `qSetMessagePattern` in each `main()` (or via `QT_MESSAGE_PATTERN`) as in the doc.  
- Fixing redundant prefixes remains useful regardless.

---

## 6. Other Consistency Notes

### 6.1 `lcDbus` vs specific D-Bus categories

- **LayoutAdaptor:** `lcDbusLayout` ✓  
- **SettingsAdaptor, WindowDragAdaptor, ZoneDetectionAdaptor, WindowTrackingAdaptor, OverlayAdaptor, ScreenAdaptor:** `lcDbus`  
- **lcDbusSettings, lcDbusWindow:** defined, unused.

So D-Bus logging is only split for “layout”; everything else is `lcDbus`. Either fully adopt `lcDbusSettings` / `lcDbusWindow` or treat the current split as intentional and document it.

### 6.2 OverlayService / ZoneSelectorController

Both use `lcOverlay` (`plasmazones.daemon.overlay`). That’s fine; the category doesn’t need to distinguish them. For consistency, drop `OverlayService:` and `ZoneSelectorController:` from the message text.

### 6.3 Config / Settings

- `lcConfig` is used in `settings.cpp`.  
- Some messages have a `"Settings: "` prefix, others don’t (e.g. `"Invalid highlight color, using default"`).  

Dropping the `"Settings: "` prefix everywhere would match the “no redundant class name” rule.

### 6.4 `Layout::` and `ScreenManager::`

- `Layout::recalculateZoneGeometries` and `ScreenManager::screens()`, `ScreenManager::calculateAvailableGeometry`, etc.  
- `Class::method` is redundant with the category. Prefer e.g. `"recalculateZoneGeometries for ..."` or `"screens() called before QGuiApplication"` and rely on the category for component.

---

## 7. Summary of Recommended Fixes

| Issue | Action |
|-------|--------|
| **Redundant `ClassName:` in message** | Remove it; category is enough. |
| **`PlasmaZones: Daemon:` in main.cpp** | Use short messages, e.g. `"Failed to initialize daemon"`, `"Started successfully"`. |
| **`Class::method` in message** | Prefer a short, action-oriented phrase; drop `Class::`. |
| **KWin effect: raw qDebug/qWarning** | Introduce `plasmazones.effect` (local or via core) and `qCDebug`/`qCWarning`; remove `"PlasmaZonesEffect: "`. |
| **KCM: raw qWarning** | Use a category (e.g. `lcKcm`/`plasmazones.kcm`) and `qCWarning`; remove `"KCM: "`. |
| **lcDbusSettings / lcDbusWindow** | Use them in the corresponding adaptors, or delete and document. |
| **qSetMessagePattern** | Set in daemon and editor (and KCM/effect if desired), or document that we rely on default. |

---

## 8. Files to Touch (High Level)

- **Daemon:** `src/daemon/main.cpp`, `src/daemon/daemon.cpp`, `src/daemon/shortcutmanager.cpp`, `src/daemon/overlayservice.cpp`, `src/daemon/zoneselectorcontroller.cpp`
- **Core:** `src/core/layout.cpp`, `src/core/layoutmanager.cpp`, `src/core/screenmanager.cpp`, `src/core/zonedetector.cpp`, `src/core/activitymanager.cpp`, `src/core/virtualdesktopmanager.cpp`
- **D-Bus:** `src/dbus/layoutadaptor.cpp`, `src/dbus/settingsadaptor.cpp`, `src/dbus/windowdragadaptor.cpp`, `src/dbus/zonedetectionadaptor.cpp`, `src/dbus/windowtrackingadaptor.cpp`, `src/dbus/overlayadaptor.cpp`, `src/dbus/screenadaptor.cpp`
- **Editor:** `src/editor/EditorController.cpp`, `src/editor/services/ZoneManager.cpp`, `src/editor/services/DBusLayoutService.cpp`, `src/editor/services/TemplateService.cpp`, `src/editor/services/SnappingService.cpp`, `src/editor/undo/commands/*.cpp`
- **Config:** `src/config/settings.cpp`
- **KCM:** `kcm/kcm_plasmazones.cpp` (+ add `lcKcm` in `logging.h`/`logging.cpp` if centralized)
- **KWin effect:** `kwin-effect/plasmazoneseffect.cpp` (+ add local or shared `lcEffect` and optional `logging` dep)

---

## 9. Example Before / After

**Before (daemon):**

```text
plasmazones.daemon: Daemon: Set default active layout: Columns
plasmazones.daemon: Daemon: D-Bus service registered successfully: org.plasmazones at /Layout
```

**After:**

```text
plasmazones.daemon: Set default active layout: Columns
plasmazones.daemon: D-Bus service registered successfully: org.plasmazones at /Layout
```

**Before (EditorController, lcEditor):**

```text
plasmazones.editor: EditorController: Zone not found for deletion: {abc-123}
```

**After:**

```text
plasmazones.editor: Zone not found for deletion: {abc-123}
```

**Before (LayoutAdaptor, lcDbusLayout):**

```text
plasmazones.dbus.layout: LayoutAdaptor: Layout not found: abc-123
```

**After:**

```text
plasmazones.dbus.layout: Layout not found: abc-123
```

---

## 10. Implementation Status

The fixes above were applied across the codebase:

- **lcKcm** added; KCM uses `qCWarning(lcKcm)` and redundant `"KCM: "` removed.
- **Daemon** (`main.cpp`, `daemon.cpp`): removed `"PlasmaZones: "`, `"Daemon: "`; **shortcutmanager**, **overlayservice**, **zoneselectorcontroller**: removed class/function prefixes.
- **Core** (**layout**, **layoutmanager**, **screenmanager**, **zonedetector**, **virtualdesktopmanager**): removed `Class:`, `Class::` prefixes.
- **D-Bus**: **LayoutAdaptor**, **OverlayAdaptor**, **ScreenAdaptor**, **ZoneDetectionAdaptor** – removed prefixes; **SettingsAdaptor** and **WindowTrackingAdaptor** / **WindowDragAdaptor** switched to **lcDbusSettings** and **lcDbusWindow** and prefixes removed.
- **Editor**: **EditorController**, **ZoneManager**, **DBusLayoutService**, **TemplateService**, **SnappingService**, and all **undo commands** – removed class prefixes. **settings.cpp** – removed `"Settings: "`.
- **KWin effect**: added local `Q_LOGGING_CATEGORY(lcEffect, "plasmazones.effect")`, replaced `qDebug`/`qWarning` with `qCDebug(lcEffect)`/`qCWarning(lcEffect)`, removed `"PlasmaZonesEffect: "`.

---

*See `docs/LOGGING_STANDARDS.md` for severity levels, categories, and patterns.*
