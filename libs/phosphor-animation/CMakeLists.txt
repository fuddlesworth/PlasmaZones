# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorAnimation — unified animation library for Phosphor-based shells.
#
# A Qt6/C++20 library providing the building blocks for shell/compositor
# animation systems:
#
#   * Polymorphic Curve base with concrete Easing (cubic-bezier + elastic
#     + bounce) and Spring (angular-frequency + damping-ratio) subclasses.
#   * CurveRegistry for string-id <-> factory, enabling config round-trip
#     and third-party curve extension.
#   * Profile + hierarchical ProfileTree with walk-up inheritance.
#   * IMotionClock abstraction + AnimatedValue<T> template with
#     MotionSpec<T> runtime bundle and RetargetPolicy.
#   * SnapPolicy gate for animation-start threshold filtering.
#   * QML module (org.phosphor.animation): PhosphorMotionAnimation,
#     PhosphorCurve, PhosphorEasing, PhosphorSpring, PhosphorProfile,
#     QtQuickClockManager.
#   * Shader-backed animation pipeline: AnimationShaderContract,
#     AnimationShaderEffect, AnimationShaderRegistry, ShaderProfile,
#     ShaderProfileTree.
#   * SurfaceAnimator: ISurfaceAnimator bridging phosphor-layer surfaces
#     to the motion runtime.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORANIMATION_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorAnimation VERSION ${PHOSPHORANIMATION_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ===============================================================================
# Dependencies
# ===============================================================================

# Pin to 6.6 to match sibling phosphor-* libraries.
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Quick Qml)

# PhosphorMotionAnimation inherits QQuickPropertyAnimation which lives
# in a private Qt Quick header (<QtQuick/private/qquickanimation_p.h>).
if(NOT TARGET Qt6::QuickPrivate)
    find_package(Qt6QuickPrivate CONFIG REQUIRED)
endif()

# phosphor-fsloader -- filesystem-backed loader scaffolding.
if(NOT TARGET PhosphorFsLoader::PhosphorFsLoader)
    find_package(PhosphorFsLoader 0.1.0 REQUIRED)
endif()

# Header-only consumption of <PhosphorShaders/CustomParamsKey.h>.
if(NOT TARGET PhosphorShaders::PhosphorShaders)
    find_package(PhosphorShaders CONFIG REQUIRED)
endif()

# PhosphorRegistry — AnimationShaderRegistry composes Registry<AnimationPack>
# + MetadataPackLoader<AnimationPack> (publicly exposed via its header).
if(NOT TARGET PhosphorRegistry::PhosphorRegistry)
    find_package(PhosphorRegistry 0.1.0 REQUIRED)
endif()

# phosphor-layer -- Surface lifecycle (ISurfaceAnimator base).
if(NOT TARGET PhosphorLayer::PhosphorLayer)
    find_package(PhosphorLayer CONFIG REQUIRED)
endif()

# phosphor-rendering -- ShaderEffect used by SurfaceAnimator.
if(NOT TARGET PhosphorRendering::PhosphorRendering)
    find_package(PhosphorRendering CONFIG REQUIRED)
endif()

# Optional Qt Quick support -- builds QtQuickClock for QML-side
# animation drivers. Off by default so headless consumers skip Qt Quick.
option(PHOSPHOR_ANIMATION_QUICK "Build QtQuickClock (requires Qt6::Quick)" OFF)

# Qt 6.5+ resource-prefix policy. NEW = use `:/qt/qml/` as the QML
# module resource prefix. Guard on COMMAND qt_policy alone: the prior
# `POLICY CMP0090 OR ...` disjunct was always true (CMP0090 is a CMake
# policy present since 3.15), making the qt_policy-existence half dead and
# fatal-erroring on a toolchain without qt_policy (Qt < 6.5).
if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# Position-independent code is required for Qt's QML type-registration
# sub-targets.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Suppress -Wmissing-include-dirs for Qt-managed AUTOMOC scratch dirs.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

# ===============================================================================
# Public headers
# ===============================================================================

set(phosphoranimation_public_HDRS
    # Core motion runtime
    include/PhosphorAnimation/AnimationLimits.h
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
    include/PhosphorAnimation/AnimatedValue_geometric.h
    include/PhosphorAnimation/AnimatedValue_lifecycle_extras.h
    include/PhosphorAnimation/Profile.h
    include/PhosphorAnimation/ProfilePaths.h
    include/PhosphorAnimation/ProfileTree.h
    include/PhosphorAnimation/SnapPolicy.h
    include/PhosphorAnimation/StaggerTimer.h
    include/PhosphorAnimation/PhosphorProfileRegistry.h
    include/PhosphorAnimation/CurveLoader.h
    include/PhosphorAnimation/ProfileLoader.h

    # Shader pipeline (ex phosphor-animation-shaders)
    include/PhosphorAnimation/PhosphorAnimationShaders.h
    include/PhosphorAnimation/AnimationShaderContract.h
    include/PhosphorAnimation/AnimationShaderEffect.h
    include/PhosphorAnimation/AnimationShaderRegistry.h
    include/PhosphorAnimation/AnimationUniformExtension.h
    include/PhosphorAnimation/ShaderProfile.h
    include/PhosphorAnimation/ShaderProfileTree.h

    # Layer animator (ex phosphor-animation-layer)
    include/PhosphorAnimation/PhosphorAnimationLayer.h
    include/PhosphorAnimation/SurfaceAnimator.h
)

# QML wrapper headers — installed alongside the public headers but
# NOT compiled into the main PhosphorAnimation target, because their
# implementations (phosphorcurve.cpp, phosphormotionanimation.cpp,
# qtquickclockmanager.cpp) only live in PhosphorAnimationQml. Listing
# them in the main target's source set caused AUTOMOC to generate
# moc references against absent implementations, producing the
# "undefined reference to ~PhosphorMotionAnimation()" link errors
# after the QML SRCS were extracted to the QML target.
set(phosphoranimation_qml_HDRS
    include/PhosphorAnimation/PhosphorEasing.h
    include/PhosphorAnimation/PhosphorSpring.h
    include/PhosphorAnimation/PhosphorCurve.h
    include/PhosphorAnimation/PhosphorProfile.h
    include/PhosphorAnimation/QtQuickClockManager.h
    include/PhosphorAnimation/PhosphorMotionAnimation.h
)

# ===============================================================================
# Sources
# ===============================================================================

set(phosphoranimation_SRCS
    # Core motion runtime
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

    # Shader pipeline (ex phosphor-animation-shaders)
    src/animationshadereffect.cpp
    src/animationshaderregistry.cpp
    src/shaderprofile.cpp
    src/shaderprofiletree.cpp
    src/contract_pins.cpp

    # Layer animator (ex phosphor-animation-layer)
    src/surfaceanimator.cpp
)

# ===============================================================================
# QML module sources (shared between STATIC and SHARED modules)
# ===============================================================================

set(_phosphor_animation_qml_sources
    src/phosphoranimation_qml_plugin.cpp
    src/phosphorcurve.cpp
    src/qtquickclockmanager.cpp
    src/phosphormotionanimation.cpp
    include/PhosphorAnimation/PhosphorEasing.h
    include/PhosphorAnimation/PhosphorSpring.h
    include/PhosphorAnimation/PhosphorCurve.h
    include/PhosphorAnimation/PhosphorProfile.h
    include/PhosphorAnimation/QtQuickClockManager.h
    include/PhosphorAnimation/PhosphorMotionAnimation.h
)

# ===============================================================================
# PhosphorAnimation Library
# ===============================================================================

add_library(PhosphorAnimation SHARED
    ${phosphoranimation_public_HDRS}
    ${phosphoranimation_SRCS}
)

add_library(PhosphorAnimation::PhosphorAnimation ALIAS PhosphorAnimation)

generate_export_header(PhosphorAnimation
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimation/phosphoranimation_export.h
    EXPORT_MACRO_NAME PHOSPHORANIMATION_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

if(NOT DEFINED CMAKE_INSTALL_FULL_DATADIR)
    include(GNUInstallDirs)
endif()

if(NOT DEFINED KDE_INSTALL_QMLDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_QMLDIR ${CMAKE_INSTALL_LIBDIR}/qt6/qml)
endif()

target_include_directories(PhosphorAnimation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_definitions(PhosphorAnimation PRIVATE
    "PHOSPHORANIMATION_INSTALL_DATADIR=\"${CMAKE_INSTALL_FULL_DATADIR}/phosphor-animation\""
)

target_compile_features(PhosphorAnimation PUBLIC cxx_std_20)

# Qt6::QuickPrivate is PUBLIC because PhosphorMotionAnimation.h includes
# <QtQuick/private/qquickanimation_p.h>.
target_link_libraries(PhosphorAnimation
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickPrivate
        Qt6::Qml
        PhosphorRegistry::PhosphorRegistry
        PhosphorFsLoader::PhosphorFsLoader
        PhosphorShaders::PhosphorShaders
        PhosphorLayer::PhosphorLayer
        PhosphorRendering::PhosphorRendering
)

# Gate QtQuickClock behind the opt-in flag.
if(PHOSPHOR_ANIMATION_QUICK)
    target_sources(PhosphorAnimation PRIVATE
        src/qtquickclock.cpp
        include/PhosphorAnimation/QtQuickClock.h
    )
    install(FILES include/PhosphorAnimation/QtQuickClock.h
            DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimation)
endif()

set_target_properties(PhosphorAnimation PROPERTIES
    VERSION ${PHOSPHORANIMATION_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ===============================================================================
# QML Module — STATIC (in-tree linkable)
# ===============================================================================

qt_add_qml_module(PhosphorAnimationQml
    URI org.phosphor.animation
    VERSION 1.0
    STATIC
    SOURCES ${_phosphor_animation_qml_sources}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/org/phosphor/animation
)

# Namespace alias for in-tree consumers.
add_library(PhosphorAnimation::PhosphorAnimationQml ALIAS PhosphorAnimationQml)

target_include_directories(PhosphorAnimationQml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        # The generated <module>_qmltyperegistrations.cpp guards each registered
        # type with `#if __has_include(<PhosphorCurve.h>)` using the BARE header
        # name, so the header's own directory must be on the include path for the
        # registration to resolve it. A non-unity build compiles that generated
        # TU alone (no batch-mate to declare the type), so without this it fails
        # with "PhosphorAnimation … not declared". PRIVATE — consumers still use
        # the <PhosphorAnimation/…> spelling via the PUBLIC include dir above.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/PhosphorAnimation>
)

target_compile_features(PhosphorAnimationQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorAnimationQml
    PUBLIC
        PhosphorAnimation::PhosphorAnimation
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickPrivate
        Qt6::Qml
)

set_target_properties(PhosphorAnimationQml PROPERTIES
    VERSION ${PHOSPHORANIMATION_VERSION}
    SOVERSION 0
)

# ===============================================================================
# QML Module — SHARED (install-only runtime plugin)
# ===============================================================================

option(PHOSPHOR_ANIMATION_QML_INSTALL
    "Also build a SHARED variant of org.phosphor.animation and install it"
    ON)

if(PHOSPHOR_ANIMATION_QML_INSTALL)
    qt_add_qml_module(PhosphorAnimationQmlShared
        URI org.phosphor.animation
        VERSION 1.0
        SHARED
        SOURCES ${_phosphor_animation_qml_sources}
        OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml_install/org/phosphor/animation
    )

    target_include_directories(PhosphorAnimationQmlShared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
            $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
        PRIVATE
            # Same bare-`__has_include(<PhosphorCurve.h>)` resolution as the
            # PhosphorAnimationQml target above — see the comment there.
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/PhosphorAnimation>
    )

    # The SHARED variant compiles the same sources under a different target
    # name; set the export define so PHOSPHORANIMATION_EXPORT resolves to
    # visibility("default") on the SHARED build too.
    target_compile_definitions(PhosphorAnimationQmlShared PRIVATE
        PhosphorAnimation_EXPORTS
    )

    target_link_libraries(PhosphorAnimationQmlShared
        PUBLIC
            PhosphorAnimation::PhosphorAnimation
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            Qt6::QuickPrivate
            Qt6::Qml
    )

    set_target_properties(PhosphorAnimationQmlShared PROPERTIES
        VERSION ${PHOSPHORANIMATION_VERSION}
        SOVERSION 0
    )

    set(_phosphor_animation_qml_shared_plugin_target PhosphorAnimationQmlSharedplugin)

    # The QML plugin and its backing PhosphorAnimationQmlShared library
    # install side by side in the QML module directory, which is not on
    # the linker's default search path. Give the plugin an $ORIGIN
    # install RPATH so it resolves the sibling library — both at runtime
    # and when Debian's dpkg-shlibdeps walks the dependency graph during
    # packaging. Its other dependencies (PhosphorAnimation, Qt) live in
    # the standard libdir and need no RPATH entry.
    set_target_properties(${_phosphor_animation_qml_shared_plugin_target} PROPERTIES
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# ===============================================================================
# Tests
# ===============================================================================

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorAnimation" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ===============================================================================
# Install
# ===============================================================================

if(NOT DEFINED KDE_INSTALL_BINDIR)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorAnimation
    EXPORT PhosphorAnimationTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphoranimation_public_HDRS} ${phosphoranimation_qml_HDRS}
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

# QML module install (SHARED variant)
if(PHOSPHOR_ANIMATION_QML_INSTALL)
    set(_phosphor_animation_qml_install_dir "${KDE_INSTALL_QMLDIR}/org/phosphor/animation")

    install(TARGETS PhosphorAnimationQmlShared ${_phosphor_animation_qml_shared_plugin_target}
        EXPORT PhosphorAnimationQmlTargets
        RUNTIME DESTINATION ${_phosphor_animation_qml_install_dir}
        LIBRARY DESTINATION ${_phosphor_animation_qml_install_dir}
        ARCHIVE DESTINATION ${_phosphor_animation_qml_install_dir}
    )

    install(FILES
            ${CMAKE_BINARY_DIR}/qml_install/org/phosphor/animation/qmldir
            ${CMAKE_BINARY_DIR}/qml_install/org/phosphor/animation/PhosphorAnimationQmlShared.qmltypes
        DESTINATION ${_phosphor_animation_qml_install_dir}
    )
endif()
