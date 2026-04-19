# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorAnimation-QML — Qt Quick wrappers for the phosphor-animation
# motion runtime.
#
# Phase 4 decision M: declarative module registration via
# qt_add_qml_module so in-tree and out-of-tree consumers (Wayfire
# plugin, Quickshell shell, third-party editors) resolve the module
# identically through Qt's plugin discovery. URI is
# `org.phosphor.animation` per decision L — extraction-ready,
# independent of the PlasmaZones namespace.

find_package(Qt6 6.6 REQUIRED COMPONENTS Quick Qml QuickTest Test)

# Position-independent code is required for Qt's QML type-registration
# sub-targets — mirrors the org.plasmazones.common module's convention
# in src/shared/CMakeLists.txt. Must be set BEFORE the qt_add_qml_module
# call so every generated sub-target inherits it.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Suppress -Wmissing-include-dirs for Qt-managed AUTOMOC scratch dirs
# (same rationale as src/shared/CMakeLists.txt — the autogen/include
# path is populated as a build-time side effect).
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorAnimationQml
    URI org.phosphor.animation
    VERSION 1.0
    SOURCES
        phosphoranimation_qml_plugin.cpp
        phosphorcurve.cpp
        qtquickclockmanager.cpp
        phosphoranimatedvaluebase.cpp
        phosphoranimatedreal.cpp
        phosphoranimatedpoint.cpp
        phosphoranimatedsize.cpp
        phosphoranimatedrect.cpp
        phosphoranimatedcolor.cpp
        phosphorprofileregistry.cpp
        phosphormotionanimation.cpp
        ../include/PhosphorAnimation/qml/PhosphorEasing.h
        ../include/PhosphorAnimation/qml/PhosphorSpring.h
        ../include/PhosphorAnimation/qml/PhosphorCurve.h
        ../include/PhosphorAnimation/qml/PhosphorProfile.h
        ../include/PhosphorAnimation/qml/QtQuickClockManager.h
        ../include/PhosphorAnimation/qml/PhosphorAnimatedValueBase.h
        ../include/PhosphorAnimation/qml/PhosphorAnimatedReal.h
        ../include/PhosphorAnimation/qml/PhosphorAnimatedPoint.h
        ../include/PhosphorAnimation/qml/PhosphorAnimatedSize.h
        ../include/PhosphorAnimation/qml/PhosphorAnimatedRect.h
        ../include/PhosphorAnimation/qml/PhosphorAnimatedColor.h
        ../include/PhosphorAnimation/qml/PhosphorProfileRegistry.h
        ../include/PhosphorAnimation/qml/PhosphorMotionAnimation.h
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/org/phosphor/animation
)

add_library(PhosphorAnimation::PhosphorAnimationQml ALIAS PhosphorAnimationQml)

target_include_directories(PhosphorAnimationQml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
)

target_link_libraries(PhosphorAnimationQml
    PUBLIC
        PhosphorAnimation::PhosphorAnimation
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
)

set_target_properties(PhosphorAnimationQml PROPERTIES
    VERSION ${PHOSPHORANIMATION_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorAnimation" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_QMLDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_QMLDIR ${CMAKE_INSTALL_LIBDIR}/qt6/qml)
endif()

# Install the QML module to Qt's standard import path so external
# consumers (out-of-tree Wayfire plugin, Quickshell shell, third-party
# KCMs) can `import org.phosphor.animation` and Qt resolves it via
# normal discovery.
install(TARGETS PhosphorAnimationQml
    RUNTIME DESTINATION ${KDE_INSTALL_QMLDIR}/org/phosphor/animation
    LIBRARY DESTINATION ${KDE_INSTALL_QMLDIR}/org/phosphor/animation
    ARCHIVE DESTINATION ${KDE_INSTALL_QMLDIR}/org/phosphor/animation
)

install(FILES
        ../include/PhosphorAnimation/qml/PhosphorEasing.h
        ../include/PhosphorAnimation/qml/PhosphorSpring.h
        ../include/PhosphorAnimation/qml/PhosphorCurve.h
        ../include/PhosphorAnimation/qml/PhosphorProfile.h
        ../include/PhosphorAnimation/qml/QtQuickClockManager.h
        ../include/PhosphorAnimation/qml/PhosphorAnimatedValueBase.h
        ../include/PhosphorAnimation/qml/PhosphorAnimatedReal.h
        ../include/PhosphorAnimation/qml/PhosphorAnimatedPoint.h
        ../include/PhosphorAnimation/qml/PhosphorAnimatedSize.h
        ../include/PhosphorAnimation/qml/PhosphorAnimatedRect.h
        ../include/PhosphorAnimation/qml/PhosphorAnimatedColor.h
        ../include/PhosphorAnimation/qml/PhosphorProfileRegistry.h
        ../include/PhosphorAnimation/qml/PhosphorMotionAnimation.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimation/qml
)
