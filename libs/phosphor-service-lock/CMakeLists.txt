# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServiceLock: a session-lock + authentication service for Phosphor-based
# desktop shells. Phase 2.9 of the service-library split (see
# docs/phosphor-shell-design/04-implementation-plan.md). It authenticates the
# user through PAM and coordinates the ext-session-lock-v1 lock state with the
# compositor. It is the policy layer over the raw protocol client that already
# lives in phosphor-wayland (SessionLock), so it does not re-vendor protocols;
# phosphor-wayland is wrapped privately and does not appear in the public link
# interface.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICELOCK_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServiceLock VERSION ${PHOSPHORSERVICELOCK_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════
#
# Core / Qml: the QObject + QML surface. Concurrent runs the blocking PAM
# transaction off the GUI thread. PhosphorWayland is the backend; it is PRIVATE
# because the foundation SessionLock client is used inside the .cpp and never
# appears in the public headers, so consumers neither include nor link
# phosphor-wayland. Concurrent and PAM are likewise private.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Qml Concurrent)

# phosphor-wayland is an in-tree sibling under BUILD_PHOSPHOR_SHELL; only
# find_package it when building this lib stand-alone.
if(NOT TARGET PhosphorWayland::PhosphorWayland)
    find_package(PhosphorWayland REQUIRED)
endif()

# PAM: the authentication backend. Linked PRIVATE (security/pam_appl.h and the
# PAM handle never appear in the public headers; the IAuthenticator seam hides
# them). PAM ships no CMake/pkg-config module, so locate it directly.
find_path(PAM_INCLUDE_DIR NAMES security/pam_appl.h)
find_library(PAM_LIBRARY NAMES pam)
if(NOT PAM_INCLUDE_DIR OR NOT PAM_LIBRARY)
    message(FATAL_ERROR "phosphor-service-lock requires PAM (libpam + security/pam_appl.h)")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorservicelock_public_HDRS
    include/PhosphorServiceLock/PhosphorServiceLock.h
    include/PhosphorServiceLock/QmlRegistration.h
    include/PhosphorServiceLock/LockService.h
    include/PhosphorServiceLock/IAuthenticator.h
    include/PhosphorServiceLock/PamAuthenticator.h
)

set(phosphorservicelock_SRCS
    src/qmlregistration.cpp
    src/lockservice.cpp
    src/lockstatemachine.cpp
    src/iauthenticator.cpp
    src/pamauthenticator.cpp
    src/isessionlock.cpp
    src/waylandsessionlock.cpp
)

add_library(PhosphorServiceLock SHARED
    ${phosphorservicelock_public_HDRS}
    ${phosphorservicelock_SRCS}
)

add_library(PhosphorServiceLock::PhosphorServiceLock ALIAS PhosphorServiceLock)

generate_export_header(PhosphorServiceLock
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceLock/phosphorservicelock_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICELOCK_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServiceLock
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorServiceLock PUBLIC cxx_std_20)

target_link_libraries(PhosphorServiceLock
    PUBLIC
        Qt6::Core
        Qt6::Qml
    PRIVATE
        Qt6::Concurrent
        PhosphorWayland::PhosphorWayland
        ${PAM_LIBRARY}
)

target_include_directories(PhosphorServiceLock PRIVATE ${PAM_INCLUDE_DIR})

set_target_properties(PhosphorServiceLock PROPERTIES
    VERSION ${PHOSPHORSERVICELOCK_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorServiceLock
    EXPORT PhosphorServiceLockTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorservicelock_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceLock
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceLock/phosphorservicelock_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceLock
)

install(EXPORT PhosphorServiceLockTargets
    FILE PhosphorServiceLockTargets.cmake
    NAMESPACE PhosphorServiceLock::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceLock
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServiceLockConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceLockConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceLock
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceLockConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICELOCK_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceLockConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceLockConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceLock
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServiceLock" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
