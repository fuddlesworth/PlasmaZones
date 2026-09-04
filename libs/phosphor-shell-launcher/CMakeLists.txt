# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShellLauncher, the launcher's C++ core.
#
# What the spotlight surface needs that is not UI: the fzf-compatible
# fuzzy matcher, the .desktop entry parser and scanner, and the built-in
# ILauncherProvider implementations (apps, calculator, command, windows,
# clipboard). The QML surface (Phosphor.Launcher) is a separate module in
# this same directory so a plugin author can link the core without the UI.
#
# Phase 4.2 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHELLLAUNCHER_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShellLauncher VERSION ${PHOSPHORSHELLLAUNCHER_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

# Gui for exactly one thing: the calculator copies its answer through
# QGuiApplication::clipboard(). Everything else (matcher, parser, model,
# the other providers) is Core, and every provider degrades to a refusal
# rather than a crash under a plain QCoreApplication, so the tests run
# headless. No Quick / Qml: the surface is a separate module.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui)

# The provider contract (ILauncherProvider / ILauncherProviderFactory) lives
# in phosphor-registry.
if(NOT TARGET PhosphorRegistry::PhosphorRegistry)
    find_package(PhosphorRegistry CONFIG REQUIRED)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Core library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorshelllauncher_public_HDRS
    include/PhosphorShellLauncher/FuzzyMatcher.h
    include/PhosphorShellLauncher/DesktopEntry.h
    include/PhosphorShellLauncher/LauncherModel.h
    # The built-in ILauncherProvider implementations.
    include/PhosphorShellLauncher/AppsProvider.h
    include/PhosphorShellLauncher/CalculatorProvider.h
    include/PhosphorShellLauncher/CommandProvider.h
    include/PhosphorShellLauncher/ClipboardProvider.h
    include/PhosphorShellLauncher/WindowsProvider.h
)

set(phosphorshelllauncher_SRCS
    src/fuzzymatcher.cpp
    src/desktopentry.cpp
    src/launchermodel.cpp
    src/appsprovider.cpp
    src/calculatorprovider.cpp
    src/commandprovider.cpp
    src/clipboardprovider.cpp
    src/windowsprovider.cpp
    src/launchhelpers_p.h
)

add_library(PhosphorShellLauncher SHARED
    ${phosphorshelllauncher_public_HDRS}
    ${phosphorshelllauncher_SRCS}
)
add_library(PhosphorShellLauncher::PhosphorShellLauncher ALIAS PhosphorShellLauncher)

generate_export_header(PhosphorShellLauncher
    BASE_NAME PHOSPHORSHELLLAUNCHER
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/PhosphorShellLauncher/phosphorshelllauncher_export.h
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorShellLauncher
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorShellLauncher PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellLauncher
    PUBLIC
        Qt6::Core
        PhosphorRegistry::PhosphorRegistry
    PRIVATE
        # Gui is needed for exactly one thing, the calculator copying its
        # answer through QGuiApplication::clipboard(). No public header names
        # a QtGui type, so keeping it PRIVATE avoids leaking the dependency
        # to every consumer of this library.
        Qt6::Gui
)

set_target_properties(PhosphorShellLauncher PROPERTIES
    VERSION ${PHOSPHORSHELLLAUNCHER_VERSION}
    SOVERSION 0
    # Match every sibling shared library in the tree: hide by default so the
    # EXPORT macro on the public types is load-bearing rather than decorative,
    # and keep the sources in separate translation units.
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════
#
# phosphor-shell links this library and is itself installed, so without these
# rules an installed shell has a DT_NEEDED on a libPhosphorShellLauncher.so.0
# that lands nowhere. The QML module below is STATIC and correctly installs
# nothing, since it rides inside the binary.

install(TARGETS PhosphorShellLauncher
    EXPORT PhosphorShellLauncherTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorshelllauncher_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorShellLauncher
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/PhosphorShellLauncher/phosphorshelllauncher_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorShellLauncher
)

install(EXPORT PhosphorShellLauncherTargets
    FILE PhosphorShellLauncherTargets.cmake
    NAMESPACE PhosphorShellLauncher::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShellLauncher
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorShellLauncherConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellLauncherConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShellLauncher
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellLauncherConfigVersion.cmake"
    VERSION ${PHOSPHORSHELLLAUNCHER_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellLauncherConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellLauncherConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShellLauncher
)

# ═══════════════════════════════════════════════════════════════════════════════
# QML module, STATIC (in-tree linkable): the Phosphor.Launcher surface
# ═══════════════════════════════════════════════════════════════════════════════
#
# Launcher + LauncherResultRow over the core. Kept a separate target so a
# host can link the core (providers, model) without the UI, and so the
# core stays free of Quick.

find_package(Qt6 6.6 REQUIRED COMPONENTS Qml Quick QuickShapes)

# Kirigami.Icon resolves freedesktop icon names through QIcon::fromTheme
# for the result rows. REQUIRED, not QUIET, for the same reason the bar,
# power and control-center modules require it: a host without Kirigami
# should fail at configure rather than draw a launcher with no icons.
find_package(KF6Kirigami REQUIRED)

if(NOT TARGET PhosphorTheme::PhosphorThemeQml)
    find_package(PhosphorTheme CONFIG QUIET)
endif()
if(NOT TARGET PhosphorShellWidgets::PhosphorShellWidgetsQml)
    find_package(PhosphorShellWidgets CONFIG QUIET)
endif()

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

set(_phosphor_launcher_qml_files
    qml/Phosphor/Launcher/Launcher.qml
    qml/Phosphor/Launcher/LauncherResultRow.qml
)

# Pin each QML file's resource alias to its basename so import resolution
# doesn't see a doubled path segment. Same pattern as the sibling libs.
foreach(_qml_file IN LISTS _phosphor_launcher_qml_files)
    get_filename_component(_qml_basename ${_qml_file} NAME)
    set_source_files_properties(${_qml_file}
        PROPERTIES
            QT_RESOURCE_ALIAS ${_qml_basename}
    )
endforeach()

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorShellLauncherQml
    URI Phosphor.Launcher
    VERSION 1.0
    STATIC
    # DEPENDENCIES, never IMPORTS: IMPORTS injects a module's unqualified
    # type names into every file here, and Kirigami exports a `Theme` that
    # would shadow the Phosphor.Theme singleton. Same rule as the bar,
    # power and control-center modules, learned the hard way on the bar.
    DEPENDENCIES
        Phosphor.Theme
        Phosphor.Widgets
        QtQuick.Layouts
        org.kde.kirigami
    SOURCES
        src/phosphorshelllauncher_qml_plugin.cpp
    QML_FILES ${_phosphor_launcher_qml_files}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/Launcher
)

add_library(PhosphorShellLauncher::PhosphorShellLauncherQml ALIAS PhosphorShellLauncherQml)

target_compile_features(PhosphorShellLauncherQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellLauncherQml
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        # No Qt6::QuickShapes here, matching the bar: this module's own QML
        # imports no QtQuick.Shapes. The Shape comes from PhosphorRipple in
        # phosphor-shell-widgets, whose target links QuickShapes and
        # propagates it. The find_package above still runs in this scope so
        # the static plugin's dependency walk resolves the target without a
        # configure warning.
        KF6::Kirigami
        PhosphorShellLauncher::PhosphorShellLauncher
)

if(TARGET PhosphorShellWidgets::PhosphorShellWidgetsQml)
    target_link_libraries(PhosphorShellLauncherQml PUBLIC PhosphorShellWidgets::PhosphorShellWidgetsQml)
endif()
if(TARGET PhosphorShellWidgetsQmlplugin)
    target_link_libraries(PhosphorShellLauncherQml PUBLIC PhosphorShellWidgetsQmlplugin)
endif()
if(TARGET PhosphorTheme::PhosphorThemeQml)
    target_link_libraries(PhosphorShellLauncherQml PUBLIC PhosphorTheme::PhosphorThemeQml)
endif()
if(TARGET PhosphorThemeQmlplugin)
    target_link_libraries(PhosphorShellLauncherQml PUBLIC PhosphorThemeQmlplugin)
endif()

# No install rules for the STATIC QML module, matching the sibling shell
# surfaces: it rides in whatever binary links it.

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShellLauncher" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
