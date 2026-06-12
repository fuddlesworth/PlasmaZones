# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorPopout, centralized popout coordinator for Phosphor shells.
#
# Owns the single arbiter for popout lifetime, focus, and exclusivity
# policy across the shell. The transport seam decouples arbitration from
# the actual layer-shell surface stack so tests can run pure logic and
# alternate hosts can swap the backend.
#
# Phase 1.2 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORPOPOUT_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorPopout VERSION ${PHOSPHORPOPOUT_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick)
# PopoutHost.qml imports Phosphor.Theme for Motion timing tokens. The
# QML import scanner (qmllint, qmlcachegen) needs the registered theme
# module visible at build time, and consumers need the runtime plugin
# linked so the import resolves. find PhosphorTheme from a sibling
# subproject build first, then fall back to an installed package.
if(NOT TARGET PhosphorTheme::PhosphorThemeQml)
    find_package(PhosphorTheme CONFIG QUIET)
endif()

# Qt 6.5+ resource-prefix policy. NEW = use `:/qt/qml/` as the QML
# module resource prefix. The `qt_policy` command is what we actually
# need; it's available on every Qt CMake build new enough to publish
# the QTP0001 policy.
if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorpopout_public_HDRS
    include/PhosphorPopout/PhosphorPopout.h
    include/PhosphorPopout/PopoutRequest.h
    include/PhosphorPopout/IPopoutTransport.h
    include/PhosphorPopout/IPopoutService.h
    include/PhosphorPopout/PopoutController.h
)

set(phosphorpopout_SRCS
    src/popoutcontroller.cpp
)

add_library(PhosphorPopout SHARED
    ${phosphorpopout_public_HDRS}
    ${phosphorpopout_SRCS}
)

add_library(PhosphorPopout::PhosphorPopout ALIAS PhosphorPopout)

generate_export_header(PhosphorPopout
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorPopout/phosphorpopout_export.h
    EXPORT_MACRO_NAME PHOSPHORPOPOUT_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

target_include_directories(PhosphorPopout
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorPopout PUBLIC cxx_std_20)

target_link_libraries(PhosphorPopout
    PUBLIC
        Qt6::Core
        Qt6::Gui
        # PopoutController carries QML_ELEMENT from
        # <QtQmlIntegration/qqmlintegration.h>. The macro expands to
        # markers that AUTOMOC and qmltyperegistrar pick up at build
        # time; the header itself is part of Qt6::Qml.
        Qt6::Qml
)

set_target_properties(PhosphorPopout PROPERTIES
    VERSION ${PHOSPHORPOPOUT_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# QML module, STATIC (in-tree linkable)
# ═══════════════════════════════════════════════════════════════════════════════
#
# C++ side: PopoutController carries QML_ELEMENT so QML callers can
# instantiate it. The PopoutHost.qml component lives alongside as a
# transport-agnostic wrapper that any popout host (in-app for the demo,
# layer-shell for the shell) parents content into.

set(_phosphor_popout_qml_files
    qml/Phosphor/Popout/PopoutHost.qml
)

# qt_add_qml_module nests QML_FILES under their source-relative path
# beneath the module URI prefix. Pin the resource alias to the basename
# so import resolution doesn't see a doubled `qml/Phosphor/Popout/`
# segment. Same pattern as PhosphorTheme.
foreach(_qml_file IN LISTS _phosphor_popout_qml_files)
    get_filename_component(_qml_basename ${_qml_file} NAME)
    set_source_files_properties(${_qml_file}
        PROPERTIES
            QT_RESOURCE_ALIAS ${_qml_basename}
    )
endforeach()

set(_phosphor_popout_qml_sources
    src/phosphorpopout_qml_plugin.cpp
    # Headers listed so AUTOMOC on the QML module target emits
    # metatype JSON. qmltyperegistrar reads it and publishes the QML
    # registrations the plugin loads at startup. Implementations stay
    # in PhosphorPopout.so (linked below).
    #
    # PopoutController.h carries QML_ELEMENT + QML_UNCREATABLE so the
    # type name is visible to QML. PopoutRequest.h carries Q_NAMESPACE
    # + Q_ENUM_NS + QML_ELEMENT (for the namespace) and Q_GADGET +
    # QML_VALUE_TYPE(popoutRequest) on the request struct, so QML
    # callers reach `Anchor.BarCenter`, `ExclusiveMode.Cooperative`,
    # and the `popoutRequest` value type.
    include/PhosphorPopout/PopoutController.h
    include/PhosphorPopout/PopoutRequest.h
)

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here: Qt adds <target>_autogen/include to each
# sub-target's -I list, but when a module carries no Q_OBJECT sources AUTOMOC
# never creates that directory (and --clean-first deletes it), so cc1plus
# warns on a Qt-managed scratch path. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorPopoutQml
    URI Phosphor.Popout
    VERSION 1.0
    STATIC
    # PopoutHost.qml uses Motion tokens from Phosphor.Theme. Declaring
    # the import here lets qmllint and qmlcachegen resolve symbols
    # at build time. The runtime registration is handled by the
    # PhosphorThemeQmlplugin link below.
    IMPORTS Phosphor.Theme
    SOURCES ${_phosphor_popout_qml_sources}
    QML_FILES ${_phosphor_popout_qml_files}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/Popout
)

add_library(PhosphorPopout::PhosphorPopoutQml ALIAS PhosphorPopoutQml)

target_include_directories(PhosphorPopoutQml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorPopoutQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorPopoutQml
    PUBLIC
        PhosphorPopout::PhosphorPopout
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

# Link the PhosphorTheme QML plugin and module when available so
# consumers don't have to wire it themselves. PopoutHost.qml's
# import resolves through the linked plugin's runtime registration.
# In-tree builds always have these targets; out-of-tree consumers
# get a clear failure mode (missing target) instead of a runtime
# "module not found" from QML.
if(TARGET PhosphorTheme::PhosphorThemeQml)
    target_link_libraries(PhosphorPopoutQml PUBLIC PhosphorTheme::PhosphorThemeQml)
endif()
if(TARGET PhosphorThemeQmlplugin)
    target_link_libraries(PhosphorPopoutQml PUBLIC PhosphorThemeQmlplugin)
endif()

# Static archive: VERSION/SOVERSION apply to SHARED libraries only and are
# silently ignored on STATIC. Don't set them so the property surface
# matches what the target actually carries.

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_BINDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorPopout
    EXPORT PhosphorPopoutTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorpopout_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorPopout
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorPopout/phosphorpopout_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorPopout
)

install(EXPORT PhosphorPopoutTargets
    FILE PhosphorPopoutTargets.cmake
    NAMESPACE PhosphorPopout::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPopout
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorPopoutConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPopoutConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPopout
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPopoutConfigVersion.cmake"
    VERSION ${PHOSPHORPOPOUT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPopoutConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPopoutConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPopout
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorPopout" OR BUILD_TESTING)
    # enable_testing() is a no-op when KDECMakeSettings already
    # included CTest at the top level. The redundant call lets a
    # standalone build of this subproject still discover ctest.
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
