# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later

# Build and Install Process Analysis

## Summary

This document analyzes the PlasmaZones build and install process, with focus on daemon autostart behavior, and recommends changes to align with common practice for system vs. user-local installs.

---

## 1. Current Install Layout

### 1.1 Daemon (`plasmazonesd`)

| What              | Destination                      | Purpose                                    |
|-------------------|----------------------------------|--------------------------------------------|
| Binary            | `KDE_INSTALL_BINDIR`             | `plasmazonesd` in PATH                     |
| Autostart .desktop| `KDE_INSTALL_AUTOSTARTDIR`       | Session autostart at login                  |
| App .desktop      | `KDE_INSTALL_APPDIR`             | Portal/D-Bus discovery (`NoDisplay=true`)   |

- **`KDE_INSTALL_AUTOSTARTDIR`** (from KDEInstallDirs): the exact path is platform-dependent and may be `share/autostart` or `etc/xdg/autostart` relative to the prefix. Typical results:
  - System prefix (`/usr`, `/usr/local`): e.g. `/usr/share/autostart` or `/etc/xdg/autostart` (in `XDG_DATA_DIRS` or `XDG_CONFIG_DIRS`)
  - User prefix (`~/.local`): e.g. `~/.local/share/autostart` (in `XDG_DATA_HOME`)

- The **same** `org.plasmazones.daemon.desktop` is installed to **both** `AUTOSTARTDIR` and `APPDIR`.

### 1.2 KCM and Override Behavior

- The KCM reads/writes **`~/.config/autostart/org.plasmazones.daemon.desktop`**.
- That path overrides (by XDG/Plasma rules) the copy in `share/autostart`.
- The KCM uses the `Hidden` key: `Hidden=true` → disabled, `Hidden=false` → enabled.
- If the override file does **not** exist, `isDaemonAutostart()` treats autostart as **enabled** (relying on the .desktop in `share/autostart`).

### 1.3 Other Installed Artifacts

- **Core library**: `KDE_INSTALL_LIBDIR`
- **Editor**: `KDE_INSTALL_BINDIR`, editor .desktop in `KDE_INSTALL_APPDIR`
- **KCM**: `KDE_INSTALL_QMLDIR`, `KDE_INSTALL_KCFGDIR`
- **KWin effect**: `KDE_INSTALL_PLUGINDIR` (kwin effects)
- **D-Bus XML**: `KDE_INSTALL_DBUSINTERFACEDIR`
- **Layouts, icons**: `KDE_INSTALL_DATADIR`, `KDE_INSTALL_ICONDIR`

---

## 2. Autostart Best-Practice Issues

### 2.1 Issue: Daemon Autostart for User-Local Installs

**Behavior today:**

- For **system** installs (e.g. `-DCMAKE_INSTALL_PREFIX=/usr`), the .desktop goes to `/usr/share/autostart`. That is standard: the distro/admin chose to install the app, and the daemon is offered as an autostart. Users can disable via the KCM, which writes `~/.config/autostart/...` with `Hidden=true`. This is correct.

- For **user-local** installs (e.g. `-DCMAKE_INSTALL_PREFIX=$HOME/.local`), the .desktop goes to **`~/.local/share/autostart`**. The session treats that as an autostart entry, so the daemon starts on every login. There is no override yet, so `isDaemonAutostart()` is true. Effect: **local install ⇒ daemon always autostarts**, until the user explicitly disables it in the KCM.

**Why this is a problem:**

- A local install is often used for development or a personal, non-system install. Automatically enabling session autostart can be surprising and harder to reason about (e.g. after `make install` into `~/.local`, the daemon runs on every future login).
- Common expectation: autostart is **opt-in** for user-local installs (e.g. enabled only after the user turns it on in the KCM), while for system installs it can remain **opt-out** (on by default, user can disable in KCM).

**Recommendation (implemented):**

- Introduce an option **`INSTALL_DAEMON_AUTOSTART`** (default `ON`).
- **Only** install the daemon .desktop into `KDE_INSTALL_AUTOSTARTDIR` when `INSTALL_DAEMON_AUTOSTART` is `ON`.
- For **system** installs: keep default `ON`; behavior stays as today.
- For **user-local** installs: recommend `-DINSTALL_DAEMON_AUTOSTART=OFF`. The daemon will not be in `share/autostart`; the user enables it in the KCM, which creates `~/.config/autostart/org.plasmazones.daemon.desktop` with `Hidden=false`, so autostart becomes opt-in.

### 2.2 Duplicate .desktop in AUTOSTARTDIR and APPDIR

- The **same** `org.plasmazones.daemon.desktop` is installed to:
  - `KDE_INSTALL_AUTOSTARTDIR` (session autostart)
  - `KDE_INSTALL_APPDIR` (for portal/D-Bus discovery)

- The .desktop has `NoDisplay=true`, so it does not show in application menus. Putting it in `APPDIR` is for D-Bus/portal association and similar; it does **not** cause autostart.

- **Conclusion:** Installing to both is acceptable and matches the two different roles (autostart vs. discovery). No change needed; only the `AUTOSTARTDIR` install is made conditional on `INSTALL_DAEMON_AUTOSTART`. The `APPDIR` install remains unconditional.

### 2.3 XDG / KDE Autostart Paths

- **XDG / Plasma:**  
  - `~/.config/autostart/` overrides system/user `share/autostart`.  
  - `share/autostart` under `$XDG_DATA_HOME` (e.g. `~/.local/share/autostart`) or `$XDG_DATA_DIRS` (e.g. `/usr/share/autostart`) is a standard, supported location.

- **KDEInstallDirs:** `KDE_INSTALL_AUTOSTARTDIR` may be `share/autostart` or `etc/xdg/autostart` under the prefix, depending on the ECM/KDE setup. Both are valid; the session will pick up .desktop files from the configured autostart dirs.

---

## 3. Uninstall

- **Step 1:** Removes all paths from `install_manifest.txt` (written by CMake `install()`).  
  - If we do **not** install to `AUTOSTARTDIR` (because `INSTALL_DAEMON_AUTOSTART=OFF`), that .desktop is never in the manifest, so uninstall correctly does not touch it.

- **Step 2:** Removes all known PlasmaZones files and directories (bin, lib, share, plugins, etc.) for full prefix coverage. No legacy or old-artifact cleanup (e.g. no `org.kde.plasmazones*`, no qfancyzones).

- **Steps 3–5:** Empty parent directories, system caches (icon, desktop, ldconfig), KDE caches.

- **User override:** `~/.config/autostart/org.plasmazones.daemon.desktop` is created by the KCM and is user configuration. It is **intentionally not** removed by uninstall.

---

## 4. Recommendations Implemented

| Item | Action |
|------|--------|
| Daemon autostart for local installs | Add `INSTALL_DAEMON_AUTOSTART` (default `ON`). Install to `AUTOSTARTDIR` only when `ON`. Document `-DINSTALL_DAEMON_AUTOSTART=OFF` for local installs. |
| Duplicate .desktop (AUTOSTARTDIR + APPDIR) | Keep both; only `AUTOSTARTDIR` is conditional. `APPDIR` stays for portal/D-Bus. |
| Uninstall | Manifest + known paths; no legacy/old-artifact cleanup. |

---

## 5. Optional Future Improvements

- **systemd user service:** In addition to (or instead of) XDG autostart, a `org.plasmazones.daemon.service` in `lib/systemd/user/` could allow session/ordering/restart control. This would be a larger change and is not required for the current autostart best-practice fix.

- **Uninstall when manifest is missing:** If `install_manifest.txt` is missing, uninstall could still remove a small set of “known” paths (e.g. `KDE_INSTALL_FULL_AUTOSTARTDIR/org.plasmazones.daemon.desktop` when `INSTALL_DAEMON_AUTOSTART` was used). This is optional and adds complexity; the main path is via the manifest.

---

## 6. References

- [XDG Autostart Specification](https://specifications.freedesktop.org/autostart-spec/autostart-spec-latest.html)
- [KDEInstallDirs (ECM)](https://api.kde.org/ecm/kde-module/KDEInstallDirs6.html)
- [KDE System Administration – Startup](https://userbase.kde.org/KDE_System_Administration/Startup)
