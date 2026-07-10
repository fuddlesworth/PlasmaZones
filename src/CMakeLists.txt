# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones Core Library and Daemon

include(GenerateExportHeader)

# QTP0004: All QML files are explicitly listed in qt_add_qml_module.
# Guarded — the policy exists only from Qt 6.8 and QT_MIN_VERSION is 6.6;
# unguarded, a 6.6/6.7 toolchain fatal-errors on the unknown policy.
# QT_KNOWN_POLICY_*, not if(POLICY ...): CMake's POLICY test only knows
# CMP* policies and is always false for QTP*.
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Shared QML Components (must be built first)
# ═══════════════════════════════════════════════════════════════════════════════
add_subdirectory(shared)

# Wayland layer-shell protocol code generation moved to PhosphorWayland library.
# PlasmaZones uses PhosphorWayland::LayerSurface via compatibility wrappers in core/.

# Core library sources
set(plasmazones_core_SRCS
    phosphor_qml_i18n.h
    core/constants.h
    core/baselinecleanup.h
    core/logging.h
    core/logging.cpp
    core/translationloader.h
    core/translationloader.cpp
    core/animationbootstrap.h
    core/animationbootstrap.cpp
    # wallpaperprovider moved to PhosphorShaders
    core/enums.h
    core/interfaces.h
    core/interfaces.cpp
    # ilayoutmanager / izonedetector shims removed - consumers include
    # <PhosphorZones/ILayoutManager.h> / <PhosphorZones/IZoneDetector.h>
    # directly.
    core/ioverlayservice.h
    core/isettings.h
    core/settings_interfaces.h
    core/types.h
    core/utils.h
    core/utils.cpp
    core/daemongeometryresolver.h
    core/daemongeometryresolver.cpp
    core/platform.cpp
    core/platform.h
    core/geometryutils.cpp
    core/geometryutils.h
    core/spatialadjacency.h
    # virtualscreenswapper.{h,cpp} lifted to libs/phosphor-screens/Swapper.h -
    # callers include <PhosphorScreens/Swapper.h> directly.
    core/screenmoderouter.cpp
    core/screenmoderouter.h
    core/crosssurfaceresolver.cpp
    core/crosssurfaceresolver.h
    core/dbusvariantutils.cpp
    core/dbusvariantutils.h
    # zone, layout, layoututils, zonedetector, zonehighlighter, LayoutRegistry,
    # AssignmentEntry, LayoutAssignmentKey live in libs/phosphor-zones/ -
    # consumers include <PhosphorZones/...> directly and construct the registry
    # with a borrowed PhosphorRules::RuleStore* + the
    # "plasmazones/layouts" subdir inline.
    core/unifiedlayoutlist.cpp
    core/unifiedlayoutlist.h
    common/layoutpreviewserialize.cpp
    common/layoutpreviewserialize.h
    common/screenidresolver.cpp
    common/screenidresolver.h
    common/layoutbundlebuilder.cpp
    common/layoutbundlebuilder.h
    core/supportreport.cpp
    core/supportreport.h
    # layersurface moved to PhosphorWayland; shaderregistry/params moved to PhosphorShaders
    core/single_instance_service.cpp
    core/single_instance_service.h
    # screen_resolver.{h,cpp} and screenmanager.{h,cpp} + screenmanager/
    # lifted to libs/phosphor-screens. Daemon consumers take
    # PhosphorScreens::ScreenManager* via constructor injection -
    # no service-locator intermediary.
    core/shaderregistry.h  # PlasmaZones subclass (singleton + paths)
    # shaderregistry.cpp + shaderincluderesolver moved to PhosphorShaders
    core/modifierutils.cpp
    core/modifierutils.h
    config/settings.cpp
    config/settings/loadsave.cpp
    config/settings/setters.cpp
    config/settings/perscreen.cpp
    config/settings.h
    config/configdefaults.cpp
    config/configbackends.h
    config/configbackends.cpp
    config/settingsconfigstore.cpp
    config/settingsconfigstore.h
    config/perscreenresolver.h
    config/perscreenresolver.cpp
    config/settingsschema.h
    config/settingsschema.cpp
    config/configmigration.h
    config/configmigration.cpp
    config/colorimporter.cpp
    config/colorimporter.h
    config/updatechecker.cpp
    config/updatechecker.h
    dbus/dbushelpers.cpp
    dbus/layoutadaptor.cpp
    dbus/layoutadaptor/assignment.cpp
    dbus/layoutadaptor/editor.cpp
    dbus/layoutadaptor.h
    dbus/settingsadaptor.cpp
    dbus/settingsadaptor.h
    dbus/shaderadaptor.cpp
    dbus/shaderadaptor.h
    dbus/compositorbridgeadaptor.cpp
    dbus/compositorbridgeadaptor.h
    dbus/controladaptor.cpp
    dbus/controladaptor.h
    dbus/overlayadaptor.cpp
    dbus/overlayadaptor.h
    dbus/zonedetectionadaptor.cpp
    dbus/zonedetectionadaptor.h
    dbus/ruleadaptor.cpp
    dbus/ruleadaptor.h
    # snapnavigationtargets moved to libs/phosphor-snap-engine/
    dbus/windowtrackingadaptor.cpp
    dbus/windowtrackingadaptor/enginewiring.cpp
    dbus/windowtrackingadaptor/navigation.cpp
    dbus/windowtrackingadaptor/float.cpp
    dbus/windowtrackingadaptor/persistence.cpp
    dbus/windowtrackingadaptor/saveload.cpp
    dbus/windowtrackingadaptor/persistenceworker.cpp
    dbus/windowtrackingadaptor/persistenceworker.h
    dbus/windowtrackingadaptor/snaprestore.cpp
    dbus/windowtrackingadaptor/convenience.cpp
    dbus/windowtrackingadaptor.h
    dbus/windowtrackingadaptor/internal.h
    dbus/windowdragadaptor.cpp
    dbus/windowdragadaptor/drag.cpp
    dbus/windowdragadaptor/drag_protocol.cpp
    dbus/windowdragadaptor/drop.cpp
    dbus/windowdragadaptor/dragactivation.cpp
    dbus/windowdragadaptor/dragactivation.h
    dbus/windowdragadaptor.h
    dbus/autotileadaptor.cpp
    dbus/autotileadaptor/config.cpp
    dbus/autotileadaptor.h
    dbus/snapadaptor.cpp
    dbus/snapadaptor/commit.cpp
    dbus/snapadaptor/snaprestore.cpp
    dbus/snapadaptor/navigation.cpp
    dbus/snapadaptor.h
    # TilingAlgorithm, TilingState, SplitTree, AlgorithmRegistry, scripted-algo
    # machinery live in libs/phosphor-tiles/. Consumers include canonical
    # <PhosphorTiles/...> headers directly; no forwarding shims remain.
    # AutotileEngine + supporting files moved to libs/phosphor-tile-engine/.
    # SnapEngine + SnapState + supporting files moved to libs/phosphor-snap-engine/.
)

# Create core library
add_library(plasmazones_core SHARED ${plasmazones_core_SRCS})

target_sources(plasmazones_core PRIVATE phosphor_qml_i18n.cpp)

# Generate export header
generate_export_header(plasmazones_core
    EXPORT_FILE_NAME plasmazones_export.h
    EXPORT_MACRO_NAME PLASMAZONES_EXPORT
)

# Layer-shell: implementation moved to PhosphorWayland library.
# The compatibility wrappers (core/qpa/*.h) are header-only - no sources to compile.
# PhosphorWayland provides the QPA plugin, protocol code, and LayerSurface implementation.

# All four TUs below are excluded from unity build:
#   configdefaults.cpp — Settings PCH includes that conflict with unity macros
#   drag.cpp / drag_protocol.cpp / drop.cpp — forward-declaration conflicts
#     with the d-bus-adaptor-generated extern declarations across these TUs
set_source_files_properties(
    config/configdefaults.cpp
    dbus/windowdragadaptor/drag.cpp
    dbus/windowdragadaptor/drag_protocol.cpp
    dbus/windowdragadaptor/drop.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)
# PhosphorShaders + PhosphorWayland headers are available via PUBLIC link dependencies

target_link_libraries(plasmazones_core
    PUBLIC
        Qt6::Core
        Qt6::Quick
        Qt6::Gui
        Qt6::DBus
        # PhosphorAnimation PUBLIC because Settings::animationProfile()
        # exposes PhosphorAnimation::Profile in the header - consumers
        # that #include <settings.h> need the core library for the
        # return type (Phase 4 sub-commit 6).
        PhosphorAnimation::PhosphorAnimation
        PhosphorConfig::PhosphorConfig
        PhosphorEngine::PhosphorEngine
        PhosphorGeometry::PhosphorGeometry
        PhosphorWorkspaces::PhosphorWorkspaces
        PhosphorPlacement::PhosphorPlacement
        PhosphorTileEngine::PhosphorTileEngine
        PhosphorSnapEngine::PhosphorSnapEngine
        PhosphorIdentity::PhosphorIdentity
        PhosphorLayoutApi::PhosphorLayoutApi
        PhosphorProtocol::PhosphorProtocol
        PhosphorScreens::PhosphorScreens
        PhosphorShaders::PhosphorShaders
        PhosphorWayland::PhosphorWayland
        PhosphorTiles::PhosphorTiles
        PhosphorZones::PhosphorZones
        # PhosphorCompositor: DecorationDefaults — the shared window-decoration
        # default constants ConfigDefaults delegates to (PUBLIC because
        # configdefaults.h includes the header).
        PhosphorCompositor::PhosphorCompositor
        # PhosphorSurface: DecorationProfileTree / DecorationProfile — the
        # per-surface decoration tree type. PUBLIC because configdefaults.h,
        # config/settings.h and core/isettings.h expose the type by value in
        # their public API (the decorationProfileTree accessors + the
        # ConfigDefaults baseline), so consumers linking plasmazones_core need
        # the include path + link edge.
        PhosphorSurface::PhosphorSurface
        # PhosphorRules: the unified LGPL rule engine. PUBLIC since
        # Phase 3 — config/settings.h owns a RuleStore for the per-mode
        # disable lists, the RuleAdaptor exposes the store on D-Bus, and
        # config/configmigration.cpp builds rules during the v3→v4 conversion,
        # so consumers of plasmazones_core need the include path + link edge.
        PhosphorRules::PhosphorRules
        # Frozen-snapshot per-screen mode + disable/lock resolver. PUBLIC
        # because D-Bus adaptors compiled into plasmazones_core
        # (windowdragadaptor / windowtrackingadaptor / snapadaptor) hold
        # `PhosphorContext::IContextResolver*` members; consumers linking
        # plasmazones_core need the include path + link edge to read those.
        PhosphorContextResolver::PhosphorContextResolver
    PRIVATE
        Wayland::Client
        Qt6::Concurrent
        Qt6::WaylandClientPrivate
)

# PhosphorShortcuts: plasmazones_core uses ONLY the Integration::IAdhocRegistrar header
# (consumed by WindowDragAdaptor); the daemon links the shared library
# binary (see target_link_libraries(plasmazonesd ...) below). Wire the
# headers via PRIVATE include — consumer-time only — so the header path
# is available when plasmazones_core's own translation units compile
# but is NOT re-exported to downstream targets that link
# plasmazones_core (editor / settings) and never touch shortcuts.
target_include_directories(plasmazones_core SYSTEM PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/libs/phosphor-shortcuts/include>
)

# Conditionally link PlasmaActivities/KActivities if available
if(PlasmaActivities_FOUND)
    # Check for Plasma 6 target first
    if(TARGET Plasma::Activities)
        target_link_libraries(plasmazones_core
            PUBLIC
                Plasma::Activities
        )
        target_compile_definitions(plasmazones_core PRIVATE HAVE_KACTIVITIES)
        message(STATUS "Linking PlasmaActivities via Plasma::Activities target")
    elseif(TARGET KF6::Activities)
        target_link_libraries(plasmazones_core
            PUBLIC
                KF6::Activities
        )
        target_compile_definitions(plasmazones_core PRIVATE HAVE_KACTIVITIES)
        message(STATUS "Linking PlasmaActivities via KF6::Activities target")
    else()
        # Fallback to library name lookup
        find_library(PLASMA_ACTIVITIES_LIBRARY NAMES PlasmaActivities KActivities)
        if(PLASMA_ACTIVITIES_LIBRARY)
            target_link_libraries(plasmazones_core
                PUBLIC
                    ${PLASMA_ACTIVITIES_LIBRARY}
            )
            target_compile_definitions(plasmazones_core PRIVATE HAVE_KACTIVITIES)
            message(STATUS "Linking PlasmaActivities via library: ${PLASMA_ACTIVITIES_LIBRARY}")
        else()
            message(WARNING "PlasmaActivities found but no linkable target/library available")
        endif()
    endif()
endif()

target_include_directories(plasmazones_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>  # For shared/ headers (virtualscreenid.h)
        $<INSTALL_INTERFACE:include/plasmazones>
)

set_target_properties(plasmazones_core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    EXPORT_NAME Core
)

# Precompiled headers for core library - dramatically reduces compile time
if(ENABLE_PCH)
    target_precompile_headers(plasmazones_core
        PRIVATE
            <QObject>
            <QString>
            <QList>
            <QVariant>
            <QRect>
            <QPoint>
            <QSize>
            <QDebug>
            <QJsonObject>
            <QJsonArray>
            <memory>
            <vector>
            <functional>
    )
endif()

install(TARGETS plasmazones_core
    EXPORT PlasmaZonesTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

# ═══════════════════════════════════════════════════════════════════════════════
# Layer Shell Qt Plugin - now provided by PhosphorWayland library
# ═══════════════════════════════════════════════════════════════════════════════
# The QPA plugin (phosphorwayland-qpa) is built and installed by libs/phosphor-wayland/CMakeLists.txt.
# PlasmaZones::registerLayerShellPlugin() forwards to PhosphorWayland::registerLayerShellPlugin()
# which sets QT_WAYLAND_SHELL_INTEGRATION=phosphorwayland.

# ═══════════════════════════════════════════════════════════════════════════════
# Rendering Library (ZoneShaderItem, shared between daemon and editor)
# ═══════════════════════════════════════════════════════════════════════════════
set(plasmazones_rendering_SRCS
    daemon/rendering/zoneentryscaffold.h
    daemon/rendering/zoneentryscaffold.cpp
    daemon/rendering/zoneshadernoderhi.h
    daemon/rendering/zoneshadernoderhi.cpp
    daemon/rendering/zoneshaderitem.h
    daemon/rendering/zoneshaderitem.cpp
    daemon/rendering/zoneshaderitem/setters.cpp
    daemon/rendering/surfaceshaderitem.h
    daemon/rendering/surfaceshaderitem.cpp
    daemon/rendering/zonelabeltexturebuilder.h
    daemon/rendering/zonelabeltexturebuilder.cpp
)

add_library(plasmazones_rendering SHARED ${plasmazones_rendering_SRCS})

generate_export_header(plasmazones_rendering
    EXPORT_FILE_NAME plasmazones_rendering_export.h
    EXPORT_MACRO_NAME PLASMAZONES_RENDERING_EXPORT
)

target_link_libraries(plasmazones_rendering
    PUBLIC
        plasmazones_core
        Qt6::Quick
        PhosphorRendering::PhosphorRendering
        # SurfaceShaderItem constructs a PhosphorSurfaceShaders::SurfaceUniformProfile
        # to drive the shared render node's surface UBO. Reachable transitively via
        # plasmazones_core (which PUBLIC-links it for DecorationProfileTree), but
        # named explicitly here because surfaceshaderitem.{h,cpp} depend on the
        # surface library's symbols directly.
        PhosphorSurface::PhosphorSurface
    PRIVATE
        Qt6::GuiPrivate
        Qt6::ShaderToolsPrivate
        Qt6::Svg
)

target_include_directories(plasmazones_rendering
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include/plasmazones>
)

# Versioned SONAME: mirrors plasmazones_core above. Without VERSION /
# SOVERSION the library installs as a bare libplasmazones_rendering.so
# with no SONAME; dpkg-shlibdeps then can't derive a package dependency
# from it ("can't extract name and version from library name"). Every
# other shipped library is versioned: keep this one consistent.
set_target_properties(plasmazones_rendering PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

install(TARGETS plasmazones_rendering
    EXPORT PlasmaZonesTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

# plasmazones_shaderpreview: shared zone-shader live-preview feed. Used by BOTH
# the editor and the settings app (each injects its own IShaderPreviewBackend);
# deliberately NOT linked by the daemon so the audio (CAVA) + preview deps stay
# out of the compositor service.
set(plasmazones_shaderpreview_SRCS
    shaderpreview/ishaderpreviewbackend.h
    shaderpreview/shaderpreviewcontroller.h
    shaderpreview/shaderpreviewcontroller.cpp
)

add_library(plasmazones_shaderpreview SHARED ${plasmazones_shaderpreview_SRCS})

generate_export_header(plasmazones_shaderpreview
    EXPORT_FILE_NAME plasmazones_shaderpreview_export.h
    EXPORT_MACRO_NAME PLASMAZONES_SHADERPREVIEW_EXPORT
)

target_link_libraries(plasmazones_shaderpreview
    PUBLIC
        plasmazones_core  # brings PhosphorShaders + PhosphorZones PUBLIC
        plasmazones_rendering  # ZoneLabelTextureBuilder + ZoneShaderItem
        PhosphorAudio::PhosphorAudio  # CAVA spectrum provider
        Qt6::Gui
)

target_include_directories(plasmazones_shaderpreview
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include/plasmazones>
)

set_target_properties(plasmazones_shaderpreview PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

install(TARGETS plasmazones_shaderpreview
    EXPORT PlasmaZonesTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

# Daemon executable
set(plasmazones_daemon_SRCS
    daemon/main.cpp
    daemon/vulkan_support.cpp
    daemon/daemon.cpp
    daemon/enginefactory.cpp
    daemon/daemon/start.cpp
    daemon/daemon/signals.cpp
    daemon/daemon/navigation.cpp
    daemon/daemon/autotile.cpp
    daemon/daemon/osd.cpp
    daemon/daemon.h
    daemon/daemon/helpers.h
    daemon/daemon/macros.h
    # Concrete daemon-side adapters bridging the existing services
    # (VirtualDesktopManager + ActivityManager + ScreenModeRouter + ISettings)
    # to the LGPL PhosphorContext::IContextResolver triple. Owned by Daemon
    # alongside m_contextResolver — see the wiring in daemon/daemon.h.
    daemon/contextresolverwiring.h
    daemon/contextresolverwiring.cpp
    daemon/overlayservice.cpp
    daemon/overlayservice/animation_config.cpp
    daemon/overlayservice/lifecycle.cpp
    daemon/overlayservice/settings.cpp
    daemon/overlayservice/overlay.cpp
    daemon/overlayservice/overlay_data.cpp
    daemon/overlayservice/osd.cpp
    daemon/overlayservice/priming.cpp
    daemon/overlayservice/rekey.cpp
    daemon/overlayservice/screens.cpp
    daemon/overlayservice/selector.cpp
    daemon/overlayservice/shellhost_bridge.cpp
    daemon/overlayservice/selector_update.cpp
    daemon/overlayservice/shader.cpp
    daemon/overlayservice/snapassist.cpp
    daemon/overlayservice.h
    daemon/overlayservice/internal.h
    daemon/snapassistthumbnailprovider.cpp
    daemon/snapassistthumbnailprovider.h
    # Zero-copy GPU thumbnail path (PLASMAZONES_DMABUF_THUMBNAILS): imports
    # dma-bufs handed from the kwin-effect as Vulkan-backed QSGTextures.
    daemon/dmabuftextureprovider.cpp
    daemon/dmabuftextureprovider.h
    daemon/dmabuffencewaiter.cpp
    daemon/dmabuffencewaiter.h
    # OpenGL-RHI fallback import (EGL dma-buf -> GL texture). Isolated TU so its
    # epoxy GL/EGL headers don't mix with the Vulkan + Qt RHI headers.
    daemon/dmabufglimport.cpp
    daemon/dmabufglimport.h
    daemon/shortcutmanager.cpp
    daemon/shortcutmanager.h
    daemon/zoneselectorcontroller.cpp
    daemon/zoneselectorcontroller.h
    # Mode tracking and smart toggle (Unified Layout Model)
    daemon/modetracker.cpp
    daemon/modetracker.h
    daemon/unifiedlayoutcontroller.cpp
    daemon/unifiedlayoutcontroller.h
    # Shader rendering: ZoneShaderItem from plasmazones_rendering
)

# QML resources for the daemon overlay
qt_add_resources(plasmazones_daemon_SRCS daemon/resources.qrc)

add_executable(plasmazonesd ${plasmazones_daemon_SRCS})

target_link_libraries(plasmazonesd
    PRIVATE
        plasmazones_core
        plasmazones_rendering
        plasmazones_shared_qml
        plasmazones_shared_qmlplugin  # Generated by qt_add_qml_module
        PhosphorAnimation::PhosphorAnimation  # motion runtime + QML module + shaders + layer
        PhosphorAnimationQmlplugin  # Generated by qt_add_qml_module (STATIC)
        PhosphorLayer::PhosphorLayer  # wlr-layer-shell surface lifecycle
        PhosphorShellPatterns::PhosphorShellPatterns  # axis-2 UI-pattern recipes
        PhosphorShortcuts::PhosphorShortcuts  # ShortcutManager / Registry / backends
        PhosphorAudio::PhosphorAudio  # audio spectrum provider (CAVA)
        PhosphorSurfaces::PhosphorSurfaces  # managed surface lifecycle (engine, keep-alive)
        PhosphorOverlay::PhosphorOverlay  # per-screen shell-host + slot mechanism (Phase 2 onward)
        # Frozen-snapshot per-screen mode + disable/lock cascade façade.
        # The daemon owns a `ContextResolver` over three local adapters
        # (DaemonWorkspaceStateAdapter / ScreenModeAdapter / SettingsGateAdapter
        # in daemon/contextresolverwiring.{h,cpp}) and lets D-Bus adaptors
        # + OverlayService borrow it via `Daemon::contextResolver()`.
        PhosphorContextResolver::PhosphorContextResolver
        # Daemon is headless: runs under QGuiApplication. No Qt6::Widgets
        # link: it was unused and pulled QtWidgets + xcb deps into a binary
        # that never instantiates a QWidget.
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Concurrent  # Pre-load shader bake cache on registry refresh
        Qt6::ShaderTools  # QShaderBaker for ZoneShaderNodeRhi (runtime GLSL bake)
)

# Qt6::GuiPrivate (QRhi) for daemon's include of zoneshadernoderhi.h (warmShaderBakeCacheForPaths)
target_link_libraries(plasmazonesd PRIVATE Qt6::GuiPrivate Qt6::ShaderToolsPrivate)

# Vulkan headers for the dma-buf thumbnail import (dmabuftextureprovider.cpp).
# Only the headers + function pointers are used (entry points are resolved via
# QVulkanInstance / vkGetDeviceProcAddr), so this is headers-only — but linking
# the imported target is the clean way to pick up the include dirs. REQUIRED:
# dmabuftextureprovider.cpp #includes <vulkan/vulkan.h> unconditionally and is
# always compiled into the daemon, so a missing Vulkan SDK must fail at configure
# time with a clear message rather than as an opaque header-not-found compile error.
find_package(Vulkan REQUIRED)
target_link_libraries(plasmazonesd PRIVATE Vulkan::Vulkan)

# libepoxy: EGL/GL entry points for the OpenGL-RHI dma-buf import fallback
# (daemon/dmabufglimport.cpp). Only that TU includes epoxy headers.
find_package(PkgConfig REQUIRED)
pkg_check_modules(Epoxy REQUIRED IMPORTED_TARGET epoxy)
target_link_libraries(plasmazonesd PRIVATE PkgConfig::Epoxy)

# Keep the epoxy TU out of the Unity (jumbo) build: epoxy/gl.h must be the
# first GL include and refuses to coexist with GL/gl.h, which jumbo-grouping
# with the Vulkan + Qt RHI sources would pull in. Standalone TU keeps its
# EGL/GL headers fully isolated.
set_source_files_properties(daemon/dmabufglimport.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

install(TARGETS plasmazonesd DESTINATION ${KDE_INSTALL_BINDIR})

# Separate precompiled headers for executables (different PIC/PIE settings than shared lib)
if(ENABLE_PCH)
    target_precompile_headers(plasmazonesd
        PRIVATE
            <QObject>
            <QString>
            <QList>
            <QVariant>
            <QDebug>
    )
endif()

# Systemd user service (preferred over autostart for proper session ordering)
# This ensures PlasmaZones starts AFTER plasma-workspace.target, avoiding
# race conditions with KDE's session restore.
# Uses configure_file to resolve the correct binary path for the install prefix.
configure_file(
    daemon/plasmazones.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/plasmazones.service
    @ONLY
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/plasmazones.service
    DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR}
)


# Daemon desktop file: use full Exec path so KWin's fetchRestrictedDBusInterfacesFromPid
# can match the process (it compares executable path to desktop Exec).
configure_file(
    daemon/org.plasmazones.daemon.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.plasmazones.daemon.desktop
    @ONLY
)

# D-Bus service activation: lets a client method-call wake the daemon on
# first contact without requiring the systemd user unit to be already
# running. Installed to ${KDE_INSTALL_DBUSSERVICEDIR}; install() rule is
# in the install block lower in this file.
configure_file(
    daemon/org.plasmazones.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.plasmazones.service
    @ONLY
)

# ═══════════════════════════════════════════════════════════════════════════════
# Phosphor Shell (opt-in via -DBUILD_PHOSPHOR_SHELL=ON at the top level)
# ═══════════════════════════════════════════════════════════════════════════════
# Gated alongside libs/phosphor-shell and the gated phosphor-service-*
# group (see the add_subdirectory block in the root CMakeLists.txt for
# the authoritative list). Without the flag those library targets
# don't exist, so the binary wouldn't link anyway. Wrapping the whole block also keeps
# `install(TARGETS phosphor-shell …)` from running when the target
# is absent.

if(BUILD_PHOSPHOR_SHELL)
    add_executable(phosphor-shell shell/main.cpp)

    target_link_libraries(phosphor-shell
        PRIVATE
            PhosphorShell::PhosphorShell
            PhosphorServiceIconTheme::PhosphorServiceIconTheme
            PhosphorServiceSni::PhosphorServiceSni
            PhosphorServiceUPower::PhosphorServiceUPower
            PhosphorServiceMpris::PhosphorServiceMpris
            PhosphorServicePipeWire::PhosphorServicePipeWire
            PhosphorServiceNetwork::PhosphorServiceNetwork
            PhosphorServiceBluetooth::PhosphorServiceBluetooth
            PhosphorServiceBrightness::PhosphorServiceBrightness
            PhosphorServiceNotifications::PhosphorServiceNotifications
            PhosphorServicePolkit::PhosphorServicePolkit
            PhosphorServiceIdle::PhosphorServiceIdle
            PhosphorServiceClipboard::PhosphorServiceClipboard
            PhosphorServiceLock::PhosphorServiceLock
            PhosphorServiceSession::PhosphorServiceSession
            PhosphorLayer::PhosphorLayer
            PhosphorWayland::PhosphorWayland
            PhosphorRendering::PhosphorRendering
            # Linked from examples/phosphor-shell: bakes the default shell.qml
            # tree into the binary as a qt-qml-module resource. Two effects:
            # (1) qmlcachegen / qmlsc run at build time (faster cold start,
            # typed bindings compile to C++); (2) when no user config exists
            # under XDG paths, ShellLoader falls back to qrc:/qt/qml/...
            # so the binary works without the optional DATADIR install.
            phosphor_shell_exampleplugin
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            Qt6::Qml
    )

    install(TARGETS phosphor-shell DESTINATION ${KDE_INSTALL_BINDIR})
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Layout Editor
# ═══════════════════════════════════════════════════════════════════════════════

set(plasmazones_editor_SRCS
    editor/main.cpp
    daemon/vulkan_support.cpp
    editor/EditorController.cpp
    editor/EditorLaunchController.cpp
    editor/EditorLaunchController.h
    editor/EditorAppAdaptor.cpp
    editor/EditorAppAdaptor.h
    editor/controller/gaps.cpp
    editor/controller/shader.cpp
    editor/controller/layout.cpp
    editor/controller/selection.cpp
    editor/controller/multiselect.cpp
    editor/controller/visibility.cpp
    editor/controller/clipboard.cpp
    editor/controller/zones.cpp
    editor/controller/zoneops.cpp
    editor/controller/settings.cpp
    editor/EditorController.h
    editor/helpers/BatchOperationScope.h
    editor/helpers/ZoneSerialization.cpp
    editor/helpers/ZoneSerialization.h
    editor/helpers/SettingsDbusQueries.cpp
    editor/helpers/SettingsDbusQueries.h
    editor/helpers/ShaderDbusQueries.cpp
    editor/helpers/ShaderDbusQueries.h
    editor/services/ILayoutService.h
    editor/services/DBusLayoutService.cpp
    editor/services/DBusLayoutService.h
    editor/services/ZoneManager.cpp
    editor/services/zonemanager/divider.cpp
    editor/services/zonemanager/zorder.cpp
    editor/services/zonemanager/serialization.cpp
    editor/services/ZoneManager.h
    editor/services/ZoneAutoFiller.cpp
    editor/services/ZoneAutoFiller.h
    editor/services/SnappingService.cpp
    editor/services/SnappingService.h
    editor/services/TemplateService.cpp
    editor/services/TemplateService.h
    editor/services/TemplateStrategy.h
    editor/undo/UndoController.cpp
    editor/undo/UndoController.h
    editor/undo/commands/BaseZoneCommand.cpp
    editor/undo/commands/BaseZoneCommand.h
    editor/undo/commands/CommandId.h
    editor/undo/commands/AddZoneCommand.cpp
    editor/undo/commands/AddZoneCommand.h
    editor/undo/commands/DeleteZoneCommand.cpp
    editor/undo/commands/DeleteZoneCommand.h
    editor/undo/commands/UpdateZoneGeometryCommand.cpp
    editor/undo/commands/UpdateZoneGeometryCommand.h
    editor/undo/commands/UpdateZoneNameCommand.cpp
    editor/undo/commands/UpdateZoneNameCommand.h
    editor/undo/commands/UpdateZoneNumberCommand.cpp
    editor/undo/commands/UpdateZoneNumberCommand.h
    editor/undo/commands/UpdateZoneAppearanceCommand.cpp
    editor/undo/commands/UpdateZoneAppearanceCommand.h
    editor/undo/commands/DuplicateZoneCommand.cpp
    editor/undo/commands/DuplicateZoneCommand.h
    editor/undo/commands/SplitZoneCommand.cpp
    editor/undo/commands/SplitZoneCommand.h
    editor/undo/commands/FillZoneCommand.cpp
    editor/undo/commands/FillZoneCommand.h
    editor/undo/commands/DeleteZoneWithFillCommand.cpp
    editor/undo/commands/DeleteZoneWithFillCommand.h
    editor/undo/commands/DividerResizeCommand.cpp
    editor/undo/commands/DividerResizeCommand.h
    editor/undo/commands/ChangeZOrderCommand.cpp
    editor/undo/commands/ChangeZOrderCommand.h
    editor/undo/commands/ApplyTemplateCommand.cpp
    editor/undo/commands/ApplyTemplateCommand.h
    editor/undo/commands/ClearAllZonesCommand.cpp
    editor/undo/commands/ClearAllZonesCommand.h
    editor/undo/commands/UpdateLayoutNameCommand.cpp
    editor/undo/commands/UpdateLayoutNameCommand.h
    editor/undo/commands/ChangeSelectionCommand.cpp
    editor/undo/commands/ChangeSelectionCommand.h
    editor/undo/commands/BatchUpdateAppearanceCommand.cpp
    editor/undo/commands/BatchUpdateAppearanceCommand.h
    editor/undo/commands/PasteZonesCommand.cpp
    editor/undo/commands/PasteZonesCommand.h
    editor/undo/commands/UpdateShaderIdCommand.cpp
    editor/undo/commands/UpdateShaderIdCommand.h
    editor/undo/commands/UpdateShaderParamsCommand.cpp
    editor/undo/commands/UpdateShaderParamsCommand.h
    editor/undo/commands/UpdateGapOverrideCommand.cpp
    editor/undo/commands/UpdateGapOverrideCommand.h
    editor/undo/commands/UpdateFullScreenGeometryCommand.cpp
    editor/undo/commands/UpdateFullScreenGeometryCommand.h
    editor/undo/commands/ToggleGeometryModeCommand.cpp
    editor/undo/commands/ToggleGeometryModeCommand.h
    editor/undo/commands/UpdateFixedGeometryCommand.cpp
    editor/undo/commands/UpdateFixedGeometryCommand.h
    editor/undo/commands/UpdateVisibilityCommand.cpp
    editor/undo/commands/UpdateVisibilityCommand.h
)

# Create the executable first
add_executable(plasmazones-editor ${plasmazones_editor_SRCS})

# ColorUtils.js is a stateless utility imported explicitly by each QML file that uses it;
# skip its qmldir entry to avoid re-evaluation in every importer (Qt 6.8+ dev warning)
set_source_files_properties(editor/qml/ColorUtils.js PROPERTIES
    QT_QML_SKIP_QMLDIR_ENTRY TRUE
)

# Suppress -Wmissing-include-dirs for the QML module's Qt-managed AUTOMOC
# scratch dirs. KDECompilerSettings enables the warning globally, and
# qt_add_qml_module adds `<target>_autogen/include` to each generated
# sub-target's -I list; the no-Q_OBJECT sub-targets (qmltyperegistration,
# resource init) leave it empty or — after `--clean-first` — missing, so
# cc1plus fires before AUTOMOC recreates it. Set here (after the core /
# rendering / daemon targets above, which are unaffected). NOTE: directory
# COMPILE_OPTIONS apply to EVERY target created after this point in the
# scope — the editor QML sub-targets just below, but also
# plasmazones-shader-validate and the add_subdirectory(settings) tree
# further down. The suppression is harmless for those (it only silences
# the missing-AUTOMOC-include-dir warning class). Mirrors
# src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

# Add QML module to the executable
qt_add_qml_module(plasmazones-editor
    URI org.plasmazones.editor
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        editor/qml/EditorWindow.qml
        editor/qml/EditorZone.qml
        editor/qml/TopBar.qml
        editor/qml/PropertyPanel.qml
        editor/qml/ControlBar.qml
        editor/qml/DividerManager.qml
        editor/qml/DividerHandle.qml
        editor/qml/ResizeHandles.qml
        editor/qml/ZoneContent.qml
        editor/qml/ActionButtons.qml
        editor/qml/ZoneActionButton.qml
        editor/qml/ThemeHelpers.js
        editor/qml/SnapIndicator.qml
        editor/qml/DimensionTooltip.qml
        editor/qml/EditorNotifications.qml
        editor/qml/NotificationBanner.qml
        editor/qml/ZoneContextMenu.qml
        editor/qml/EditorShortcuts.qml
        editor/qml/KeyboardNavigation.qml
        editor/qml/ZoneDragHandler.qml
        editor/qml/ZoneFillAnimation.qml
        editor/qml/ZoneGeometrySync.qml
        editor/qml/GridOverlay.qml
        editor/qml/CanvasMouseHandler.qml
        editor/qml/ZoneOperations.qml
        editor/qml/HelpDialogContent.qml
        editor/qml/TemplatePreview.qml
        editor/qml/ShaderSettingsDialog.qml
        editor/qml/VisibilitySettingsDialog.qml
        editor/qml/LayoutSettingsDialog.qml
        editor/qml/SectionHeader.qml
        # SRP-extracted components from PropertyPanel
        editor/qml/ColorUtils.js
        editor/qml/ColorPickerRow.qml
        editor/qml/OpacitySliderRow.qml
        editor/qml/AppearanceSpinBox.qml
        editor/qml/ZoneColorDialog.qml
    )

target_link_libraries(plasmazones-editor
    PRIVATE
        plasmazones_core
        plasmazones_rendering
        plasmazones_shaderpreview  # shared zone-shader preview feed (delegated to)
        plasmazones_shared_qml
        plasmazones_shared_qmlplugin  # Generated by qt_add_qml_module
        PhosphorAnimation::PhosphorAnimation  # motion runtime + QML module
        PhosphorAnimationQmlplugin  # Generated by qt_add_qml_module (STATIC)
        PhosphorAudio::PhosphorAudio  # audio spectrum provider (CAVA)
        # Qt6::Widgets is REQUIRED even though src/editor has no direct QWidget
        # usage: the editor runs under QApplication and uses the org.kde.desktop
        # QtQuick Controls style (qqc2-desktop-style), which renders controls
        # through a QtWidgets QStyle. Dropping Widgets here and constructing a
        # QGuiApplication is what crashed third-party styles like Darkly on first
        # paint (discussion #262). The daemon stays QGuiApplication-only — it
        # uses no Kirigami/Controls — so it deliberately does NOT link Widgets.
        Qt6::Widgets
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::DBus
)

# Qt6::GuiPrivate and Qt6::ShaderToolsPrivate for ZoneShaderItem (shader preview)
target_link_libraries(plasmazones-editor PRIVATE Qt6::GuiPrivate Qt6::ShaderToolsPrivate)

install(TARGETS plasmazones-editor DESTINATION ${KDE_INSTALL_BINDIR})

# T1.2 — offline shader-pack validator. Parses metadata.json with the daemon's
# parser, reproduces the runtime GLSL assembly (entry scaffold + p_<id> preamble
# + includes) and bakes every stage through headless glslang (QShaderBaker — CPU
# only, no GPU/Wayland). The CI gate for the bundled sets; pre-commit-friendly for
# pack authors. Links plasmazones_rendering for the zone entry scaffold and
# PhosphorAnimation for the animation effect parser + entry scaffold.
add_executable(plasmazones-shader-validate
    shadervalidate/main.cpp
    shadervalidate/packvalidators.cpp
    shadervalidate/packvalidatorcommon.cpp
)
target_link_libraries(plasmazones-shader-validate
    PRIVATE
        plasmazones_rendering
        PhosphorRendering::PhosphorRendering
        PhosphorShaders::PhosphorShaders
        PhosphorAnimation::PhosphorAnimation
        PhosphorSurface::PhosphorSurface
        Qt6::Core
        Qt6::GuiPrivate
        Qt6::ShaderToolsPrivate
)
install(TARGETS plasmazones-shader-validate DESTINATION ${KDE_INSTALL_BINDIR})

# CI gate: every bundled pack must parse + bake clean. Headless glslang, so this
# runs in CI with no GPU/Wayland — exactly like luau-analyze for algorithms. Zone
# packs (data/shaders), animation packs (--animation, data/animations), and
# surface packs (--surface, data/surface) are distinct authoring models with
# different entry conventions, so each gets its own invocation.
if(BUILD_TESTING)
    add_test(NAME shader_validate_bundled
             COMMAND plasmazones-shader-validate --overlay ${PROJECT_SOURCE_DIR}/data/shaders)
    add_test(NAME shader_validate_animations
             COMMAND plasmazones-shader-validate --animation ${PROJECT_SOURCE_DIR}/data/animations)
    add_test(NAME shader_validate_surface
             COMMAND plasmazones-shader-validate --surface ${PROJECT_SOURCE_DIR}/data/surface)
endif()

# Standalone settings app (no KDE Frameworks required)
add_subdirectory(settings)

# Separate precompiled headers for executables (different PIC/PIE settings than shared lib)
if(ENABLE_PCH)
    target_precompile_headers(plasmazones-editor
        PRIVATE
            <QObject>
            <QString>
            <QList>
            <QVariant>
            <QRect>
            <QDebug>
            <memory>
    )
endif()

# Desktop file for the editor (org.kde naming for portal integration)
install(FILES
    editor/org.plasmazones.editor.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)

# Desktop file for the daemon (for XDG portal registration, NoDisplay=true).
# Uses configured .desktop with full Exec path for KWin screenshot auth.
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/org.plasmazones.daemon.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)

# D-Bus session activation file. Wakes the daemon on first bus call.
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/org.plasmazones.service
    DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR}
)

# AppStream metainfo: so Discover / GNOME Software / KDE store list the
# editor + settings apps with summary, screenshots, and content rating.
# Validated by `appstreamcli validate <file>` if `appstream` is installed.
install(FILES
    editor/org.plasmazones.editor.metainfo.xml
    settings/org.plasmazones.settings.metainfo.xml
    DESTINATION ${KDE_INSTALL_METAINFODIR}
)

# KGlobalAccel component desktop file - provides display name + icon in
# System Settings > Shortcuts.  Filename must match applicationName ("plasmazonesd").
install(FILES
    daemon/plasmazonesd.desktop
    DESTINATION ${KDE_INSTALL_DATADIR}/kglobalaccel
)

# Snap Assist thumbnails are now produced inside the kwin-effect via
# OffscreenQuickScene + org.kde.kwin's WindowThumbnail, so the daemon no
# longer needs ScreenShot2 access. The X-KDE-DBUS-Restricted-Interfaces
# entry and the environment.d/99-plasmazones-screenshot.conf override
# that gated that path are gone with the C++ removal.
