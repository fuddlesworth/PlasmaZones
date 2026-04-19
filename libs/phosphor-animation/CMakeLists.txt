# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorAnimation — motion-runtime primitives for Phosphor-based shells.
#
# A Qt6/C++20 library providing the building blocks for shell/compositor
# animation systems:
#
#   • Polymorphic Curve base with concrete Easing (cubic-bezier + elastic
#     + bounce) and Spring (angular-frequency + damping-ratio) subclasses.
#   • CurveRegistry for string-id ↔ factory, enabling config round-trip
#     and third-party curve extension (plugin-extensible, not a closed
#     variant — required for niri/Hyprland/Quickshell-level customization).
#   • Profile + hierarchical ProfileTree with walk-up inheritance so shell
#     events (window.open, zone.snapIn, osd.show, …) share defaults while
#     allowing per-event overrides.
#   • WindowMotion state + progression, AnimationMath helpers, StaggerTimer
#     orchestration — the primitives previously co-located in
#     src/compositor-common/ are now in their own domain-named library.
#
# Depends on Qt6 Core + Gui only. There is no compositor-common
# forwarding shim — the old easingcurve.h / animation_math.h /
# stagger_timer.h headers are gone and PlasmaZones consumers include
# <PhosphorAnimation/...> directly.
#
# Future phases (separate branches, each gated):
#   Phase 2  AnimationController base + WindowAnimator split
#   Phase 3  IMotionClock abstraction + AnimatedValue<T> template
#   Phase 4  QML value types (PhosphorEasing/Spring/Motion)
#   Phase 5  phosphor-animation-layer sub-target (ISurfaceAnimator impl)
#   Phase 6  Shader-backed animation pipeline

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORANIMATION_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorAnimation VERSION ${PHOSPHORANIMATION_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

# Pin to 6.6 to match sibling phosphor-* libraries. Qt Gui is required for
# QPointF/QSizeF/QRect/QRectF used across WindowMotion + AnimationMath.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorAnimation Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphoranimation_SRCS
    src/curve.cpp
    src/easing.cpp
    src/spring.cpp
    src/curveregistry.cpp
    src/profile.cpp
    src/profilepaths.cpp
    src/profiletree.cpp
    src/windowmotion.cpp
    src/animationmath.cpp
    src/animatedvalue.cpp
    src/staggertimer.cpp
)

set(phosphoranimation_public_HDRS
    include/PhosphorAnimation/PhosphorAnimation.h
    include/PhosphorAnimation/Curve.h
    include/PhosphorAnimation/Easing.h
    include/PhosphorAnimation/Spring.h
    include/PhosphorAnimation/CurveRegistry.h
    include/PhosphorAnimation/IMotionClock.h
    include/PhosphorAnimation/RetargetPolicy.h
    include/PhosphorAnimation/MotionSpec.h
    include/PhosphorAnimation/AnimatedValue.h
    include/PhosphorAnimation/Profile.h
    include/PhosphorAnimation/ProfilePaths.h
    include/PhosphorAnimation/ProfileTree.h
    include/PhosphorAnimation/WindowMotion.h
    include/PhosphorAnimation/AnimationMath.h
    include/PhosphorAnimation/AnimationController.h
    include/PhosphorAnimation/StaggerTimer.h
)

add_library(PhosphorAnimation SHARED
    ${phosphoranimation_public_HDRS}
    ${phosphoranimation_SRCS}
)

add_library(PhosphorAnimation::PhosphorAnimation ALIAS PhosphorAnimation)

# Generate the export header under the PhosphorAnimation/ prefix so the
# same #include <PhosphorAnimation/phosphoranimation_export.h> resolves in
# both in-tree builds (via BUILD_INTERFACE below) and installed consumers.
generate_export_header(PhosphorAnimation
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimation/phosphoranimation_export.h
    EXPORT_MACRO_NAME PHOSPHORANIMATION_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

target_include_directories(PhosphorAnimation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorAnimation PUBLIC cxx_std_20)

target_link_libraries(PhosphorAnimation
    PUBLIC
        Qt6::Core
        Qt6::Gui
)

set_target_properties(PhosphorAnimation PROPERTIES
    VERSION ${PHOSPHORANIMATION_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

# Enable unit tests when building as part of the parent project or when the
# user explicitly toggles BUILD_TESTING. Mirrors phosphor-layer's policy so
# ctest picks them up automatically on a normal `cmake --build` cycle.
if(CMAKE_PROJECT_NAME STREQUAL "PhosphorAnimation" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorAnimation
    EXPORT PhosphorAnimationTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphoranimation_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimation
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimation/phosphoranimation_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimation
)

install(EXPORT PhosphorAnimationTargets
    FILE PhosphorAnimationTargets.cmake
    NAMESPACE PhosphorAnimation::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorAnimation
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorAnimationConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorAnimation
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationConfigVersion.cmake"
    VERSION ${PHOSPHORANIMATION_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorAnimation
)
