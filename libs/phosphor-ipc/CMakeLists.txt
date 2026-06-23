# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorIpc, typed JSON-over-Unix-socket invocation channel for the
# phosphor-shell. Lets compositor keybinds, external scripts, and other
# shells drive Phosphor actions via `phosphorctl call <target>.<fn>`,
# without replacing the existing D-Bus surface.
#
# Phase 1.4 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORIPC_VERSION "0.1.0")
# Wire-protocol version. Build-time invariant only: there is no
# on-wire handshake and no version-negotiation message in the
# protocol. The constant is exported as PHOSPHOR_IPC_PROTOCOL_VERSION
# so the static_assert in IpcProtocol.h can cross-check this CMake
# value against the header constant at compile time. Bumped whenever
# the JSON shape on the wire changes incompatibly; mirrors
# PluginAbiVersion's CMake-↔-header link in phosphor-registry.
set(PHOSPHOR_IPC_PROTOCOL_VERSION 1)

if(NOT PROJECT_VERSION)
    project(PhosphorIpc VERSION ${PHOSPHORIPC_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    # Stay strictly ISO C++20, no GNU extensions in LGPL public headers.
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

# Core: QObject / QVariant / QString. Network: QLocalServer /
# QLocalSocket. Qml: IpcTarget's QML_ELEMENT registration. No Gui
# here: none of the public headers (IpcRouter, IpcTarget, IpcProtocol,
# IpcSchemaGenerator, IpcEngine) pull in any QtGui types, so linking
# Gui at the core target would force CLI consumers like phosphorctl
# to drag in the full GUI stack for no reason. The Gui dependency is
# scoped onto PhosphorIpcQml below (qmltyperegistrar's generated code
# transitively wants it). Keeping the find_package list aligned with
# PhosphorIpcConfig.cmake.in's downstream find_dependency block so the
# build-side dependency set doesn't drift from the install-side one.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Qml Network)

# Qt 6.5+ resource-prefix policy. NEW = use `:/qt/qml/` as the QML
# module resource prefix. Without this, qt_add_qml_module emits
# QTP0001 dev warnings on every configure.
if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphoripc_public_HDRS
    include/PhosphorIpc/PhosphorIpc.h
    include/PhosphorIpc/IpcRouter.h
    include/PhosphorIpc/IpcTarget.h
    include/PhosphorIpc/IpcEngine.h
    include/PhosphorIpc/IpcProtocol.h
    include/PhosphorIpc/IpcSchemaGenerator.h
)

set(phosphoripc_SRCS
    src/ipcrouter.cpp
    src/ipcrouterdispatch.cpp
    src/ipctarget.cpp
    src/ipcengine.cpp
    src/ipcprotocol.cpp
    src/ipcschemagenerator.cpp
)

add_library(PhosphorIpc SHARED
    ${phosphoripc_public_HDRS}
    ${phosphoripc_SRCS}
)

add_library(PhosphorIpc::PhosphorIpc ALIAS PhosphorIpc)

generate_export_header(PhosphorIpc
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorIpc/phosphoripc_export.h
    EXPORT_MACRO_NAME PHOSPHORIPC_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

target_include_directories(PhosphorIpc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorIpc PUBLIC cxx_std_20)

target_compile_definitions(PhosphorIpc
    PUBLIC
        PHOSPHOR_IPC_PROTOCOL_VERSION=${PHOSPHOR_IPC_PROTOCOL_VERSION}
)

target_link_libraries(PhosphorIpc
    PUBLIC
        Qt6::Core
        Qt6::Qml
        Qt6::Network
)

set_target_properties(PhosphorIpc PROPERTIES
    VERSION ${PHOSPHORIPC_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# QML module registrar
# ═══════════════════════════════════════════════════════════════════════════════
#
# IpcTarget is declared with QML_ELEMENT in the SHARED PhosphorIpc
# library. A separate STATIC qt_add_qml_module call publishes the
# "Phosphor.Ipc" import URI by generating the QML module registrar
# and the plugin glue. Consumers link both PhosphorIpc::PhosphorIpcQml
# (the namespaced alias declared below) and the unnamespaced
# PhosphorIpcQmlplugin target that qt_add_qml_module creates to get
# the type registered in their QQmlEngine. Mirrors the PhosphorTheme
# / PhosphorThemeQml split.

# Qt6::Gui is scoped onto the QML plugin specifically; the
# qmltyperegistrar-generated registration translation unit
# transitively expects QGuiApplication / QQmlEngine to be in scope.
# find_package is idempotent — when the top-level project already
# called find_package(Qt6 ... Gui) this is a no-op; when this lib
# is consumed standalone via add_subdirectory, the call ensures
# Qt6::Gui is found before the qt_add_qml_module below.
find_package(Qt6 6.6 REQUIRED COMPONENTS Gui)

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here: Qt adds <target>_autogen/include to each
# sub-target's -I list, but when a module carries no Q_OBJECT sources AUTOMOC
# never creates that directory (and --clean-first deletes it), so cc1plus
# warns on a Qt-managed scratch path. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorIpcQml
    URI Phosphor.Ipc
    VERSION 1.0
    STATIC
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/Ipc
    SOURCES
        include/PhosphorIpc/IpcTarget.h
)

add_library(PhosphorIpc::PhosphorIpcQml ALIAS PhosphorIpcQml)

# The QML type registration generator emits #include <IpcTarget.h>
# (bare basename of the SOURCES file). Adding the PhosphorIpc/
# directory itself to the include path lets that bare include
# resolve without changing the public header layout.
target_include_directories(PhosphorIpcQml
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/PhosphorIpc
)

target_link_libraries(PhosphorIpcQml
    PUBLIC
        PhosphorIpc::PhosphorIpc
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
)

target_compile_features(PhosphorIpcQml PUBLIC cxx_std_20)

set_target_properties(PhosphorIpcQml PROPERTIES
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_BINDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorIpc
    EXPORT PhosphorIpcTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphoripc_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorIpc
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorIpc/phosphoripc_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorIpc
)

install(EXPORT PhosphorIpcTargets
    FILE PhosphorIpcTargets.cmake
    NAMESPACE PhosphorIpc::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorIpc
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorIpcConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorIpcConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorIpc
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorIpcConfigVersion.cmake"
    VERSION ${PHOSPHORIPC_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorIpcConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorIpcConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorIpc
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorIpc" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
