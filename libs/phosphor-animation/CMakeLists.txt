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
#   • IMotionClock abstraction + per-handle AnimatedValue<T> template
#     (qreal, QPointF, QSizeF, QRectF, QColor linear/OkLab, QTransform
#     polar-decomposed) with MotionSpec<T> runtime bundle and
#     RetargetPolicy for velocity-preserving redirects.
#   • AnimationController<Handle> compositor-agnostic state machine
#     built on AnimatedValue<QRectF>; SnapPolicy gate;
#     StaggerTimer orchestration.
#
# Depends on Qt6 Core + Gui only. Qt6 Quick becomes a transitive
# dependency when the opt-in PHOSPHOR_ANIMATION_QUICK flag is ON
# (adds QtQuickClock for QML-side animation drivers).
#
# Phase status:
#   Phase 2  AnimationController base + WindowAnimator split      ✓ landed
#   Phase 3  IMotionClock + AnimatedValue<T> unified runtime       ✓ landed
#   Phase 4  QML module + loaders + settings migration             ✓ landed
#            sub-commit 2: Q_GADGET scaffolding + ClockManager     ✓
#            sub-commit 3: per-T PhosphorAnimatedValue wrappers    ✓
#            sub-commit 4: PhosphorMotionAnimation + registry      ✓
#            sub-commit 5: CurveLoader + ProfileLoader             ✓
#            sub-commit 6: Settings → Profile JSON blob migration  ✓
#            sub-commit 7: daemon ProfileRegistry wiring           ✓
#            (in-tree QML call-site migration deferred — housekeeping)
#   Phase 5  phosphor-animation-layer sub-target (ISurfaceAnimator) — planned
#   Phase 6  Shader-backed animation pipeline                      — planned

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
# QPointF/QSizeF/QRect/QRectF + QColor/QTransform used across AnimatedValue<T>.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui)

# phosphor-jsonloader — generic JSON-directory loader (CurveLoader /
# ProfileLoader are thin sinks over it). find_package is a no-op when
# building in-tree (the parent CMakeLists adds the target first); it
# lets the lib stand alone against an installed PhosphorJsonLoader for
# future `git subtree split` extraction.
if(NOT TARGET PhosphorJsonLoader::PhosphorJsonLoader)
    find_package(PhosphorJsonLoader 0.1.0 REQUIRED)
endif()

# Optional Qt Quick support — builds QtQuickClock for QML-side
# animation drivers (Phase 3 sub-commit 5). Off by default so
# compositor / daemon consumers that don't drive QML animations
# don't pull Qt Quick into their link graph transitively.
option(PHOSPHOR_ANIMATION_QUICK "Build QtQuickClock (requires Qt6::Quick)" OFF)
if(PHOSPHOR_ANIMATION_QUICK)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Quick Qml)
endif()

# Optional QML plugin (Phase 4) — builds org.phosphor.animation Q_GADGET
# wrappers (PhosphorEasing / Spring / Curve / Profile) and the
# QtQuickClockManager singleton. Implies PHOSPHOR_ANIMATION_QUICK
# since the clock manager wraps QtQuickClock; the Quick flag is
# auto-enabled when PHOSPHOR_ANIMATION_QML is ON so consumers only
# need to flip the QML flag.
option(PHOSPHOR_ANIMATION_QML "Build org.phosphor.animation QML module (requires Qt6::Quick + Qt6::Qml)" OFF)
if(PHOSPHOR_ANIMATION_QML)
    set(PHOSPHOR_ANIMATION_QUICK ON CACHE BOOL "Auto-enabled by PHOSPHOR_ANIMATION_QML" FORCE)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Quick Qml)
endif()

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
    src/imotionclock.cpp
    src/animatedvalue.cpp
    src/snappolicy.cpp
    src/staggertimer.cpp
    src/phosphorprofileregistry.cpp
    src/curveloader.cpp
    src/profileloader.cpp
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
    include/PhosphorAnimation/Interpolate.h
    include/PhosphorAnimation/AnimatedValue.h
    include/PhosphorAnimation/Profile.h
    include/PhosphorAnimation/ProfilePaths.h
    include/PhosphorAnimation/ProfileTree.h
    include/PhosphorAnimation/SnapPolicy.h
    include/PhosphorAnimation/AnimationController.h
    include/PhosphorAnimation/StaggerTimer.h
    include/PhosphorAnimation/PhosphorProfileRegistry.h
    include/PhosphorAnimation/CurveLoader.h
    include/PhosphorAnimation/ProfileLoader.h
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

# Ensure GNUInstallDirs is included so CMAKE_INSTALL_FULL_DATADIR is
# defined for the datadir compile-definition below. KDE projects
# already include GNUInstallDirs transitively, but we guard it here so
# a standalone build also resolves the absolute data path.
if(NOT DEFINED CMAKE_INSTALL_FULL_DATADIR)
    include(GNUInstallDirs)
endif()

target_include_directories(PhosphorAnimation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

# Expose the install-prefix data directory to the sources. CurveLoader
# and ProfileLoader use this in `loadLibraryBuiltins` so a consumer's
# user-local `~/.local/share/<consumer>/curves` pack never accidentally
# shadows the library's built-in pack (the previous XDG-based lookup
# had the reverse property). The value resolves to the absolute
# installed path such as `/usr/share/phosphor-animation`.
target_compile_definitions(PhosphorAnimation PRIVATE
    "PHOSPHORANIMATION_INSTALL_DATADIR=\"${CMAKE_INSTALL_FULL_DATADIR}/phosphor-animation\""
)

target_compile_features(PhosphorAnimation PUBLIC cxx_std_20)

target_link_libraries(PhosphorAnimation
    PUBLIC
        Qt6::Core
        Qt6::Gui
        PhosphorJsonLoader::PhosphorJsonLoader
)

# Gate QtQuickClock + its Qt Quick dependency behind the opt-in
# flag. Header + source ship only when the feature is enabled so the
# install set stays clean for non-QML consumers.
if(PHOSPHOR_ANIMATION_QUICK)
    target_sources(PhosphorAnimation PRIVATE
        src/qtquickclock.cpp
        include/PhosphorAnimation/QtQuickClock.h
    )
    target_link_libraries(PhosphorAnimation PUBLIC Qt6::Quick)
    install(FILES include/PhosphorAnimation/QtQuickClock.h
            DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimation)
endif()

set_target_properties(PhosphorAnimation PROPERTIES
    VERSION ${PHOSPHORANIMATION_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# QML module (optional)
# ═══════════════════════════════════════════════════════════════════════════════

# The QML module is a separate target — `PhosphorAnimationQml` —
# depending on Quick + Qml. Gated behind PHOSPHOR_ANIMATION_QML so core
# consumers that don't link QML surface don't pay the plugin build cost.
if(PHOSPHOR_ANIMATION_QML)
    add_subdirectory(qml)
endif()

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
