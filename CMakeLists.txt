# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones - KDE/Plasma FancyZones Implementation

cmake_minimum_required(VERSION 3.16)

project(PlasmaZones VERSION 1.12.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Build Performance Optimizations
# ═══════════════════════════════════════════════════════════════════════════════

# Unity builds - combine multiple source files into larger translation units
# This significantly reduces compilation time by reducing header parsing overhead
option(CMAKE_UNITY_BUILD "Enable unity builds for faster compilation" ON)
set(CMAKE_UNITY_BUILD_BATCH_SIZE 16 CACHE STRING "Number of sources per unity batch")

# Precompiled headers - reuse expensive header compilation
option(ENABLE_PCH "Enable precompiled headers" ON)

# ccache support - cache compilation results for faster rebuilds
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Found ccache: ${CCACHE_PROGRAM} - enabling compiler cache")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Parallel AUTOMOC for faster meta-object compilation
set(CMAKE_AUTOMOC_PARALLEL_ENABLED ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Release Build Optimizations (LTO and aggressive optimization)
# ═══════════════════════════════════════════════════════════════════════════════

# Link-Time Optimization (LTO) for Release builds
# Benefits: 5-15% runtime performance improvement, smaller binary size
# Trade-off: Significantly increased link times (2-5x longer) and memory usage
# Recommendation: Keep enabled for release builds, disable for development
# To disable: cmake -DENABLE_LTO=OFF ..
option(ENABLE_LTO "Enable Link-Time Optimization for Release builds" ON)
if(ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_OUTPUT)
    if(IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "Link-Time Optimization (LTO) enabled for Release builds")
        message(STATUS "  Note: LTO increases link times significantly. Use -DENABLE_LTO=OFF for faster dev builds.")
    else()
        message(WARNING "LTO not supported by compiler: ${IPO_OUTPUT}")
    endif()
endif()

# QML compilation to C++ (qmlcachegen) for faster QML execution
# Qt6 enables this by default when using qt_add_qml_module
set(QT_QML_GENERATE_QMLLS_INI ON CACHE BOOL "Generate QML language server config")

# Create AUTOMOC include directories to prevent -Wmissing-include-dirs warnings
# These directories are added to include paths by Qt's AUTOMOC but may not exist
# if no MOC-generated code is produced for certain targets
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/shared/plasmazones_shared_qml_autogen/include)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/shared/plasmazones_shared_qmlplugin_autogen/include)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/shared/plasmazones_shared_qmlplugin_init_autogen/include)

# KDE/Qt requirements (Plasma 6.6 target: KF 6.6, Qt 6.6, LayerShellQt 6.6, KWin 6.6)
set(QT_MIN_VERSION "6.6.0")
set(KF_MIN_VERSION "6.6.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
set(KDE_SKIP_UNINSTALL_TARGET ON)  # We provide our own uninstall target
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

# Find Qt6
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Concurrent
    Core
    Quick
    QuickControls2
    Gui
    DBus
    Widgets
    ShaderTools
)
# GuiPrivate (QRhi) and ShaderToolsPrivate (QShaderBaker) required for daemon zone overlay
find_package(Qt6GuiPrivate CONFIG REQUIRED)
find_package(Qt6ShaderToolsPrivate CONFIG REQUIRED)

# Optional: Qt6::ShaderTools for shader effects support
# RenderNode shaders use raw GLSL - no Qt6::ShaderTools compilation needed
# Shaders are always enabled with the RenderNode implementation
message(STATUS "RenderNode shaders ENABLED (raw GLSL, no qsb compilation)")
set(PLASMAZONES_SHADERS_ENABLED ON)
add_compile_definitions(PLASMAZONES_SHADERS_ENABLED)

# Find KDE Frameworks
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    Config
    ConfigWidgets
    CoreAddons
    DBusAddons
    I18n
    KCMUtils
    WindowSystem
    GlobalAccel
    Notifications
    ColorScheme
)

# KActivities/PlasmaActivities is optional (for activity-based layouts)
# Plasma 6 renamed KActivities to PlasmaActivities
find_package(PlasmaActivities QUIET)
if(NOT PlasmaActivities_FOUND)
    # Try KF6 name (some distros)
    find_package(KF6Activities QUIET COMPONENTS Activities)
    if(KF6Activities_FOUND)
        set(PlasmaActivities_FOUND TRUE)
    endif()
endif()
if(NOT PlasmaActivities_FOUND)
    # Try older KF5 package name
    find_package(KActivities QUIET)
    if(KActivities_FOUND)
        set(PlasmaActivities_FOUND TRUE)
    endif()
endif()

if(PlasmaActivities_FOUND)
    message(STATUS "PlasmaActivities found - activity-based layouts enabled")
    # Don't add global definitions here - set per-target instead
else()
    message(STATUS "PlasmaActivities/KActivities not found - activity support disabled")
endif()

# LayerShellQt for Wayland overlay support (REQUIRED, 6.6+ for setScreen/wantsToBeOnActiveScreen API)
# PlasmaZones requires Wayland with LayerShellQt for proper overlay functionality.
# X11 support has been removed as of v1.2.0 - Plasma 6 is Wayland-first.
find_package(LayerShellQt 6.6 REQUIRED)

# Systemd user unit directory (fallback if not provided by ECM)
if(NOT DEFINED KDE_INSTALL_SYSTEMDUSERUNITDIR)
    set(KDE_INSTALL_SYSTEMDUSERUNITDIR "lib/systemd/user")
endif()

# ---------------------------------------------------------------------------
# Code formatting (clang-format for C++, qmlformat for QML)
#   make clang-format  (or ninja clang-format)
#   make format-qml   (or ninja format-qml)
# ---------------------------------------------------------------------------
include(KDEClangFormat)
file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/**/*.cpp
    ${CMAKE_SOURCE_DIR}/src/**/*.h
    ${CMAKE_SOURCE_DIR}/src/**/*.hpp
    ${CMAKE_SOURCE_DIR}/kcm/*.cpp
    ${CMAKE_SOURCE_DIR}/kcm/*.h
    ${CMAKE_SOURCE_DIR}/kwin-effect/*.cpp
    ${CMAKE_SOURCE_DIR}/kwin-effect/*.h
    ${CMAKE_SOURCE_DIR}/tests/**/*.cpp
    ${CMAKE_SOURCE_DIR}/tests/**/*.h
)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

# QML: qmlformat (from qt6-tools, optional)
find_program(QMLFORMAT qmlformat)
file(GLOB_RECURSE ALL_QML_FILES
    ${CMAKE_SOURCE_DIR}/src/**/*.qml
    ${CMAKE_SOURCE_DIR}/kcm/**/*.qml
)
if(QMLFORMAT AND ALL_QML_FILES)
    # Run qmlformat via helper script; per-file failures are ignored (one bad QML won't fail target)
    add_custom_target(format-qml
        COMMAND ${CMAKE_COMMAND}
            -DQMLFORMAT=${QMLFORMAT}
            -DFILES="${ALL_QML_FILES}"
            -P ${CMAKE_SOURCE_DIR}/cmake/format-qml.cmake
        COMMENT "Formatting QML files with qmlformat"
    )
else()
    add_custom_target(format-qml
        COMMAND ${CMAKE_COMMAND} -E echo "qmlformat not found or no QML files; install qt6-tools for QML formatting"
    )
endif()

# ---------------------------------------------------------------------------
# Translations (KI18n / Gettext)
#   make extract-pot   - extract translatable strings into po/*.pot
#   po/<lang>/<domain>.po are compiled to .mo and installed by KI18N_INSTALL(po)
# ---------------------------------------------------------------------------
find_package(KF6I18n REQUIRED)
find_program(GETTEXT_XGETTEXT_EXECUTABLE NAMES xgettext)
find_program(GETTEXT_MSGCAT_EXECUTABLE NAMES msgcat)

# Domains: plasmazonesd (daemon + overlay), plasmazones-editor (editor), kcm_plasmazones (KCM)
set(PLASMAZONESD_CPP
    ${CMAKE_SOURCE_DIR}/src/daemon/main.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/daemon.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/overlayservice.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/shortcutmanager.cpp
)
set(PLASMAZONESD_QML
    ${CMAKE_SOURCE_DIR}/src/ui/LayoutPreview.qml
    ${CMAKE_SOURCE_DIR}/src/ui/ZoneItem.qml
    ${CMAKE_SOURCE_DIR}/src/ui/ZoneOverlay.qml
    ${CMAKE_SOURCE_DIR}/src/ui/ZoneSelector.qml
    ${CMAKE_SOURCE_DIR}/src/ui/ZoneSelectorWindow.qml
    ${CMAKE_SOURCE_DIR}/src/ui/LayoutOsd.qml
    ${CMAKE_SOURCE_DIR}/src/ui/NavigationOsd.qml
)
set(PLASMAZONES_EDITOR_CPP
    ${CMAKE_SOURCE_DIR}/src/editor/main.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/EditorController.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/AddZoneCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/ApplyTemplateCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/ChangeSelectionCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/ChangeZOrderCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/DividerResizeCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/DuplicateZoneCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/FillZoneCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/SplitZoneCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/BatchUpdateAppearanceCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/ClearAllZonesCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/DeleteZoneCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/DeleteZoneWithFillCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/PasteZonesCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/UpdateLayoutNameCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/UpdateZoneAppearanceCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/UpdateZoneGeometryCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/UpdateZoneNameCommand.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/UpdateZoneNumberCommand.cpp
)
file(GLOB PLASMAZONES_EDITOR_QML "${CMAKE_SOURCE_DIR}/src/editor/qml/*.qml")
set(KCM_PLASMAZONES_QML
    ${CMAKE_SOURCE_DIR}/kcm/ui/main.qml
    ${CMAKE_SOURCE_DIR}/kcm/ui/ModifierCheckBoxes.qml
    ${CMAKE_SOURCE_DIR}/kcm/ui/LayoutThumbnail.qml
    ${CMAKE_SOURCE_DIR}/kcm/ui/KeySequenceInput.qml
    ${CMAKE_SOURCE_DIR}/kcm/ui/PreviewSizeControl.qml
    ${CMAKE_SOURCE_DIR}/kcm/ui/PositionPicker.qml
)

if(GETTEXT_XGETTEXT_EXECUTABLE)
    add_custom_target(extract-pot
        COMMENT "Extracting translatable strings into po/*.pot"
    )
    foreach(_domain plasmazonesd plasmazones-editor kcm_plasmazones)
        if(_domain STREQUAL "plasmazonesd")
            set(_cpp "${PLASMAZONESD_CPP}")
            set(_qml "${PLASMAZONESD_QML}")
        elseif(_domain STREQUAL "plasmazones-editor")
            set(_cpp "${PLASMAZONES_EDITOR_CPP}")
            set(_qml "${PLASMAZONES_EDITOR_QML}")
        else()
            set(_cpp "")
            set(_qml "${KCM_PLASMAZONES_QML}")
        endif()
        list(JOIN _cpp ";" _cstr)
        list(JOIN _qml ";" _qstr)
        # Write file lists and source dir to files so the script avoids shell splitting
        set(_cpp_file "${CMAKE_BINARY_DIR}/po-extract-${_domain}-cpp.txt")
        set(_qml_file "${CMAKE_BINARY_DIR}/po-extract-${_domain}-qml.txt")
        set(_src_dir_file "${CMAKE_BINARY_DIR}/po-extract-src-dir.txt")
        string(REPLACE ";" "\n" _cpp_lines "${_cpp}")
        string(REPLACE ";" "\n" _qml_lines "${_qml}")
        file(WRITE "${_cpp_file}" "${_cpp_lines}\n")
        file(WRITE "${_qml_file}" "${_qml_lines}\n")
        file(WRITE "${_src_dir_file}" "${CMAKE_SOURCE_DIR}\n")
        add_custom_command(TARGET extract-pot POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DCMAKE_SOURCE_DIR_FILE=${_src_dir_file}
                -DBINARY_DIR=${CMAKE_BINARY_DIR}
                -DCMAKE_CMD=${CMAKE_COMMAND}
                -DDOMAIN=${_domain}
                -DCPP_FILES_FILE=${_cpp_file}
                -DQML_FILES_FILE=${_qml_file}
                -DPOT_FILE=${CMAKE_SOURCE_DIR}/po/${_domain}.pot
                -DGETTEXT_XGETTEXT_EXECUTABLE=${GETTEXT_XGETTEXT_EXECUTABLE}
                -DGETTEXT_MSGCAT_EXECUTABLE=${GETTEXT_MSGCAT_EXECUTABLE}
                -P ${CMAKE_SOURCE_DIR}/cmake/extract-pot.cmake
        )
    endforeach()
else()
    add_custom_target(extract-pot
        COMMAND ${CMAKE_COMMAND} -E echo "xgettext not found; install gettext to extract translations"
    )
endif()

# Install .po from po/<lang>/<domain>.po → share/locale/<lang>/LC_MESSAGES/<domain>.mo
# Run extract-pot to create/update po/*.pot; translators add po/<lang>/*.po
KI18N_INSTALL(po)

# Generate version header with compile-time version info
configure_file(
    "${CMAKE_SOURCE_DIR}/src/core/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)
include_directories("${CMAKE_BINARY_DIR}/generated")

# Source directories
add_subdirectory(src)
add_subdirectory(kwin-effect)  # KWin C++ Effect with modifier detection (uses effects.mouseChanged)
add_subdirectory(kcm)

# Tests - enable with -DBUILD_TESTING=ON
option(BUILD_TESTING "Build tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install D-Bus interface XMLs (one per interface)
install(FILES
    dbus/org.plasmazones.LayoutManager.xml
    dbus/org.plasmazones.Overlay.xml
    dbus/org.plasmazones.Screen.xml
    dbus/org.plasmazones.Settings.xml
    dbus/org.plasmazones.WindowDrag.xml
    dbus/org.plasmazones.WindowTracking.xml
    DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR}
)

# Install default layouts
install(DIRECTORY data/layouts/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/layouts
    FILES_MATCHING PATTERN "*.json"
)

# Install bundled RenderNode shaders (raw GLSL, no compilation needed)
install(DIRECTORY data/shaders/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/shaders
    FILES_MATCHING
    PATTERN "metadata.json"
    PATTERN "*.glsl"
    PATTERN "*.vert"
    PATTERN "*.frag"
    PATTERN "preview.png"
)

# Install icons (hicolor for dark themes, hicolor-light for light themes)
install(FILES
    icons/hicolor/scalable/apps/plasmazones.svg
    icons/hicolor/scalable/apps/plasmazones-editor.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps
)
install(FILES
    icons/hicolor-light/scalable/apps/plasmazones.svg
    icons/hicolor-light/scalable/apps/plasmazones-editor.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor-light/scalable/apps
)

# Custom uninstall target that cleans up directories properly
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/uninstall.cmake.in"
    "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake"
    @ONLY
)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/cmake_uninstall.cmake
    COMMENT "Uninstalling PlasmaZones..."
)

# Build/install only installs files. Enabling the daemon, refreshing sycoca/KWin,
# and user-facing messages are handled by packaging (Arch .install, Debian postinst, RPM %post).

# Suppress transitive Qt6::Gui dependency from feature summary (not used by PlasmaZones)
set_package_properties(WrapVulkanHeaders PROPERTIES TYPE RUNTIME)

# Feature summary (REQUIRED + OPTIONAL only; RUNTIME hidden to reduce noise)
feature_summary(WHAT REQUIRED_PACKAGES_FOUND REQUIRED_PACKAGES_NOT_FOUND
                     OPTIONAL_PACKAGES_FOUND OPTIONAL_PACKAGES_NOT_FOUND
                FATAL_ON_MISSING_REQUIRED_PACKAGES)
