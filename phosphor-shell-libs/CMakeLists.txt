# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Phosphor shell libraries: the theme, popout and IPC foundation, the
# Phosphor.* QML surfaces, and the per-domain system service libraries.
# Entered from the root CMakeLists only when BUILD_PHOSPHOR_SHELL is ON, so
# the per-subdirectory `if(BUILD_PHOSPHOR_SHELL)` blocks below are redundant
# rather than load-bearing. They are kept as belt-and-braces so each entry
# reads as declaring its own precondition; do not take their presence as
# evidence that this file is reachable with the flag off.
#
# Order matters: phosphor-shell links theme/popout/ipc, so they precede it.
# icontheme is consumed by sni and notifications, so it configures first
# among the services.

add_subdirectory(libs/phosphor-theme)   # M3 color-token store + Phosphor.Theme QML singletons
add_subdirectory(libs/phosphor-popout)  # central popout coordinator (lifetime, focus, scope arbitration)
add_subdirectory(libs/phosphor-ipc)     # JSON-over-Unix-socket IpcRouter + IpcTarget QML type
add_subdirectory(libs/phosphor-shell)   # shell infrastructure and QML types
add_subdirectory(libs/phosphor-shell-widgets)  # Phosphor.Widgets M3 atom library (Phase 3.1); depends on phosphor-theme
add_subdirectory(libs/phosphor-shell-osd)      # Phosphor.OSD on-screen-display framework (Phase 3.3); depends on theme + widgets
add_subdirectory(libs/phosphor-shell-notifications) # Phosphor.Notifications toast framework (Phase 3.4); depends on theme + widgets
add_subdirectory(libs/phosphor-shell-control-center) # Phosphor.ControlCenter tile surface (Phase 4.4); depends on theme + widgets + Kirigami
add_subdirectory(libs/phosphor-shell-launcher)       # Launcher core: ILauncherProvider implementations, fzf-style matcher, .desktop scanner (Phase 4.2); depends on registry
add_subdirectory(libs/phosphor-shell-bar)      # Phosphor.Bar connected-corner bar surface (Phase 4.1). Depends on theme + widgets, and is one of the shell-tier libs that also require KF6 Kirigami (bar, power, control-center and launcher)
add_subdirectory(libs/phosphor-shell-power)    # Phosphor.Power session menu (Phase 4.6); depends on theme + widgets + Kirigami
add_subdirectory(libs/phosphor-shell-dashboard) # Phosphor.Dashboard: dashboard grid + keybind cheatsheet on the placement map (Phase 4); depends on theme + widgets + protocol
add_subdirectory(libs/phosphor-shell-picker)   # Phosphor.Picker wallpaper and theme strip (Phase 4); depends on theme + widgets
add_subdirectory(libs/phosphor-shell-polkit)   # Phosphor.Polkit authentication prompt (Phase 4); depends on theme + widgets
add_subdirectory(libs/phosphor-shell-lock)     # Phosphor.Lock lockscreen surface (Phase 4); depends on theme + widgets + Kirigami + phosphor-wayland (lock surfaces)

# Order: icontheme is consumed by sni (SNI publisher uses the
# image provider + theme resolver), so configure it first. The
# other Phase 2.0 siblings are independent. Phase 2.0 is complete:
# the phosphor-services umbrella has been dissolved into these
# per-domain libraries.
add_subdirectory(libs/phosphor-service-icontheme) # XDG icon-theme resolver + Qt image provider
add_subdirectory(libs/phosphor-service-sni)       # StatusNotifierItem host + watcher + dbusmenu
add_subdirectory(libs/phosphor-service-upower)    # UPower battery / power-supply readouts
add_subdirectory(libs/phosphor-service-mpris)     # MPRIS2 media-player discovery + control
add_subdirectory(libs/phosphor-service-pipewire)  # PipeWire mixer (Phase 2.1, shipped)
add_subdirectory(libs/phosphor-service-network)   # NetworkManager devices / connectivity (Phase 2.2)
add_subdirectory(libs/phosphor-service-bluetooth) # BlueZ adapters / devices / pairing (Phase 2.3)
add_subdirectory(libs/phosphor-service-brightness) # display / keyboard backlight brightness (Phase 2.4)
add_subdirectory(libs/phosphor-service-notifications) # org.freedesktop.Notifications server (Phase 2.5)
add_subdirectory(libs/phosphor-service-polkit)        # PolicyKit authentication agent (Phase 2.6)
add_subdirectory(libs/phosphor-service-clipboard)     # Wayland clipboard history (data-control) (Phase 2.8)
add_subdirectory(libs/phosphor-service-lock)          # PAM auth + ext-session-lock-v1 coordination (Phase 2.9)
add_subdirectory(libs/phosphor-service-session)       # logind session/power actions + inhibitors (Phase 2.10)

# Example demos and CLI acceptance harnesses are not part of the installed
# product. The Nix flake builds from a source tree with examples/ stripped
# (flake.nix removes this directory from the fileset), so only descend into
# the example subdirectories when the directory is actually present.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
# Phosphor SDK groundwork demos. These link the gated Phase-1 libraries
# (phosphor-theme / popout / ipc), so they are gated to match: with
# BUILD_PHOSPHOR_SHELL OFF those targets do not exist and an unconditional
# add_subdirectory would fail the CMake generate step.
#   phosphor-theme-demo  — swatch-sheet viewer, hot-reloads the PaletteStore.
#   phosphor-theme-cli   — headless wallpaper → matugen → palette → template.
#   phosphor-popout-demo — popout arbitration harness (popout + theme).
#   phosphor-registry-demo — in-process IBarWidgetFactory registration
#                            (registry + theme).
#   phosphor-ipc-demo    — three IpcTargets driven via phosphorctl.
#   phosphor-widgets-kitchen-sink — every Phosphor.Widgets atom in its
#                            enabled/disabled states (widgets + theme).
#   phosphor-osd-demo — OSDHost overlay; the four built-in OSDs via
#                            Registry<IOSDFactory>, driven by phosphorctl.
#   phosphor-toast-demo — ToastHost overlay fed by a real NotificationServer
#                            (notify-send) + buttons; DND exercises the rules seam.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-theme-demo)
    add_subdirectory(examples/phosphor-theme-cli)
    add_subdirectory(examples/phosphor-popout-demo)
    add_subdirectory(examples/phosphor-registry-demo)
    add_subdirectory(examples/phosphor-ipc-demo)
    add_subdirectory(examples/phosphor-widgets-kitchen-sink)
    add_subdirectory(examples/phosphor-osd-demo)
    add_subdirectory(examples/phosphor-toast-demo)
    add_subdirectory(examples/phosphor-control-center-demo)
    add_subdirectory(examples/phosphor-launcher-demo)
endif()

# phosphor-perscreen-demo. Acceptance harness for the Phosphor.Shell
# PerScreen helper (Phase 1.5). Opens a small window per monitor;
# hot-plug a display → window appears. Unlike the foundation-lib demos
# above, this one links the gated PhosphorShell::PhosphorShellQml target
# (PerScreen lives in libs/phosphor-shell), so it must be gated too: when
# BUILD_PHOSPHOR_SHELL is OFF that target does not exist and an
# unconditional add_subdirectory fails the CMake generate step.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-perscreen-demo)
endif()

# phosphor-service-pipewire-cli. Acceptance harness for the
# phosphor-service-pipewire library (Phase 2.1). Headless driver
# exercising the read paths (list sinks/sources/streams, default),
# write paths (set-volume, mute/unmute), and metadata writes
# (set-default-sink/source) against a live PipeWire + WirePlumber
# daemon. Gated to match libs/phosphor-service-pipewire itself: when
# BUILD_PHOSPHOR_SHELL is OFF the lib target does not exist, so the
# CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-pipewire-cli)
endif()

# phosphor-service-network-cli. Acceptance harness for the
# phosphor-service-network library (Phase 2.2). Headless driver
# exercising the read paths (status, list-devices, list-connections,
# list-aps) and write paths (scan, connect) against a live
# org.freedesktop.NetworkManager. Gated like the lib: when
# BUILD_PHOSPHOR_SHELL is OFF the lib target does not exist, so the CLI
# can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-network-cli)
endif()

# phosphor-service-bluetooth-cli. Acceptance harness for the
# phosphor-service-bluetooth library (Phase 2.3). Headless driver exercising
# the read paths (status, list-adapters, list-devices) and write paths
# (power, scan, pair, connect, trust, remove) against a live org.bluez, with
# an interactive pairing agent. Gated like the lib: when BUILD_PHOSPHOR_SHELL
# is OFF the lib target does not exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-bluetooth-cli)
endif()

# phosphor-service-brightness-cli. Acceptance harness for the
# phosphor-service-brightness library (Phase 2.4). Headless driver exercising
# the read path (list, get) and write path (set) against real sysfs backlights
# and logind. Gated like the lib: when BUILD_PHOSPHOR_SHELL is OFF the lib
# target does not exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-brightness-cli)
endif()

# phosphor-service-notifications-cli. Acceptance harness for the
# phosphor-service-notifications library (Phase 2.5). Both a server (watch: own
# org.freedesktop.Notifications and log incoming Notify + close events) and a
# client (send / close / info via D-Bus). Gated like the lib: when
# BUILD_PHOSPHOR_SHELL is OFF the lib target does not exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-notifications-cli)
endif()

# phosphor-service-polkit-cli. Acceptance harness for the phosphor-service-polkit
# library (Phase 2.6). Runs as the session's PolicyKit authentication agent and
# answers PAM prompts (trigger with `pkexec true`). Gated like the lib: when
# BUILD_PHOSPHOR_SHELL is OFF the lib target does not exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-polkit-cli)
endif()

# phosphor-service-idle-cli. Acceptance harness for the phosphor-service-idle
# library (Phase 2.7). Reports idle support, logs each idle stage as it fires
# with a wall-clock timestamp, and can hold an inhibition for a fixed duration
# (--inhibit-for). Gated like the lib: when BUILD_PHOSPHOR_SHELL is OFF the lib
# target does not exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-idle-cli)
endif()

# phosphor-service-clipboard-cli. Acceptance harness for the
# phosphor-service-clipboard library (Phase 2.8). Watches the clipboard, lists
# the history, and copies an entry back. Gated like the lib: when
# BUILD_PHOSPHOR_SHELL is OFF the lib target does not exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-clipboard-cli)
endif()

# phosphor-service-lock-cli. Acceptance harness for the phosphor-service-lock
# library (Phase 2.9). Authenticates the user (PAM) and drives the session lock.
# Gated like the lib: when BUILD_PHOSPHOR_SHELL is OFF the lib target does not
# exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-lock-cli)
endif()

# phosphor-service-session-cli. Acceptance harness for the
# phosphor-service-session library (Phase 2.10). Drives the logind session
# capabilities + actions (status / lock / logout / suspend / ... / power-off).
# Gated like the lib: when BUILD_PHOSPHOR_SHELL is OFF the lib target does not
# exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-session-cli)
endif()

# phosphor-registry-plugin-demo. Sibling of phosphor-registry-demo
# that exercises the PluginLoader + hot-reload path. The third
# (CPU-meter) widget builds as a separate .so + manifest.json and
# is loaded via PluginLoader from a per-build plugin root. Links
# PhosphorTheme, so it is gated with the other SDK groundwork demos.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-registry-plugin-demo)
endif()
endif()

# Aggregate target for the moon workspace: `cmake --build build --target phosphor-shell-libs-tier`.
include(PhosphorTier)
phosphor_tier_target(phosphor-shell-libs-tier)
