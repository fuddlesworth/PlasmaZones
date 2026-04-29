# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorAnimationQml — Qt Quick wrappers for the phosphor-animation
# motion runtime.
#
# Sibling library to phosphor-animation. Owns the Q_GADGET / Q_OBJECT
# wrappers (PhosphorEasing, PhosphorSpring, PhosphorCurve, PhosphorProfile,
# QtQuickClockManager singleton, per-T PhosphorAnimatedValue<T> wrappers,
# PhosphorMotionAnimation Behavior driver) plus the org.phosphor.animation
# QML module declaration.
#
# Phase A1 of docs/phosphor-architecture-refactor-plan.md — promoted
# out of a sub-directory of phosphor-animation into a top-level sibling
# lib so the core motion runtime stays Qt6 Core+Gui only and doesn't
# transitively pull Qt Quick into compositor/daemon consumers that
# don't drive QML animations.
#
# QML URI is `org.phosphor.animation` — extraction-ready, independent of
# the PlasmaZones namespace. The eventual rename to Phosphor.Animation
# is a separate post-rebrand concern.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORANIMATIONQML_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorAnimationQml VERSION ${PHOSPHORANIMATIONQML_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

# Pin to 6.6 to match sibling phosphor-* libraries.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Quick Qml)

# PhosphorMotionAnimation inherits QQuickPropertyAnimation which lives
# in a private Qt Quick header (<QtQuick/private/qquickanimation_p.h>).
# The Qt6::QuickPrivate target provides the include path for these
# headers — mirrors the Qt6WaylandClientPrivate pattern in src/.
if(NOT TARGET Qt6::QuickPrivate)
    find_package(Qt6QuickPrivate CONFIG REQUIRED)
endif()

# phosphor-animation — motion runtime that the QML wrappers expose to
# QML code (Curve, Profile, AnimatedValue<T>, IMotionClock + QtQuickClock).
# find_package is a no-op when building in-tree (the parent CMakeLists
# adds the target first); the guard lets the lib resolve an installed
# PhosphorAnimation when built standalone.
if(NOT TARGET PhosphorAnimation::PhosphorAnimation)
    find_package(PhosphorAnimation 0.1.0 REQUIRED)
endif()

# Qt 6.5+ resource-prefix policy. NEW = use `:/qt/qml/` as the QML module
# resource prefix, the documented default. Setting it explicitly silences
# the Qt6CoreMacros QTP0001 dev warning on the two qt_add_qml_module calls
# below; the policy stays in scope for this directory's qml-module calls.
if(POLICY CMP0090 OR COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# Position-independent code is required for Qt's QML type-registration
# sub-targets — mirrors the org.plasmazones.common module's convention
# in src/shared/CMakeLists.txt. Must be set BEFORE the qt_add_qml_module
# call so every generated sub-target inherits it.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Suppress -Wmissing-include-dirs for Qt-managed AUTOMOC scratch dirs
# (same rationale as src/shared/CMakeLists.txt — the autogen/include
# path is populated as a build-time side effect).
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

if(NOT DEFINED KDE_INSTALL_QMLDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_QMLDIR ${CMAKE_INSTALL_LIBDIR}/qt6/qml)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install toggle
# ═══════════════════════════════════════════════════════════════════════════════
#
# An installed Qt QML module is loaded by the QML engine at runtime from
# ${KDE_INSTALL_QMLDIR}/org/phosphor/animation/: a SHARED plugin library
# plus its `qmldir` and `plugins.qmltypes`. A STATIC `.a` cannot be
# opened by `QPluginLoader`, so a STATIC-only build installs correctly
# on disk but is unusable via `import org.phosphor.animation` — Qt
# silently falls through every import path.
#
# Default ON because the typical production build installs. Toggle OFF
# to skip the extra SHARED compilation in pure in-tree setups (embedded
# out-of-tree consumers that vendor the library source and only want
# the STATIC registration shape).
option(PHOSPHOR_ANIMATION_QML_INSTALL
    "Also build a SHARED variant of org.phosphor.animation and install it at \${KDE_INSTALL_QMLDIR}/org/phosphor/animation (required for out-of-tree `import org.phosphor.animation` to resolve)"
    ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Public headers
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphoranimationqml_public_HDRS
    include/PhosphorAnimationQml/PhosphorEasing.h
    include/PhosphorAnimationQml/PhosphorSpring.h
    include/PhosphorAnimationQml/PhosphorCurve.h
    include/PhosphorAnimationQml/PhosphorProfile.h
    include/PhosphorAnimationQml/QtQuickClockManager.h
    include/PhosphorAnimationQml/PhosphorAnimatedValueBase.h
    include/PhosphorAnimationQml/PhosphorAnimatedReal.h
    include/PhosphorAnimationQml/PhosphorAnimatedPoint.h
    include/PhosphorAnimationQml/PhosphorAnimatedSize.h
    include/PhosphorAnimationQml/PhosphorAnimatedRect.h
    include/PhosphorAnimationQml/PhosphorAnimatedColor.h
    include/PhosphorAnimationQml/PhosphorMotionAnimation.h
)

set(phosphoranimationqml_detail_HDRS
    include/PhosphorAnimationQml/detail/PhosphorAnimatedValueImpl.h
    include/PhosphorAnimationQml/detail/PhosphorAnimatedTypeMacro.h
)

# ═══════════════════════════════════════════════════════════════════════════════
# Shared sources — listed once, used by both STATIC and SHARED modules
# ═══════════════════════════════════════════════════════════════════════════════

set(_phosphor_animation_qml_sources
    src/phosphoranimation_qml_plugin.cpp
    src/phosphorcurve.cpp
    src/qtquickclockmanager.cpp
    src/phosphoranimatedvaluebase.cpp
    src/phosphoranimatedreal.cpp
    src/phosphoranimatedpoint.cpp
    src/phosphoranimatedsize.cpp
    src/phosphoranimatedrect.cpp
    src/phosphoranimatedcolor.cpp
    src/phosphormotionanimation.cpp
    ${phosphoranimationqml_public_HDRS}
    ${phosphoranimationqml_detail_HDRS}
)

# ═══════════════════════════════════════════════════════════════════════════════
# STATIC module — in-tree linkable (plasmazonesd, plasmazones-settings,
# plasmazones-editor, plasmazones_shared_qml)
# ═══════════════════════════════════════════════════════════════════════════════
#
# STATIC so in-tree consumers link the module directly and every
# QQmlEngine in the process picks up the registration without
# QML_IMPORT_PATH plumbing. Mirrors the `org.plasmazones.common`
# pattern in src/shared/CMakeLists.txt — qrc-loaded overlay QML in the
# daemon resolves the module the moment the plugin's library is linked.
#
# External consumers that vendor the library source link the same
# STATIC target — a SHARED companion for out-of-tree `import`
# resolution is built below when PHOSPHOR_ANIMATION_QML_INSTALL is ON.

qt_add_qml_module(PhosphorAnimationQml
    URI org.phosphor.animation
    VERSION 1.0
    STATIC
    SOURCES ${_phosphor_animation_qml_sources}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/org/phosphor/animation
)

# Namespace alias kept under the PhosphorAnimation:: prefix because
# every in-tree consumer already links `PhosphorAnimation::PhosphorAnimationQml`.
# The alias names the umbrella project family, not the on-disk lib
# directory; sibling lib boundaries don't change the public consumer
# surface.
add_library(PhosphorAnimation::PhosphorAnimationQml ALIAS PhosphorAnimationQml)

# Export header generated under PhosphorAnimationQml/ prefix so the same
# `#include <PhosphorAnimationQml/phosphoranimationqml_export.h>` resolves
# in both in-tree builds (via BUILD_INTERFACE below) and installed
# consumers.
generate_export_header(PhosphorAnimationQml
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationQml/phosphoranimationqml_export.h
    EXPORT_MACRO_NAME PHOSPHORANIMATIONQML_EXPORT
)

target_include_directories(PhosphorAnimationQml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorAnimationQml PUBLIC cxx_std_20)

# Qt6::QuickPrivate stays PUBLIC because the INSTALLED public header
# PhosphorMotionAnimation.h physically `#include`s
# <QtQuick/private/qquickanimation_p.h> — the QQuickPropertyAnimation
# base class lives under QtQuick/private/* with no public alias
# (documented at the top of that header). Consumers of the installed
# headers therefore need the private include path too; demoting this
# to PRIVATE would let the library compile but break every downstream
# that includes the header. See PhosphorMotionAnimation.h's header
# comment for the full Qt version compatibility policy.
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
    VERSION ${PHOSPHORANIMATIONQML_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# SHARED module — install-only runtime plugin at ${KDE_INSTALL_QMLDIR}
# ═══════════════════════════════════════════════════════════════════════════════
#
# Produced as a separate target (PhosphorAnimationQmlShared) because Qt
# forbids mixing a STATIC backing library with a SHARED plugin (see the
# check in Qt6QmlMacros.cmake: "Mixing a static backing library with a
# non-static plugin is not supported"). The two modules compile the same
# sources under the same URI; at install time only the SHARED variant's
# artifacts (plugin library, qmldir, qmltypes) land under
# ${KDE_INSTALL_QMLDIR}/org/phosphor/animation, so the QML loader's
# runtime discovery resolves a valid SHARED-plugin module.
#
# The duplicated compilation cost is accepted as the price of having one
# build artifact serve both registration models. A future Qt API that
# produces a dual STATIC+SHARED module from a single declaration would
# let us collapse this back.

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
    )

    # generate_export_header above defines PhosphorAnimationQml_EXPORTS only
    # while compiling the STATIC target; the SHARED variant compiles the
    # same sources under a different target name and so needs the symbol
    # set explicitly. Without this PHOSPHORANIMATIONQML_EXPORT decays to
    # `__attribute__((visibility("default")))` only on the STATIC build,
    # leading to silently-private SHARED-plugin symbols on dlopen.
    target_compile_definitions(PhosphorAnimationQmlShared PRIVATE
        PhosphorAnimationQml_EXPORTS
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
        VERSION ${PHOSPHORANIMATIONQML_VERSION}
        SOVERSION 0
    )

    # The auto-generated plugin target follows `${target}plugin`, so for
    # our SHARED backing `PhosphorAnimationQmlShared` the plugin target
    # is `PhosphorAnimationQmlSharedplugin`. qt_add_qml_module writes
    # this name into the generated `qmldir`'s `plugin` / `linktarget`
    # fields; the on-disk file is therefore `PhosphorAnimationQmlSharedplugin.so`
    # (the plugin MODULE library type doesn't carry a `lib` prefix on
    # Linux — Qt emits the file named after the target directly under
    # the OUTPUT_DIRECTORY). No OUTPUT_NAME override here because that
    # would desynchronise the qmldir declaration from the on-disk
    # filename, silently breaking `import org.phosphor.animation` with
    # the generic "module not installed" error and no other diagnostic.
    #
    # The "Shared" suffix in the installed plugin filename is a minor
    # aesthetic cost — invisible to end users, who interact with the
    # module through the `import org.phosphor.animation` URI — and a
    # worthwhile price for keeping the qmldir invariant honest.
    set(_phosphor_animation_qml_shared_plugin_target PhosphorAnimationQmlSharedplugin)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorAnimationQml" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_BINDIR)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

# Only the SHARED runtime plugin is installed and exported. The STATIC
# backing target is an in-tree convenience for the daemon / settings /
# editor binaries (linked via `PhosphorAnimation::PhosphorAnimationQml`)
# — packaging it for installed consumers would also require shipping
# Qt's auto-generated `PhosphorAnimationQml_resources_1` target alongside,
# which is implementation-detail. Out-of-tree consumers resolve
# `import org.phosphor.animation` through the SHARED variant below
# regardless, so the STATIC export brings no consumer benefit.

if(PHOSPHOR_ANIMATION_QML_INSTALL)
    # Target path on disk under the QML import root — matches the module
    # URI (org.phosphor.animation) so Qt's resolver finds it via the
    # standard `import org.phosphor.animation 1.0` search.
    set(_phosphor_animation_qml_install_dir "${KDE_INSTALL_QMLDIR}/org/phosphor/animation")

    # Install the SHARED backing library AND its generated plugin MODULE
    # side by side in the QML import path. Qt's QML loader opens the
    # plugin library named in `qmldir` (PhosphorAnimationQmlSharedplugin);
    # the backing library is next to it because Qt's `linktarget` field
    # in `qmldir` lets the loader resolve the backing library via the
    # usual RPATH / loader search.
    install(TARGETS PhosphorAnimationQmlShared ${_phosphor_animation_qml_shared_plugin_target}
        EXPORT PhosphorAnimationQmlTargets
        RUNTIME DESTINATION ${_phosphor_animation_qml_install_dir}
        LIBRARY DESTINATION ${_phosphor_animation_qml_install_dir}
        ARCHIVE DESTINATION ${_phosphor_animation_qml_install_dir}
    )

    # Install the qmldir + qmltypes alongside the plugin. These are
    # generated by the SHARED qt_add_qml_module into its OUTPUT_DIRECTORY —
    # install from that source path so we pick up the freshly regenerated
    # files on every build rather than a stale copy. `qt_add_qml_module`
    # emits the qmltypes with the target name; qmldir's `typeinfo` field
    # already points at that filename so we don't rename on install.
    install(FILES
            ${CMAKE_BINARY_DIR}/qml_install/org/phosphor/animation/qmldir
            ${CMAKE_BINARY_DIR}/qml_install/org/phosphor/animation/PhosphorAnimationQmlShared.qmltypes
        DESTINATION ${_phosphor_animation_qml_install_dir}
    )
endif()

install(FILES ${phosphoranimationqml_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimationQml
)
install(FILES ${phosphoranimationqml_detail_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimationQml/detail
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationQml/phosphoranimationqml_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimationQml
)

# The CMake package config and target export only ship when the SHARED
# plugin is built, since the export set names only the SHARED targets.
# In-tree consumers don't go through find_package — they get the alias
# the moment add_subdirectory(libs/phosphor-animation-qml) runs.
if(PHOSPHOR_ANIMATION_QML_INSTALL)
    install(EXPORT PhosphorAnimationQmlTargets
        FILE PhosphorAnimationQmlTargets.cmake
        NAMESPACE PhosphorAnimation::
        DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorAnimationQml
    )

    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorAnimationQmlConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationQmlConfig.cmake"
        INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorAnimationQml
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationQmlConfigVersion.cmake"
        VERSION ${PHOSPHORANIMATIONQML_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationQmlConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/PhosphorAnimationQmlConfigVersion.cmake"
        DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorAnimationQml
    )
endif()
