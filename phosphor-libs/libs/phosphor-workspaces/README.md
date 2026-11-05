<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-workspaces

> Virtual desktop and activity tracking. KWin virtual desktops via
> D-Bus; KDE Activities via KActivities / PlasmaActivities.

## Responsibility

Tracks the active virtual desktop and current Activity, with change
signals on switch. It is independent of the Phosphor daemon, so consumers
that need workspace awareness link this library directly.

## Key types

| Type | Purpose |
|------|---------|
| `VirtualDesktopManager` | Tracks KWin virtual desktops via D-Bus — current desktop, count, names, UUID mapping |
| `ActivityManager` | Tracks KDE Activities via KActivities/PlasmaActivities (optional compile-time dep) |

## Typical use

```cpp
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/ActivityManager.h>

PhosphorWorkspaces::VirtualDesktopManager vdm;
vdm.init();
vdm.start();
connect(&vdm, &VirtualDesktopManager::currentDesktopChanged, [](int desktop) {
    // React to desktop switch
});

PhosphorWorkspaces::ActivityManager activities;
activities.init();
activities.start();
// activities.currentActivity() — empty string if KActivities unavailable
```

## Dependencies

- `QtCore`, `QtDBus`
- KActivities / PlasmaActivities (optional, detected at CMake time;
  `ActivityManager::isAvailable()` returns false when absent and
  activity queries return empty results, while virtual desktop tracking
  works regardless).
