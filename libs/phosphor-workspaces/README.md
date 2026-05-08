<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-workspaces

> Virtual desktop and activity management for compositors. Tracks KWin
> virtual desktops via D-Bus and KDE Activities via KActivities/PlasmaActivities.

## Responsibility

Compositors and window managers need to know which virtual desktop is
active and react to desktop/activity switches. This library provides that
state independently of the PlasmaZones daemon — any consumer that needs
workspace awareness links it directly.

## Key types

| Type | Purpose |
|------|---------|
| `VirtualDesktopManager` | Tracks KWin virtual desktops via D-Bus — current desktop, count, names, UUID mapping |
| `ActivityManager` | Tracks KDE Activities via KActivities/PlasmaActivities (optional compile-time dep) |

## Usage

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

## Link dependencies

```cmake
target_link_libraries(my_target PRIVATE
    PhosphorWorkspaces::PhosphorWorkspaces
)
```

## Optional dependencies

KActivities/PlasmaActivities is detected at CMake time. If absent,
`ActivityManager` compiles but `isAvailable()` returns false and all
activity queries return empty results. Virtual desktop tracking works
regardless.
