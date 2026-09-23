# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Phosphor core libraries. Every library here is usable without the
# Phosphor shell; the shell tier and the PlasmaZones tier both build on
# them. Each library ships its own CMakeLists, project() metadata and
# find_package config so it stays individually extractable when it
# graduates to a standalone repository. Add new core libraries under
# libs/ following the phosphor-<name> convention.
#
# Declaration order matters where a library find_package()s a sibling:
# the in-tree target must exist before the consumer configures, or the
# fallback resolves a stale installed copy.

add_subdirectory(libs/phosphor-identity)    # stable cross-process window identity (composite ids, app-id matching)
add_subdirectory(libs/phosphor-dbus)        # generic, service-agnostic D-Bus client utilities (Client, HasDBusStreaming)
add_subdirectory(libs/phosphor-protocol)    # D-Bus wire types and service constants, needed by phosphor-screens (Resolver endpoint defaults)
add_subdirectory(libs/phosphor-fsloader)    # filesystem-backed loader scaffolding (WatchedDirectorySet + DirectoryLoader) - Core-only, needed by phosphor-rules's store watcher
# phosphor-registry: generic Registry<T> + 5 factory interfaces + plugin/metadata
# loaders. Unconditional (third-party hosts + demos consume it without the shell).
# Moved ahead of phosphor-shaders: ShaderRegistry now composes Registry<T> +
# MetadataPackLoader<T>, so the in-tree target must exist before shaders configures
# (else find_package resolves a stale installed copy). Depends on phosphor-fsloader.
add_subdirectory(libs/phosphor-registry)    # generic Registry<T> + 5 factory interfaces + plugin loader
add_subdirectory(libs/phosphor-rules)  # unified window/context rule engine (MatchExpression, RuleEvaluator) - depends on phosphor-identity + phosphor-protocol + phosphor-fsloader
add_subdirectory(libs/phosphor-layout-api)  # shared layout-preview contract (LayoutPreview, ILayoutSource)
add_subdirectory(libs/phosphor-config)      # schema-driven configuration + migration runner - needed by phosphor-zones
add_subdirectory(libs/phosphor-shaders)     # shader-domain primitives (BaseUniforms, IUniformExtension, ShaderRegistry)
add_subdirectory(libs/phosphor-surface)     # surface-shader pack subsystem (SurfaceShaderContract/Effect/Registry) - depends on phosphor-shaders/registry/fsloader
add_subdirectory(libs/phosphor-pointer)     # pointer-shader pack subsystem (PointerShaderContract/Effect/Registry, PointerHistory) - depends on phosphor-shaders/registry/fsloader
add_subdirectory(libs/phosphor-wayland)       # Wayland layer-shell QPA plugin + LayerSurface - needed by phosphor-screens
# ext-idle-notify-v1. Built unconditionally (it used to sit behind
# BUILD_PHOSPHOR_SHELL) because the DAEMON now depends on it: the KWin effect
# pauses decoration animation while the session is idle, and idleness is a Wayland
# CLIENT concern the compositor serves rather than consumes, so the daemon has to
# watch for it and push the result to the effect. Only needs phosphor-wayland, which
# is already unconditional.
add_subdirectory(libs/phosphor-service-idle)  # Wayland idle monitoring + inhibition
add_subdirectory(libs/phosphor-geometry)    # pure geometry math (coordinate transforms, overlap, min-size) - needed by phosphor-screens (swapper directional selector)
add_subdirectory(libs/phosphor-screens)     # screen-topology + virtual-screen subdivision - needed by phosphor-zones
add_subdirectory(libs/phosphor-zones)       # zone primitives + LayoutManager (depends on phosphor-config IBackend, phosphor-screens ScreenIdentity)
add_subdirectory(libs/phosphor-context-resolver) # frozen-snapshot per-screen mode + disable/lock cascade façade (depends on phosphor-zones for AssignmentEntry::Mode)
add_subdirectory(libs/phosphor-scripting)   # generic embedded Luau host (engine, sandbox, watchdog, marshalling) — vendored extern/luau
add_subdirectory(libs/phosphor-tiles)       # auto-tiling algorithm primitives (TilingAlgorithm, AlgorithmRegistry, SplitTree)
add_subdirectory(libs/phosphor-rendering)   # GPU shader rendering via Qt RHI
add_subdirectory(libs/phosphor-layer)       # wlr-layer-shell surface lifecycle management
add_subdirectory(libs/phosphor-shell-patterns) # UI-pattern recipes (axis 2) built on PhosphorLayer::Role
add_subdirectory(libs/phosphor-shortcuts)   # global shortcut backends + domain-free Registry
# PlasmaZones consumes the org.phosphor.animation QML module via the
# daemon, the editor, and the phosphor-shell binary. The QML plugin's
# QtQuickClockManager directly constructs QtQuickClock, which is gated
# behind PHOSPHOR_ANIMATION_QUICK in phosphor-animation's CMakeLists.
# Without it, the QML plugin emits an unresolved-symbol link error in
# every PlasmaZones binary.
#
# FORCE so a stale cached OFF (from a prior standalone configure of
# libs/phosphor-animation in this build directory, or an explicit
# -DPHOSPHOR_ANIMATION_QUICK=OFF on the parent build) cannot silently
# turn into a broken build. Surface the override loudly so a user who
# DID pass -DPHOSPHOR_ANIMATION_QUICK=OFF understands their request
# was rejected — and why.
if(DEFINED CACHE{PHOSPHOR_ANIMATION_QUICK} AND NOT PHOSPHOR_ANIMATION_QUICK)
    message(WARNING
        "PHOSPHOR_ANIMATION_QUICK=OFF was requested but every PlasmaZones binary "
        "links the org.phosphor.animation QML plugin which requires QtQuickClock. "
        "Forcing PHOSPHOR_ANIMATION_QUICK=ON for this configure. "
        "To build without QtQuickClock, configure libs/phosphor-animation standalone.")
endif()
set(PHOSPHOR_ANIMATION_QUICK ON CACHE BOOL "Build QtQuickClock (required by every PlasmaZones binary linking PhosphorAnimationQmlplugin)" FORCE)
add_subdirectory(libs/phosphor-animation)   # unified animation library: motion runtime, QML module, shaders, layer
add_subdirectory(libs/phosphor-audio)       # audio spectrum provider (CAVA backend)
add_subdirectory(libs/phosphor-surfaces)    # managed surface lifecycle (engine, keep-alive, scope gen)
add_subdirectory(libs/phosphor-overlay)     # per-screen shell-host + slot mechanism (rides on layer/shell-patterns/surfaces/animation/screens)
# ───────────────────────────────────────────────────────────────────────
# Declared last, because the shell tier looks these up
# ───────────────────────────────────────────────────────────────────────
# Everything in this file is unconditional: the shell-tier libraries (theme,
# popout, ipc), the phosphorctl CLI and the phosphor-shell binary live in
# phosphor-shell-libs/ and phosphor-shell/, which the ROOT CMakeLists enters
# under BUILD_PHOSPHOR_SHELL. Nothing here is gated by that flag.
#
# Order still matters. phosphor-shell links PhosphorWorkspaces (backing the
# Workspaces QML singleton) and phosphor-workspaces links PhosphorEngine, so
# both must be declared before the root enters the shell tier. Declared after
# it, the in-tree targets would not exist when phosphor-shell looks for them,
# and its find_package fallback would silently resolve against a
# system-installed copy instead. Their own dependencies — identity, protocol,
# layout-api, geometry — are all declared further up.
add_subdirectory(libs/phosphor-engine)  # unified placement engine contracts + window registry (SHARED lib)
add_subdirectory(libs/phosphor-workspaces)  # virtual desktop / workspace management
add_subdirectory(libs/phosphor-tile-engine) # autotile placement engine (IAutotileSettings + engine code)
add_subdirectory(libs/phosphor-snap-engine) # zone-based snap placement engine
add_subdirectory(libs/phosphor-scroll-engine) # niri-style scrolling placement engine (strip model + engine)

add_subdirectory(libs/phosphor-placement)   # window placement state tracking
add_subdirectory(libs/phosphor-compositor)  # compositor-plugin SDK
add_subdirectory(libs/phosphor-control) # reusable Qt6/QML/Kirigami settings-app framework (page controllers, registry, chrome)

# Aggregate target for the moon workspace: `cmake --build build --target phosphor-tier`.
include(PhosphorTier)
phosphor_tier_target(phosphor-tier)
