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

# PhosphorMotionAnimation inherits QQuickPropertyAnimation which lives
# in a private Qt Quick header (<QtQuick/private/qquickanimation_p.h>).
# The Qt6::QuickPrivate target provides the include path for these
# headers — mirrors the Qt6WaylandClientPrivate pattern in src/.
if(NOT TARGET Qt6::QuickPrivate)
    find_package(Qt6QuickPrivate REQUIRED)
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
# Shared sources — listed once, used by both STATIC and SHARED modules
# ═══════════════════════════════════════════════════════════════════════════════

set(_phosphor_animation_qml_sources
    phosphoranimation_qml_plugin.cpp
    phosphorcurve.cpp
    qtquickclockmanager.cpp
    phosphoranimatedvaluebase.cpp
    phosphoranimatedreal.cpp
    phosphoranimatedpoint.cpp
    phosphoranimatedsize.cpp
    phosphoranimatedrect.cpp
    phosphoranimatedcolor.cpp
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
    ../include/PhosphorAnimation/qml/PhosphorMotionAnimation.h
    ../include/PhosphorAnimation/qml/detail/PhosphorAnimatedValueImpl.h
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

add_library(PhosphorAnimation::PhosphorAnimationQml ALIAS PhosphorAnimationQml)

target_include_directories(PhosphorAnimationQml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

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
    VERSION ${PHOSPHORANIMATION_VERSION}
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
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
            $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
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

if(PHOSPHOR_ANIMATION_QML_INSTALL)
    # Target path on disk under the QML import root — matches the module
    # URI (org.phosphor.animation) so Qt's resolver finds it via the
    # standard `import org.phosphor.animation 1.0` search.
    set(_phosphor_animation_qml_install_dir "${KDE_INSTALL_QMLDIR}/org/phosphor/animation")

    # Install the SHARED backing library AND its generated plugin MODULE
    # side by side in the QML import path. Qt's QML loader opens the
    # plugin library named in `qmldir` (PhosphorAnimationQmlplugin); the
    # backing library is next to it because Qt's `linktarget` field in
    # `qmldir` lets the loader resolve the backing library via the
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

    # Export the QML module's CMake target set so downstream CMake
    # consumers can `find_package(PhosphorAnimation COMPONENTS QML)` and
    # link `PhosphorAnimation::PhosphorAnimationQmlShared`. Landing this
    # alongside PhosphorAnimationTargets in the same cmake dir keeps
    # the installed layout single-rooted.
    install(EXPORT PhosphorAnimationQmlTargets
        FILE PhosphorAnimationQmlTargets.cmake
        NAMESPACE PhosphorAnimation::
        DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorAnimation
    )
endif()

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
        ../include/PhosphorAnimation/qml/PhosphorMotionAnimation.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimation/qml
)

install(FILES
        ../include/PhosphorAnimation/qml/detail/PhosphorAnimatedValueImpl.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorAnimation/qml/detail
)
