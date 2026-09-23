# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones: the daemon, layout editor, settings app, KCM, KWin effect,
# developer tools, bundled data assets and the unit tests. Builds on the
# phosphor tier; nothing here links a shell-tier library.

# Source directories
add_subdirectory(src)

# Developer tools - opt-in via -DBUILD_TOOLS=ON.  Currently builds
# plasmazones-shader-render (offscreen shader preview generator
# used by the docs site).  Skipped by default so packagers don't
# pick up CLI tools they didn't ask for.
if(BUILD_TOOLS)
    add_subdirectory(tools)
endif()

option(BUILD_KWIN_EFFECT "Build KWin C++ effect plugin (needs KWin 6.7+; 6.7 and 6.8 both supported)" ON)
if(USE_KDE_FRAMEWORKS)
    if(BUILD_KWIN_EFFECT)
        add_subdirectory(kwin-effect)  # KWin C++ Effect (KDE-only)
    endif()
    add_subdirectory(kcm)          # KDE System Settings modules (KDE-only)
endif()

# Tests - enable with -DBUILD_TESTING=ON. The option is declared in the ROOT
# CMakeLists before KDECMakeSettings, which is what makes the OFF default
# win; declaring it here would be a no-op, because include(CTest) has
# already locked BUILD_TESTING into the cache by the time this file runs.
# This block just gates the test subdirectory.
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install D-Bus interface XMLs (one per interface). Keep in sync with
# dbus/*.xml on disk, third-party clients (and qdbus-qt6 introspection)
# resolve interfaces against these files post-install.
install(FILES
    dbus/org.plasmazones.Autotile.xml
    dbus/org.plasmazones.CompositorBridge.xml
    dbus/org.plasmazones.Control.xml
    dbus/org.plasmazones.EditorApp.xml
    dbus/org.plasmazones.LayoutRegistry.xml
    dbus/org.plasmazones.Overlay.xml
    dbus/org.plasmazones.Rules.xml
    dbus/org.plasmazones.Screen.xml
    dbus/org.plasmazones.Scrolling.xml
    dbus/org.plasmazones.Settings.xml
    dbus/org.plasmazones.SettingsApp.xml
    dbus/org.plasmazones.Shader.xml
    dbus/org.plasmazones.Snap.xml
    dbus/org.plasmazones.Tiling.xml
    dbus/org.plasmazones.WindowDrag.xml
    dbus/org.plasmazones.WindowTracking.xml
    dbus/org.plasmazones.ZoneDetection.xml
    DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR}
)

# Install default layouts
install(DIRECTORY data/layouts/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/layouts
    FILES_MATCHING PATTERN "*.json"
)

# Install bundled scrolling templates
install(DIRECTORY data/scrolling-templates/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/scrolling-templates
    FILES_MATCHING PATTERN "*.json"
)

# Settings embeds whatsnew.json as a Qt resource at build time, so nothing in
# this repo reads the installed copy. It ships anyway as a packaging
# convenience: the release notes a distro or a downstream tool wants to quote
# are here in one file, rather than inside the settings binary's resource
# bundle.
install(FILES data/whatsnew.json
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones
)

# Install shared named curves. Profile JSONs reference these by name
# (e.g. `"curve": "widget-out"`) so a single edit to a curve file
# retunes every profile that uses it, and users can drop their own
# JSON under ~/.local/share/plasmazones/curves/ to override a shipped
# curve system-wide. The daemon's CurveLoader scans this path first, so
# every later consumer resolves curve names against the merged set.
install(DIRECTORY data/curves/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/curves
    FILES_MATCHING PATTERN "*.json"
)

# Defensive cleanup: an upgrade from a build that shipped per-leaf JSONs
# (every commit before the family-seed migration) leaves stale files in
# the install dir that `cmake --install` does NOT remove on its own -
# install only adds/updates, never deletes orphans. Those stale leaves
# silently shadow the user's parent-node Settings edits via the
# deeper-leaf-wins overlay walk in PhosphorProfileRegistry::resolveWithInheritance,
# making "All Popups → 2000 ms" appear to do nothing. Wipe the install
# dir BEFORE the install(DIRECTORY) below copies new content so the
# post-install state always matches the source tree.
#
# SCRIPT runs at install time (`cmake --install`), not configure time,
# and runs in the install prefix the user actually invoked install with
# (DESTDIR-aware via $ENV{DESTDIR}). The wipe is bounded to our own
# `plasmazones/profiles` subdirectory - never to a directory we don't
# own - so an accidental wide install prefix can't cascade.
# Use KDE_INSTALL_FULL_DATADIR (already prefix-resolved by ECM) so we
# don't manually concatenate CMAKE_INSTALL_PREFIX with KDE_INSTALL_DATADIR:
# that join produces `/usr//absolute/...` when ECM hands us an absolute
# KDE_INSTALL_DATADIR (some distros override it that way).
install(CODE "
    set(_p_profiles_install_dir \"\$ENV{DESTDIR}${KDE_INSTALL_FULL_DATADIR}/plasmazones/profiles\")
    if(EXISTS \"\${_p_profiles_install_dir}\")
        file(GLOB _p_stale_profile_jsons \"\${_p_profiles_install_dir}/*.json\")
        if(_p_stale_profile_jsons)
            message(STATUS \"Removing stale animation profile JSONs from \${_p_profiles_install_dir}\")
            file(REMOVE \${_p_stale_profile_jsons})
        endif()
    endif()
")

# The data/profiles/ directory no longer ships per-leaf JSONs - animation
# timings/curves are driven entirely from the Settings UI via
# PhosphorProfileRegistry, with parent-chain inheritance filling in
# unset leaves. The install(DIRECTORY) rule and the build-time
# check-animation-profiles target were removed because there are no
# profile JSONs to install or validate. scripts/check-animation-profiles.py
# itself is still in the tree as a manual tool, and says so in its
# docstring; nothing invokes it. The install(CODE) stale-cleanup
# block above is intentionally kept for upgrades from older builds.

# Install bundled RenderNode shaders (raw GLSL, no compilation needed).
# Ship the whole pack tree so every asset a pack's metadata references
# travels with it, and exclude only hidden editor/OS/dev files (.DS_Store,
# .luaurc-style linter configs, VCS dotfiles). An extension allowlist here
# silently dropped any new asset type, or any JSON not named metadata.json.
install(DIRECTORY data/overlays/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/overlays
    PATTERN ".*" EXCLUDE
    # plasmazones-shader-validate --emit-preamble writes this
    # gitignored editor aid for ALL FOUR pack families, not just the
    # two that already excluded it. It carries no SPDX header, so
    # installing one after a local emit ships an unheadered file.
    PATTERN "p_generated.glsl" EXCLUDE
)

# The bundled overlay packs no longer ship preview.png card screenshots
# (the browser previews live shaders), but install(DIRECTORY) never removes
# files dropped from the tree, and the registry still adopts a lingering
# preview.png into a pack's previewPath on a manually-upgraded prefix.
# Same DESTDIR-aware, own-subdirectory-bounded cleanup pattern as the
# animation-profiles wipe above; one level deep, bundled pack dirs only.
install(CODE "
    set(_p_overlays_install_dir \"\$ENV{DESTDIR}${KDE_INSTALL_FULL_DATADIR}/plasmazones/overlays\")
    if(EXISTS \"\${_p_overlays_install_dir}\")
        file(GLOB _p_stale_overlay_previews \"\${_p_overlays_install_dir}/*/preview.png\")
        if(_p_stale_overlay_previews)
            message(STATUS \"Removing stale overlay preview.png files from \${_p_overlays_install_dir}\")
            file(REMOVE \${_p_stale_overlay_previews})
        endif()
    endif()
")

# Install bundled animation transition shader packs. The daemon's
# AnimationShaderRegistry scans ${XDG_DATA_DIRS}/plasmazones/animations
# at startup; user overrides at ~/.local/share/plasmazones/animations win.
# Ship the whole pack tree (metadata, shaders, the shared/ GLSL includes,
# and any texture a pack references) and exclude only hidden dev/OS files.
install(DIRECTORY data/animations/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/animations
    PATTERN ".*" EXCLUDE
    # plasmazones-shader-validate --emit-preamble writes this
    # gitignored editor aid for ALL FOUR pack families, not just the
    # two that already excluded it. It carries no SPDX header, so
    # installing one after a local emit ships an unheadered file.
    PATTERN "p_generated.glsl" EXCLUDE
)

# Install bundled surface shader packs (window border / rounded corners today;
# the third shader-pack category alongside shaders + animations). The kwin-effect
# discovers ${XDG_DATA_DIRS}/plasmazones/surface via the SurfaceShaderRegistry;
# user overrides at ~/.local/share/plasmazones/surface win.
# Ship the whole pack tree (metadata, shaders, the shared/ GLSL includes,
# and any texture a pack references) and exclude only hidden dev/OS files.
install(DIRECTORY data/surface/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/surface
    PATTERN ".*" EXCLUDE
    PATTERN "p_generated.glsl" EXCLUDE
)

# Install bundled pointer shader packs (pointer trails, halos, click ripples;
# the fourth shader-pack family). The kwin-effect and the settings preview
# discover ${XDG_DATA_DIRS}/plasmazones/pointer via the PointerShaderRegistry;
# user overrides at ~/.local/share/plasmazones/pointer win.
# Ship the whole pack tree (metadata, shaders, the shared/ GLSL includes,
# and any texture a pack references) and exclude only hidden dev/OS files.
install(DIRECTORY data/pointer/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/pointer
    PATTERN ".*" EXCLUDE
    PATTERN "p_generated.glsl" EXCLUDE
)

# Install bundled scripted tiling algorithms
install(DIRECTORY data/algorithms/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/algorithms
    FILES_MATCHING
    PATTERN "*.luau"
)

# Install support report script
install(PROGRAMS scripts/plasmazones-report.sh
    DESTINATION ${KDE_INSTALL_BINDIR}
    RENAME plasmazones-report
)

# Install icons (hicolor for dark themes, hicolor-light for light themes).
# The phosphor-shell.svg icons moved to phosphor-shell/CMakeLists.txt with
# the rest of that tier, where the whole file is already gated on
# BUILD_PHOSPHOR_SHELL, so nothing here is conditional.
set(_plasmazones_dark_icons
    icons/hicolor/scalable/apps/plasmazones.svg
    icons/hicolor/scalable/apps/plasmazones-editor.svg
    icons/hicolor/scalable/apps/plasmazones-settings.svg
)
set(_plasmazones_light_icons
    icons/hicolor-light/scalable/apps/plasmazones.svg
    icons/hicolor-light/scalable/apps/plasmazones-editor.svg
    icons/hicolor-light/scalable/apps/plasmazones-settings.svg
)
install(FILES ${_plasmazones_dark_icons}
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps
)
install(FILES ${_plasmazones_light_icons}
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor-light/scalable/apps
)

# Aggregate target for the moon workspace: `cmake --build build --target plasmazones-tier`.
include(PhosphorTier)
phosphor_tier_target(plasmazones-tier)

# The .qm catalogs are this tier's artifact, but the lrelease target cannot be
# collected automatically: cmake/PhosphorTranslations.cmake is include()d from
# the ROOT CMakeLists, so its targets belong to the root directory scope, and
# phosphor_tier_target() only walks from this directory down. Even relocating
# the include would not be enough — qt_add_lrelease() produces custom targets,
# which the collector skips by TYPE UTILITY. So attach it explicitly, or
# `moon run plasmazones:build` produces no catalogs and `moon run repo:install`
# then fails on install(FILES ${QM_FILES}) with nothing to install.
if(TARGET release_translations)
    add_dependencies(plasmazones-tier release_translations)
endif()
