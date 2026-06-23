# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorTheme, design-token store + QML theme module for Phosphor shells.
#
# Holds the active M3 token palette, watches the on-disk JSON palette for
# hot-reload, and exposes everything to QML as a `Phosphor.Theme` import
# (Theme / Tokens / Motion / StateLayer singletons).
#
# Phase 1.1 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORTHEME_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorTheme VERSION ${PHOSPHORTHEME_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick)

# Qt 6.5+ resource-prefix policy. NEW = use `:/qt/qml/` as the QML
# module resource prefix. The `qt_policy` command is what we actually
# need; it's available on every Qt CMake build new enough to publish
# the QTP0001 policy.
if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# Position-independent code is required for Qt's QML type-registration
# sub-targets.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphortheme_public_HDRS
    include/PhosphorTheme/PhosphorTheme.h
    include/PhosphorTheme/IThemeService.h
    include/PhosphorTheme/MatugenRunner.h
    include/PhosphorTheme/PaletteStore.h
    include/PhosphorTheme/TemplateEngine.h
)

set(phosphortheme_SRCS
    src/defaultpalette.cpp
    src/matugenrunner.cpp
    src/palettestore.cpp
    src/templateengine.cpp
)

add_library(PhosphorTheme SHARED
    ${phosphortheme_public_HDRS}
    ${phosphortheme_SRCS}
)

add_library(PhosphorTheme::PhosphorTheme ALIAS PhosphorTheme)

generate_export_header(PhosphorTheme
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorTheme/phosphortheme_export.h
    EXPORT_MACRO_NAME PHOSPHORTHEME_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

target_include_directories(PhosphorTheme
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorTheme PUBLIC cxx_std_20)

target_link_libraries(PhosphorTheme
    PUBLIC
        Qt6::Core
        Qt6::Gui
        # PaletteStore carries QML_ELEMENT / QML_SINGLETON macros from
        # <QtQmlIntegration/qqmlintegration.h>. The macros expand to
        # markers that AUTOMOC and qmltyperegistrar pick up at build
        # time; the header itself is part of Qt6::Qml. Phosphor-theme
        # is shell-side, not engine-side, so the engine-QML firewall
        # in the top-level CMakeLists doesn't apply here.
        Qt6::Qml
)

set_target_properties(PhosphorTheme PROPERTIES
    VERSION ${PHOSPHORTHEME_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    # Unity builds merge anonymous-namespace helpers across TUs and
    # surface spurious "defined but not used" warnings on this tree's
    # small files. The translation units are small enough that unity
    # buys nothing here.
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# QML module, STATIC (in-tree linkable)
# ═══════════════════════════════════════════════════════════════════════════════
#
# C++ side: PaletteStore is registered as a QML singleton so QML files can
# `import Phosphor.Theme` and reference it. The four QML-file singletons
# (Theme / Tokens / Motion / StateLayer) live alongside.
#
# QML singletons are *not* process-global statics, Qt instantiates one per
# QQmlEngine. Each shell process gets exactly one engine, so there's one
# PaletteStore per shell, which is what we want. The `IThemeService` interface
# in the header is the seam for tests and alternate implementations to swap
# the store via `qmlRegisterSingletonInstance` before QML import.

set(_phosphor_theme_qml_singletons
    qml/Phosphor/Theme/Theme.qml
    qml/Phosphor/Theme/Tokens.qml
    qml/Phosphor/Theme/Motion.qml
    qml/Phosphor/Theme/StateLayer.qml
)

# qt_add_qml_module's default resource layout nests each QML_FILES entry
# under the module's URI path, then UNDER the file's source-relative
# path, so `qml/Phosphor/Theme/Theme.qml` becomes
# `qrc:/qt/qml/Phosphor/Theme/qml/Phosphor/Theme/Theme.qml`. The doubled
# path causes the engine to fail to resolve singletons declared in the
# same module ("PaletteStore is not defined" inside Theme.qml). Pin
# each file's resource path to the basename so the resource URL matches
# the module's URI cleanly.
foreach(_qml_singleton IN LISTS _phosphor_theme_qml_singletons)
    get_filename_component(_qml_basename ${_qml_singleton} NAME)
    set_source_files_properties(${_qml_singleton}
        PROPERTIES
            QT_QML_SINGLETON_TYPE TRUE
            QT_RESOURCE_ALIAS ${_qml_basename}
    )
endforeach()

set(_phosphor_theme_qml_sources
    src/phosphortheme_qml_plugin.cpp
    # The headers below are added so AUTOMOC on the QML module target
    # emits metatype JSON for each, which qmltyperegistrar picks up to
    # generate the QML type registrations the plugin loads at startup.
    # The IMPLEMENTATIONS still live in PhosphorTheme.so (linked below);
    # the QML module only needs the type metadata.
    include/PhosphorTheme/PaletteStore.h
    include/PhosphorTheme/MatugenRunner.h
    include/PhosphorTheme/TemplateEngine.h
)

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here: Qt adds <target>_autogen/include to each
# sub-target's -I list, but when a module carries no Q_OBJECT sources AUTOMOC
# never creates that directory (and --clean-first deletes it), so cc1plus
# warns on a Qt-managed scratch path. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(PhosphorThemeQml
    URI Phosphor.Theme
    VERSION 1.0
    STATIC
    SOURCES ${_phosphor_theme_qml_sources}
    QML_FILES ${_phosphor_theme_qml_singletons}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Phosphor/Theme
)

add_library(PhosphorTheme::PhosphorThemeQml ALIAS PhosphorThemeQml)

target_include_directories(PhosphorThemeQml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        # Qt 6.10+ qmltyperegistrar emits the generated *_qmltyperegistrations.cpp
        # #includes by header basename, so the header subdirectory must be on the
        # path for those includes to resolve.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/PhosphorTheme>
)

target_compile_features(PhosphorThemeQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorThemeQml
    PUBLIC
        PhosphorTheme::PhosphorTheme
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

# ═══════════════════════════════════════════════════════════════════════════════
# QML Module — SHARED (install-only runtime plugin)
# ═══════════════════════════════════════════════════════════════════════════════
# The STATIC variant above is what in-tree consumers link against (bakes
# the QML module's resources into the consuming binary's qrc). The SHARED
# variant exists purely so `cmake --install build` lands a runtime-
# discoverable Phosphor.Theme module under ${KDE_INSTALL_QMLDIR}/Phosphor/
# Theme/. Without it, an installed phosphor-shell binary loads shell.qml
# from XDG paths and fails to import Phosphor.Theme because the engine
# searches `${KDE_INSTALL_QMLDIR}` first and finds nothing. Mirrors the
# pattern in phosphor-animation.

option(PHOSPHOR_THEME_QML_INSTALL
    "Also build a SHARED variant of Phosphor.Theme and install it"
    ON)

if(PHOSPHOR_THEME_QML_INSTALL)
    qt_add_qml_module(PhosphorThemeQmlShared
        URI Phosphor.Theme
        VERSION 1.0
        SHARED
        SOURCES ${_phosphor_theme_qml_sources}
        QML_FILES ${_phosphor_theme_qml_singletons}
        OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml_install/Phosphor/Theme
    )

    target_include_directories(PhosphorThemeQmlShared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
            $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
        PRIVATE
            # Qt 6.10+ qmltyperegistrar emits basename #includes in the generated
            # *_qmltyperegistrations.cpp; the header subdirectory must be on the path.
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/PhosphorTheme>
    )

    # Visibility-define parity with the STATIC build so
    # PHOSPHORTHEME_EXPORT resolves to visibility("default") here too.
    target_compile_definitions(PhosphorThemeQmlShared PRIVATE
        PhosphorTheme_EXPORTS
    )

    target_compile_features(PhosphorThemeQmlShared PUBLIC cxx_std_20)

    target_link_libraries(PhosphorThemeQmlShared
        PUBLIC
            PhosphorTheme::PhosphorTheme
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
    )

    set_target_properties(PhosphorThemeQmlShared PROPERTIES
        VERSION ${PHOSPHORTHEME_VERSION}
        SOVERSION 0
    )

    set(_phosphor_theme_qml_shared_plugin_target PhosphorThemeQmlSharedplugin)

    # The QML plugin and its backing PhosphorThemeQmlShared library install
    # side by side in the QML module directory, which is not on the
    # linker's default search path. Give the plugin an $ORIGIN install
    # RPATH so it resolves the sibling library both at runtime and when
    # dpkg-shlibdeps walks the dependency graph during packaging. Its
    # other dependencies (PhosphorTheme, Qt) live in the standard libdir
    # and need no RPATH entry.
    #
    # PREFIX "lib" — Qt's QML plugin loader on Linux searches for
    # `lib<plugin>.so` (see `/usr/lib/qt6/qml/QtCore/libqtqmlcoreplugin.so`
    # for the canonical Qt-shipped example). CMake's MODULE library
    # default for QML plugins drops the lib prefix; Qt then can't find
    # the file and reports `module "..." plugin "..." not found` even
    # though the .so is present in the module dir.
    set_target_properties(${_phosphor_theme_qml_shared_plugin_target} PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        PREFIX "lib"
    )
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_BINDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()
if(NOT DEFINED KDE_INSTALL_QMLDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_QMLDIR ${CMAKE_INSTALL_LIBDIR}/qt6/qml)
endif()

install(TARGETS PhosphorTheme
    EXPORT PhosphorThemeTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphortheme_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorTheme
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorTheme/phosphortheme_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorTheme
)

install(EXPORT PhosphorThemeTargets
    FILE PhosphorThemeTargets.cmake
    NAMESPACE PhosphorTheme::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorTheme
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorThemeConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorThemeConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorTheme
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorThemeConfigVersion.cmake"
    VERSION ${PHOSPHORTHEME_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorThemeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorThemeConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorTheme
)

# ═══════════════════════════════════════════════════════════════════════════════
# QML module install (SHARED variant)
# ═══════════════════════════════════════════════════════════════════════════════

if(PHOSPHOR_THEME_QML_INSTALL)
    set(_phosphor_theme_qml_install_dir "${KDE_INSTALL_QMLDIR}/Phosphor/Theme")

    install(TARGETS PhosphorThemeQmlShared ${_phosphor_theme_qml_shared_plugin_target}
        EXPORT PhosphorThemeQmlTargets
        RUNTIME DESTINATION ${_phosphor_theme_qml_install_dir}
        LIBRARY DESTINATION ${_phosphor_theme_qml_install_dir}
        ARCHIVE DESTINATION ${_phosphor_theme_qml_install_dir}
    )

    install(FILES
            ${CMAKE_BINARY_DIR}/qml_install/Phosphor/Theme/qmldir
            ${CMAKE_BINARY_DIR}/qml_install/Phosphor/Theme/PhosphorThemeQmlShared.qmltypes
        DESTINATION ${_phosphor_theme_qml_install_dir}
    )

    # The QML singletons are listed in qmldir but must also be present
    # as files in the module dir for the QML engine to load them. Qt's
    # qt_add_qml_module stages copies into the OUTPUT_DIRECTORY; install
    # those rather than the source-tree copies so any future codegen step
    # that transforms them still ships.
    foreach(_qml_singleton IN LISTS _phosphor_theme_qml_singletons)
        get_filename_component(_qml_basename ${_qml_singleton} NAME)
        install(FILES
                ${CMAKE_BINARY_DIR}/qml_install/Phosphor/Theme/${_qml_basename}
            DESTINATION ${_phosphor_theme_qml_install_dir}
        )
    endforeach()
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorTheme" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
