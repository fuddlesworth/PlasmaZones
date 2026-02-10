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

# KConfigXT for settings (GENERATE_MOC ensures Q_OBJECT is processed)
kconfig_add_kcfg_files(plasmazones_settings_SRCS
    config/plasmazones.kcfgc
    GENERATE_MOC
)

# Core library sources
set(plasmazones_core_SRCS
    core/constants.h
    core/logging.h
    core/logging.cpp
    core/interfaces.h
    core/interfaces.cpp
    core/settings_interfaces.h
    core/types.h
    core/utils.h
    core/layoutfactory.h
    core/layoutfactory.cpp
    core/windowtrackingservice.h
    core/windowtrackingservice.cpp
    core/platform.cpp
    core/platform.h
    core/geometryutils.cpp
    core/geometryutils.h
    core/dbusvariantutils.cpp
    core/dbusvariantutils.h
    core/zone.cpp
    core/zone.h
    core/tilingalgorithm.cpp
    core/tilingalgorithm.h
    core/layout.cpp
    core/layout.h
    core/layoutmanager.cpp
    core/layoutmanager.h
    core/layoututils.cpp
    core/layoututils.h
    core/screenmanager.cpp
    core/screenmanager.h
    core/virtualdesktopmanager.cpp
    core/virtualdesktopmanager.h
    core/activitymanager.cpp
    core/activitymanager.h
    core/zonedetector.cpp
    core/zonedetector.h
    core/zonehighlighter.cpp
    core/zonehighlighter.h
    core/shaderregistry.cpp
    core/shaderregistry.h
    core/shaderincluderesolver.cpp
    core/shaderincluderesolver.h
    core/modifierutils.cpp
    core/modifierutils.h
    config/settings.cpp
    config/settings.h
    config/colorimporter.cpp
    config/colorimporter.h
    config/updatechecker.cpp
    config/updatechecker.h
    dbus/layoutadaptor.cpp
    dbus/layoutadaptor.h
    dbus/settingsadaptor.cpp
    dbus/settingsadaptor.h
    dbus/overlayadaptor.cpp
    dbus/overlayadaptor.h
    dbus/zonedetectionadaptor.cpp
    dbus/zonedetectionadaptor.h
    dbus/windowtrackingadaptor.cpp
    dbus/windowtrackingadaptor.h
    dbus/screenadaptor.cpp
    dbus/screenadaptor.h
    dbus/windowdragadaptor.cpp
    dbus/windowdragadaptor.h
    ${plasmazones_settings_SRCS}
)

# Create core library
add_library(plasmazones_core SHARED ${plasmazones_core_SRCS})

# Generate export header
generate_export_header(plasmazones_core
    EXPORT_FILE_NAME plasmazones_export.h
    EXPORT_MACRO_NAME PLASMAZONES_EXPORT
)

# LayerShellQt is required - no conditional needed

target_link_libraries(plasmazones_core
    PUBLIC
        Qt6::Core
        Qt6::Quick
        Qt6::Gui
        Qt6::DBus
        KF6::ConfigCore
        KF6::ConfigGui
        KF6::CoreAddons
        KF6::WindowSystem
        KF6::I18n
        KF6::ColorScheme
)

# Conditionally link PlasmaActivities/KActivities if available
# Plasma 6 uses Plasma::Activities, KF6 uses KF6::Activities, KF5 uses KActivities
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

# Link LayerShellQt (required for overlay and geometry sensor windows)
target_link_libraries(plasmazones_core
    PUBLIC
        LayerShellQt::Interface
)

target_include_directories(plasmazones_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
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

# Daemon executable
# Note: We need to add kcfg files here too because KConfigXT generates local symbols
# that can't be exported from plasmazones_core shared library.
kconfig_add_kcfg_files(plasmazones_daemon_kcfg_SRCS
    config/plasmazones.kcfgc
    GENERATE_MOC
)

set(plasmazones_daemon_SRCS
    daemon/main.cpp
    daemon/daemon.cpp
    daemon/daemon.h
    daemon/cavaservice.cpp
    daemon/cavaservice.h
    daemon/overlayservice.cpp
    daemon/overlayservice.h
    daemon/shortcutmanager.cpp
    daemon/shortcutmanager.h
    daemon/zoneselectorcontroller.cpp
    daemon/zoneselectorcontroller.h
    # Mode tracking and smart toggle (Unified Layout Model)
    daemon/modetracker.cpp
    daemon/modetracker.h
    daemon/unifiedlayoutcontroller.cpp
    daemon/unifiedlayoutcontroller.h
    # Shader rendering system (RHI only)
    daemon/rendering/zoneshadercommon.h
    daemon/rendering/zonelabeltexturebuilder.h
    daemon/rendering/zonelabeltexturebuilder.cpp
    daemon/rendering/zoneshadernodebase.h
    daemon/rendering/zoneshaderitem.h
    daemon/rendering/zoneshaderitem.cpp
    daemon/rendering/zoneshadernoderhi.h
    daemon/rendering/zoneshadernoderhi.cpp
    # KConfigXT-generated config class (for ConfigDefaults)
    ${plasmazones_daemon_kcfg_SRCS}
)

# QML resources for the daemon overlay
qt_add_resources(plasmazones_daemon_SRCS daemon/resources.qrc)

add_executable(plasmazonesd ${plasmazones_daemon_SRCS})

# LayerShellQt is required - no conditional needed

target_link_libraries(plasmazonesd
    PRIVATE
        plasmazones_core
        plasmazones_shared_qml
        plasmazones_shared_qmlplugin  # Generated by qt_add_qml_module
        Qt6::Widgets
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Concurrent  # Pre-load shader bake cache on registry refresh
        Qt6::ShaderTools  # QShaderBaker for ZoneShaderNodeRhi (runtime GLSL bake)
        KF6::DBusAddons
        KF6::GlobalAccel
        KF6::Notifications
)

# Qt6::GuiPrivate (QRhi) and Qt6::ShaderToolsPrivate (QShaderBaker) for zone overlay (required by root)
target_link_libraries(plasmazonesd PRIVATE Qt6::GuiPrivate Qt6::ShaderToolsPrivate)

# Link LayerShellQt (required)
target_link_libraries(plasmazonesd
    PRIVATE
        LayerShellQt::Interface
)

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
# race conditions with KDE's session restore
install(FILES
    daemon/plasmazones.service
    DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR}
)

# Install KConfig settings file
install(FILES
    config/plasmazones.kcfg
    DESTINATION ${KDE_INSTALL_KCFGDIR}
)

# ═══════════════════════════════════════════════════════════════════════════════
# Layout Editor
# ═══════════════════════════════════════════════════════════════════════════════

set(plasmazones_editor_SRCS
    editor/main.cpp
    editor/EditorController.cpp
    editor/EditorController.h
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
        editor/qml/SnapIndicator.qml
        editor/qml/DimensionTooltip.qml
        editor/qml/EditorNotifications.qml
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
        plasmazones_shared_qml
        plasmazones_shared_qmlplugin  # Generated by qt_add_qml_module
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Widgets
        Qt6::DBus
        KF6::I18n
        KF6::I18nQml
        KF6::CoreAddons
        KF6::WindowSystem
)

# Link LayerShellQt (required)
target_link_libraries(plasmazones-editor
    PRIVATE
        LayerShellQt::Interface
)

install(TARGETS plasmazones-editor DESTINATION ${KDE_INSTALL_BINDIR})

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

# Desktop file for the daemon (for XDG portal registration, NoDisplay=true)
install(FILES
    daemon/org.plasmazones.daemon.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)
