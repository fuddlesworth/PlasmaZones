# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorAnimationLayer — concrete ISurfaceAnimator backed by phosphor-animation
#
# Phase 5 of the phosphor-animation roadmap. Bridges phosphor-layer
# (Surface lifecycle) and phosphor-animation (motion runtime) so every
# overlay surface drives its show/hide through the same Profile-driven
# curve / duration as in-window animations. Without this library every
# consumer hand-rolls its own QML `ParallelAnimation` blocks per overlay;
# with it the animation is library-driven and the QML side stays
# declarative.
#
# Optional target: consumers that don't link phosphor-animation can still
# use phosphor-layer with the no-op default. Enable only where the
# Profile-driven animator is wanted.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORANIMATIONLAYER_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorAnimationLayer VERSION ${PHOSPHORANIMATIONLAYER_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Quick)

# In-tree, the two upstream libs are added via add_subdirectory before us.
# Out-of-tree consumers find them via find_package.
if(NOT TARGET PhosphorLayer::PhosphorLayer)
    find_package(PhosphorLayer CONFIG REQUIRED)
endif()
if(NOT TARGET PhosphorAnimation::PhosphorAnimation)
    find_package(PhosphorAnimation CONFIG REQUIRED)
endif()

set(phosphoranimationlayer_SRCS
    src/surfaceanimator.cpp
)

set(phosphoranimationlayer_public_HDRS
    include/PhosphorAnimationLayer/PhosphorAnimationLayer.h
    include/PhosphorAnimationLayer/SurfaceAnimator.h
)

add_library(PhosphorAnimationLayer SHARED
    ${phosphoranimationlayer_SRCS}
    ${phosphoranimationlayer_public_HDRS}
)

add_library(PhosphorAnimationLayer::PhosphorAnimationLayer ALIAS PhosphorAnimationLayer)

target_include_directories(PhosphorAnimationLayer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# PhosphorAnimation is PUBLIC because the SurfaceAnimator constructor
# accepts a PhosphorProfileRegistry& by reference and the public header
# `#include`s `<PhosphorAnimation/PhosphorProfileRegistry.h>`. PhosphorLayer
# is PUBLIC for the same reason — `ISurfaceAnimator` lives there.
target_link_libraries(PhosphorAnimationLayer
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        PhosphorLayer::PhosphorLayer
        PhosphorAnimation::PhosphorAnimation
)

set_target_properties(PhosphorAnimationLayer PROPERTIES
    VERSION ${PHOSPHORANIMATIONLAYER_VERSION}
    SOVERSION 0
    EXPORT_NAME PhosphorAnimationLayer
)

generate_export_header(PhosphorAnimationLayer
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/PhosphorAnimationLayer/phosphoranimationlayer_export.h
    EXPORT_MACRO_NAME PHOSPHORANIMATIONLAYER_EXPORT
)
target_include_directories(PhosphorAnimationLayer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorAnimationLayer" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(DEFINED KDE_INSTALL_INCLUDEDIR AND DEFINED KDE_INSTALL_LIBDIR)
    install(TARGETS PhosphorAnimationLayer
        EXPORT PhosphorAnimationLayerTargets
        LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
    )
    install(FILES ${phosphoranimationlayer_public_HDRS}
        DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimationLayer
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/PhosphorAnimationLayer/phosphoranimationlayer_export.h
        DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimationLayer
    )
endif()
