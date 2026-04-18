# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones Core Library and Daemon

include(GenerateExportHeader)

# QTP0004: All QML files are explicitly listed in qt_add_qml_module
qt_policy(SET QTP0004 NEW)

# ═══════════════════════════════════════════════════════════════════════════════
# Shared QML Components (must be built first)
# ═══════════════════════════════════════════════════════════════════════════════
add_subdirectory(shared)
add_subdirectory(compositor-common)

# Wayland layer-shell protocol code generation moved to PhosphorShell library.
# PlasmaZones uses PhosphorShell::LayerSurface via compatibility wrappers in core/.

# Core library sources
set(plasmazones_core_SRCS
    pz_qml_i18n.h
    core/constants.h
    core/logging.h
    core/logging.cpp
    core/translationloader.h
    core/translationloader.cpp
    # wallpaperprovider moved to PhosphorShell
    core/enums.h
    core/interfaces.h
    core/interfaces.cpp
    core/inavigationactions.h
    core/iwindowengine.h
    core/settings_interfaces.h
    core/types.h
    core/utils.h
    core/utils.cpp
    core/layoutfactory.h
    core/layoutfactory.cpp
    core/windowtrackingservice.h
    core/windowtrackingservice.cpp
    core/windowtrackingservice/snap.cpp
    core/windowtrackingservice/navigation.cpp
    core/windowtrackingservice/resnap.cpp
    core/windowtrackingservice/lifecycle.cpp
    core/windowregistry.h
    core/windowregistry.cpp
    core/platform.cpp
    core/platform.h
    core/geometryutils.cpp
    core/geometryutils/constraints.cpp
    core/geometryutils/overlaps.cpp
    core/geometryutils.h
    core/spatialadjacency.h
    core/virtualscreenswapper.cpp
    core/virtualscreenswapper.h
    core/screenmoderouter.cpp
    core/screenmoderouter.h
    core/dbusvariantutils.cpp
    core/dbusvariantutils.h
    core/zone.cpp
    core/zone.h
    core/layout.cpp
    core/layout/serialization.cpp
    core/layout/factories.cpp
    core/layout.h
    core/layoutmanager.cpp
    core/layoutmanager/persistence.cpp
    core/layoutmanager/assignments.cpp
    core/layoutmanager.h
    core/layoutworker/layoutworker.cpp
    core/layoutworker/layoutworker.h
    core/layoutworker/layoutcomputeservice.cpp
    core/layoutworker/layoutcomputeservice.h
    core/layoutworker/types.h
    core/layoututils.cpp
    core/layoututils.h
    core/supportreport.cpp
    core/supportreport.h
    # layersurface + shaderregistry/params moved to PhosphorShell
    core/single_instance_service.cpp
    core/single_instance_service.h
    core/screen_resolver.cpp
    core/screen_resolver.h
    core/screenmanager.cpp
    core/screenmanager/panels.cpp
    core/screenmanager/virtualscreens.cpp
    core/screenmanager.h
    core/virtualdesktopmanager.cpp
    core/virtualdesktopmanager.h
    core/activitymanager.cpp
    core/activitymanager.h
    core/zonedetector.cpp
    core/zonedetector.h
    core/zonehighlighter.cpp
    core/zonehighlighter.h
    core/shaderregistry.h  # PlasmaZones subclass (singleton + paths)
    # shaderregistry.cpp + shaderincluderesolver moved to PhosphorShell
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
    dbus/snapnavigationtargets.cpp
    dbus/snapnavigationtargets.h
    dbus/windowtrackingadaptor.cpp
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
    dbus/screenadaptor.cpp
    dbus/screenadaptor.h
    dbus/windowdragadaptor.cpp
    dbus/windowdragadaptor/drag.cpp
    dbus/windowdragadaptor/drag_protocol.cpp
    dbus/windowdragadaptor/drop.cpp
    dbus/windowdragadaptor.h
    dbus/autotileadaptor.cpp
    dbus/autotileadaptor/config.cpp
    dbus/autotileadaptor.h
    dbus/snapadaptor.cpp
    dbus/snapadaptor.h
    autotile/TilingAlgorithm.cpp
    autotile/TilingAlgorithm.h
    autotile/TilingState.cpp
    autotile/TilingStateSerialization.cpp
    autotile/TilingState.h
    autotile/SplitTree.cpp
    autotile/SplitTreeRebuild.cpp
    autotile/SplitTreeSerializer.cpp
    autotile/SplitTree.h
    autotile/AlgorithmRegistry.cpp
    autotile/AlgorithmRegistry.h
    autotile/AutotileConfig.cpp
    autotile/AutotileConfig.h
    autotile/AutotileEngine.cpp
    autotile/AutotileEngine.h
    autotile/autotilenavigationadapter.cpp
    autotile/autotilenavigationadapter.h
    autotile/PerScreenConfigResolver.cpp
    autotile/PerScreenConfigResolver.h
    autotile/NavigationController.cpp
    autotile/NavigationController.h
    autotile/SettingsBridge.cpp
    autotile/SettingsBridge.h
    autotile/OverflowManager.cpp
    autotile/OverflowManager.h
    autotile/algorithms/ScriptedAlgorithm.cpp
    autotile/algorithms/ScriptedAlgorithm.h
    autotile/algorithms/ScriptedAlgorithmHelpers.cpp
    autotile/algorithms/ScriptedAlgorithmHelpers.h
    autotile/algorithms/ScriptedAlgorithmJsBuiltins.cpp
    autotile/algorithms/ScriptedAlgorithmJsBuiltins.h
    autotile/algorithms/ScriptedAlgorithmLoader.cpp
    # Builtin JS helpers compiled into the binary as Qt resources
    autotile/algorithms/builtins.qrc
    autotile/algorithms/ScriptedAlgorithmLoader.h
    autotile/algorithms/ScriptedAlgorithmSandbox.cpp
    autotile/algorithms/ScriptedAlgorithmSandbox.h
    snap/SnapEngine.cpp
    snap/SnapEngine.h
    snap/snapnavigationadapter.cpp
    snap/snapnavigationadapter.h
    snap/snapengine/float.cpp
    snap/snapengine/lifecycle.cpp
    snap/snapengine/navigation.cpp
    snap/snapengine/navigation_actions.cpp
)

# Create core library
add_library(plasmazones_core SHARED ${plasmazones_core_SRCS})

target_sources(plasmazones_core PRIVATE pz_qml_i18n.cpp)

# Generate export header
generate_export_header(plasmazones_core
    EXPORT_FILE_NAME plasmazones_export.h
    EXPORT_MACRO_NAME PLASMAZONES_EXPORT
)

# Layer-shell: implementation moved to PhosphorShell library.
# The compatibility wrappers (core/qpa/*.h) are header-only — no sources to compile.
# PhosphorShell provides the QPA plugin, protocol code, and LayerSurface implementation.
set_source_files_properties(
    config/configdefaults.cpp
    # Exclude from unity build to avoid forward-declaration conflicts
    # between AutotileEngine.h (forward-declares Settings) and
    # SettingsBridge.h (uses QPointer<Settings>, needs full definition)
    dbus/windowdragadaptor/drag.cpp
    dbus/windowdragadaptor/drag_protocol.cpp
    dbus/windowdragadaptor/drop.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)
# PhosphorShell headers are available via PUBLIC link dependency

target_link_libraries(plasmazones_core
    PUBLIC
        Qt6::Core
        # Qt6::Qml is PUBLIC because ScriptedAlgorithm.h exposes QJSEngine/QJSValue types
        Qt6::Qml
        Qt6::Quick
        Qt6::Gui
        Qt6::DBus
        plasmazones_compositor_common
        PhosphorConfig::PhosphorConfig
        PhosphorShell::PhosphorShell
    PRIVATE
        Wayland::Client
        Qt6::Concurrent
        Qt6::WaylandClientPrivate
)

# PhosphorShortcuts: plasmazones_core uses ONLY the IAdhocRegistrar header
# (consumed by WindowDragAdaptor); the daemon links the shared library
# binary (see target_link_libraries(plasmazonesd ...) below). Wire the
# headers via INTERFACE-only include — pulls no .so dependency into
# editor / settings builds that link plasmazones_core but never touch
# shortcuts.
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
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/compositor-common>
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
# Layer Shell Qt Plugin — now provided by PhosphorShell library
# ═══════════════════════════════════════════════════════════════════════════════
# The QPA plugin (phosphorshell-qpa) is built and installed by libs/phosphor-shell/CMakeLists.txt.
# PlasmaZones::registerLayerShellPlugin() forwards to PhosphorShell::registerLayerShellPlugin()
# which sets QT_WAYLAND_SHELL_INTEGRATION=phosphorshell.

# ═══════════════════════════════════════════════════════════════════════════════
# Rendering Library (ZoneShaderItem, shared between daemon and editor)
# ═══════════════════════════════════════════════════════════════════════════════
set(plasmazones_rendering_SRCS
    daemon/rendering/zoneshadercommon.h
    daemon/rendering/zoneuniformextension.h
    daemon/rendering/zoneshadernoderhi.h
    daemon/rendering/zoneshadernoderhi.cpp
    daemon/rendering/zoneshaderitem.h
    daemon/rendering/zoneshaderitem.cpp
    daemon/rendering/zoneshaderitem/setters.cpp
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

install(TARGETS plasmazones_rendering
    EXPORT PlasmaZonesTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

# Daemon executable
set(plasmazones_daemon_SRCS
    daemon/main.cpp
    daemon/vulkan_support.cpp
    daemon/daemon.cpp
    daemon/daemon/start.cpp
    daemon/daemon/signals.cpp
    daemon/daemon/navigation.cpp
    daemon/daemon/autotile.cpp
    daemon/daemon/osd.cpp
    daemon/daemon.h
    daemon/daemon/helpers.h
    daemon/daemon/macros.h
    daemon/cavaservice.cpp
    daemon/cavaservice.h
    daemon/overlayservice.cpp
    daemon/overlayservice/settings.cpp
    daemon/overlayservice/overlay.cpp
    daemon/overlayservice/overlay_data.cpp
    daemon/overlayservice/osd.cpp
    daemon/overlayservice/selector.cpp
    daemon/overlayservice/selector_update.cpp
    daemon/overlayservice/shader.cpp
    daemon/overlayservice/snapassist.cpp
    daemon/overlayservice.h
    daemon/overlayservice/internal.h
    daemon/windowthumbnailservice.cpp
    daemon/windowthumbnailservice.h
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
        PhosphorLayer::PhosphorLayer  # wlr-layer-shell surface lifecycle
        PhosphorShortcuts::PhosphorShortcuts  # ShortcutManager / Registry / backends
        Qt6::Widgets
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Concurrent  # Pre-load shader bake cache on registry refresh
        Qt6::ShaderTools  # QShaderBaker for ZoneShaderNodeRhi (runtime GLSL bake)
)

# Qt6::GuiPrivate (QRhi) for daemon's include of zoneshadernoderhi.h (warmShaderBakeCacheForPaths)
target_link_libraries(plasmazonesd PRIVATE Qt6::GuiPrivate Qt6::ShaderToolsPrivate)

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
    daemon/cavaservice.cpp
    daemon/cavaservice.h
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
        editor/qml/ShaderParameterSection.qml
        editor/qml/ShaderSettingsDialog.qml
        editor/qml/VisibilitySettingsDialog.qml
        editor/qml/LayoutSettingsDialog.qml
        editor/qml/SectionHeader.qml
        editor/qml/ParameterDelegate.qml
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
        plasmazones_shared_qml
        plasmazones_shared_qmlplugin  # Generated by qt_add_qml_module
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Widgets
        Qt6::DBus
)

# Qt6::GuiPrivate and Qt6::ShaderToolsPrivate for ZoneShaderItem (shader preview)
target_link_libraries(plasmazones-editor PRIVATE Qt6::GuiPrivate Qt6::ShaderToolsPrivate)

install(TARGETS plasmazones-editor DESTINATION ${KDE_INSTALL_BINDIR})

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

# KGlobalAccel component desktop file — provides display name + icon in
# System Settings > Shortcuts.  Filename must match applicationName ("plasmazonesd").
install(FILES
    daemon/plasmazonesd.desktop
    DESTINATION ${KDE_INSTALL_DATADIR}/kglobalaccel
)

# Snap Assist thumbnails: daemon captures via KWin ScreenShot2. When desktop matching fails
# (e.g. local install), set KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 in environment.
configure_file(
    daemon/99-plasmazones-screenshot.conf.in
    ${CMAKE_CURRENT_BINARY_DIR}/99-plasmazones-screenshot.conf
    @ONLY
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/99-plasmazones-screenshot.conf
    DESTINATION ${KDE_INSTALL_LIBDIR}/environment.d
)
