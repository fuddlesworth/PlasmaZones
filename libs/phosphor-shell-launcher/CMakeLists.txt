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

# Core only. Gui is deliberately NOT linked into the core library: nothing in
# the matcher, the parser or the providers touches QImage / QGuiApplication,
# which keeps a CLI or a test free of the GUI stack.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

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
)

set(phosphorshelllauncher_SRCS
    src/fuzzymatcher.cpp
    src/desktopentry.cpp
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

target_include_directories(PhosphorShellLauncher
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(PhosphorShellLauncher PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellLauncher
    PUBLIC
        Qt6::Core
        PhosphorRegistry::PhosphorRegistry
)

set_target_properties(PhosphorShellLauncher PROPERTIES
    VERSION ${PHOSPHORSHELLLAUNCHER_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShellLauncher" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
