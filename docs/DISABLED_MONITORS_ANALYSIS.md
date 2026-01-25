# Disabled Monitors Feature – Bug & Edge Case Analysis

## Summary
Per-monitor disable (overlay, zone picker, and snapping off) was implemented via `DisabledMonitors` (StringList of screen names). This document lists bugs, edge cases, inconsistencies, and .cursorrules findings.

---

## Bugs (to fix)

### 1. **Snap-to-zone when releasing on a disabled monitor**
**Location:** `WindowDragAdaptor::dragStopped`

**Issue:** If the user holds the zone modifier on an enabled monitor (zone highlighted), then moves to a disabled monitor and releases, the code still snaps to the zone on the enabled monitor. The window is moved to another screen.

**Expected:** Releasing on a disabled monitor should not snap; the window should stay where it is (or unsnap if it was snapped).

**Fix:** Only use overlay-based zone snap when the release screen is not disabled. Add a `useOverlayZone` (or equivalent) that is false when `screenAtPoint(capturedLastCursorX, capturedLastCursorY)` is disabled, and require it in the condition for the `!usedZoneSelector && !capturedSnapCancelled && !capturedZoneId.isEmpty()` block.

---

### 2. **Overlay/zone selector not hidden when a monitor is disabled via settings**
**Location:** `OverlayService::updateSettings`

**Issue:** If the overlay or zone selector is visible and the user disables a monitor in the KCM and applies, `updateSettings` runs but we do not hide overlay/zone-selector windows for screens that are *newly* disabled. Those windows can stay visible on that monitor until the next hide/show.

**Fix:** In `updateSettings`, after `setSettings(settings)`, iterate `m_overlayWindows` and `m_zoneSelectorWindows` and `hide()` (or equivalent) for any screen where `m_settings->isMonitorDisabled(screen->name())`.

---

## Edge cases (documented, optional to improve)

### 3. **All monitors disabled + “show on all monitors”**
**Location:** `OverlayService::show`, `OverlayService::showAtPosition`

**Behavior:** With `showOnAllMonitors == true` and every screen disabled, we never return early (no cursor-screen check). We set `m_visible = true` / `m_zoneSelectorVisible = true`, but the loops skip all screens, so no windows are shown. `hide()` / `hideZoneSelector()` still run and are safe.

**Assessment:** State is slightly odd (“visible” with no windows) but harmless. No change required.

---

### 4. **Stale names in `DisabledMonitors`**
**Location:** Config / `disabledMonitors`

**Behavior:** Unplugging a monitor does not remove its name from `DisabledMonitors`. If it is replugged with the same name, it stays disabled; if the name changes (e.g. another port), the old name never matches.

**Assessment:** Acceptable; stale names do not cause crashes or wrong behavior. Optional: prune in `load()` against current screen list.

---

### 5. **Overlay/zone-selector windows for later-disabled monitors**
**Location:** `OverlayService::updateGeometries`, `updateLayout` (flash), `updateSettings` (update path)

**Behavior:**  
- Overlay/zone-selector windows for a screen are not destroyed when that screen is disabled in settings; they stay in the hashes but are not shown in later `show()` / `showZoneSelector()`.  
- `updateGeometries` and `updateLayout`’s flash still run for those screens; `updateSettings` still calls `updateOverlayWindow` / `updateZoneSelectorWindow` for them.

**Assessment:** Minor unnecessary work and a small amount of retained resources until screen removal or restart. Optional: skip disabled screens in `updateGeometries`, flash, and `updateSettings`; and/or destroy overlay/zone-selector for a screen when it becomes disabled in `updateSettings`.

---

### 6. **Zone state when moving from enabled to disabled during drag**
**Location:** `WindowDragAdaptor::handleSingleZoneModifier`, `handleMultiZoneModifier`

**Behavior:** When the cursor moves to a disabled screen, we return early and do not clear `m_currentZoneId`, highlights, etc. The overlay on enabled screens still shows the last zone. Bug #1 covers the main problem (snap on release). Clearing zone state on entering a disabled screen would be an alternative or addition; the chosen fix for #1 avoids snapping on release without requiring a new “clear zone only” helper.

**Assessment:** Handled by fix for #1. Optional: clear zone state when entering a disabled screen for clearer semantics.

---

### 7. **Keyboard/shortcut-driven moves and focus**
**Location:** Daemon/shortcuts (e.g. move window left/right, focus zone, quick layout)

**Behavior:** These paths were not changed. A window on a disabled monitor could still be moved or affected by shortcuts.

**Assessment:** Requirement was “overlay and zone picker don’t show” on disabled monitors; shortcuts were not in scope. Left as a possible follow-up.

---

### 8. **ZoneSelectorController**
**Location:** `ZoneSelectorController`

**Behavior:** `ZoneSelectorController` has its own `show()`/state and does not call `OverlayService::showZoneSelector()`. The per-monitor disable only affects the zone selector triggered from `WindowDragAdaptor::checkZoneSelectorTrigger` → `OverlayService::showZoneSelector()`.

**Assessment:** No change; different code path. If `ZoneSelectorController` is bound to a specific screen, it could be updated later to respect `isMonitorDisabled` for that screen.

---

## Inconsistencies

### 9. **`m_settings` null checks**
**Location:** Multiple

**Findings:**
- `OverlayService::show` / `showAtPosition`: use `m_settings && m_settings->isMonitorDisabled(...)` – OK.
- `WindowDragAdaptor::handleMultiZoneModifier` / `handleSingleZoneModifier`: `(m_settings && m_settings->isMonitorDisabled(screen->name()))` – OK.
- `WindowDragAdaptor::checkZoneSelectorTrigger`: `m_settings->isMonitorDisabled(screen->name())` is only reached after `if (!m_settings || !m_settings->zoneSelectorEnabled()) return;`, so `m_settings` is non-null – OK.
- `WindowDragAdaptor::dragStopped` (zone selector block): `(!m_settings || !m_settings->isMonitorDisabled(...))` – OK.
- `OverlayService::handleScreenAdded`: `(!m_settings || !m_settings->isMonitorDisabled(screen->name()))` – OK.

**Assessment:** No inconsistency; null is handled appropriately where it matters.

---

### 10. **KCM: double emission of `disabledMonitorsChanged`**
**Location:** `KCMPlasmaZones::setMonitorDisabled`

**Behavior:** We call `m_settings->setDisabledMonitors(list)` (which emits `Settings::disabledMonitorsChanged`) and then `Q_EMIT disabledMonitorsChanged()` on the KCM. The KCM and daemon use different `Settings` instances, so no double delivery to a single listener. The KCM must emit for its `Q_PROPERTY(disabledMonitors)` NOTIFY.

**Assessment:** Acceptable; no change.

---

## .cursorrules compliance

### 11. **Settings workflow (kcfg, interfaces, settings, load/save/reset)**  
- kcfg: `Display` / `DisabledMonitors` (StringList), `PascalCase`, `<default></default>` – OK.  
- interfaces.h: `disabledMonitors()`, `setDisabledMonitors()`, `isMonitorDisabled()`, `disabledMonitorsChanged` – OK.  
- settings.h/cpp: Q_PROPERTY, getters/setter, load/save/reset – OK.  
- Key `DisabledMonitors` matches `settings.cpp` – OK.

### 12. **Signal emission**
- `Settings::setDisabledMonitors`: emits only when `m_disabledMonitors != screenNames` – OK.  
- `KCMPlasmaZones::setMonitorDisabled`: only calls `m_settings->setDisabledMonitors` when the list actually changes – OK.

### 13. **i18n**
- QML: `i18n("Disable PlasmaZones on this monitor")` and tooltip – OK.  
- kcfg `<label>` is for schema; no i18n required there – OK.

### 14. **Qt6 string handling**
- No `QJsonObject` or `const char*`→`QString` misuse in the new code – OK.

### 15. **Naming**
- `disabledMonitors`, `setDisabledMonitors`, `isMonitorDisabled`, `disabledMonitorsChanged` – match project style – OK.

---

## Recommended fixes (minimal)

1. **Bug #1:** In `WindowDragAdaptor::dragStopped`, compute `useOverlayZone = false` when the release screen is disabled, and require `useOverlayZone` in the overlay-zone snap condition.  
2. **Bug #2:** In `OverlayService::updateSettings`, after `setSettings(settings)`, hide overlay and zone-selector windows for any screen that `m_settings->isMonitorDisabled(screen->name())`.

---

## Optional improvements

- Skip disabled screens in `updateGeometries`, in `updateLayout`’s flash, and in the update loops in `updateSettings` (and optionally destroy overlay/zone-selector for newly disabled screens).  
- Prune `DisabledMonitors` in `load()` using the current screen list.  
- Consider keyboard/shortcut behavior for windows on disabled monitors.  
- Consider `ZoneSelectorController` if it is ever bound to a specific monitor.
