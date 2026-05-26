# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorSettingsUi — reusable Qt6/QML/Kirigami settings-app framework
#
# Provides the chrome (window, sidebar, breadcrumbs, footer, page host)
# and orchestration (PageController base, PageRegistry, IStagingDomain,
# ApplicationController) that any Qt6/Kirigami settings app can build on,
# so each consumer only writes its own page controllers + QML pages.
#
# Boundary:
#   - phosphor-config owns the IBackend/IGroup configuration store. This
#     library does NOT duplicate that contract. Consumers wire their own
#     ISettings implementation against phosphor-config.
#   - phosphor-shortcuts owns global-shortcut backends. Not included here.
#
# Built standalone or as a subdirectory of PlasmaZones (or any consumer).

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSETTINGSUI_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorSettingsUi VERSION ${PHOSPHORSETTINGSUI_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core DBus Gui Qml Quick)

# Qt 6.5+ QML policies:
#   QTP0001 — use `:/qt/qml/` as the QML module resource prefix (NEW).
#             Matches the RESOURCE_PREFIX we set on qt_add_qml_module below.
#   QTP0004 — auto-generate qmldir files for QML subdirectories (`qml/`)
#             so module type resolution finds them. Without NEW, Qt warns
#             "You need qmldir files for each extra directory".
if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorsettingsui_public_HDRS
    include/PhosphorSettingsUi/PhosphorSettingsUi.h
    include/PhosphorSettingsUi/StagingDomain.h
    include/PhosphorSettingsUi/PageController.h
    include/PhosphorSettingsUi/PageRegistry.h
    include/PhosphorSettingsUi/ApplicationController.h
    include/PhosphorSettingsUi/LocalizedContext.h
    include/PhosphorSettingsUi/DBusBridge.h
)

set(phosphorsettingsui_SRCS
    src/phosphorsettingsui.cpp
    src/stagingdomain.cpp
    src/pagecontroller.cpp
    src/pageregistry.cpp
    src/applicationcontroller.cpp
    src/localizedcontext.cpp
    src/dbusbridge.cpp
)

add_library(PhosphorSettingsUi SHARED
    ${phosphorsettingsui_public_HDRS}
    ${phosphorsettingsui_SRCS}
)

add_library(PhosphorSettingsUi::PhosphorSettingsUi ALIAS PhosphorSettingsUi)

generate_export_header(PhosphorSettingsUi
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorsettingsui_export.h
    EXPORT_MACRO_NAME PHOSPHORSETTINGSUI_EXPORT
)

# Resolve KDE_INSTALL_INCLUDEDIR locally so INSTALL_INTERFACE can reference
# it. Mirrors the pattern in libs/phosphor-config/CMakeLists.txt.
if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorSettingsUi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorSettingsUi PUBLIC cxx_std_20)

target_link_libraries(PhosphorSettingsUi
    PUBLIC
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
)

set_target_properties(PhosphorSettingsUi PROPERTIES
    VERSION ${PHOSPHORSETTINGSUI_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# Surface the CMake version to the C++ version() accessor so the two
# stay in sync (avoid drift between the project metadata + a hardcoded
# string literal in src/phosphorsettingsui.cpp).
target_compile_definitions(PhosphorSettingsUi PRIVATE
    PHOSPHORSETTINGSUI_VERSION_STR="${PHOSPHORSETTINGSUI_VERSION}"
)

# ═══════════════════════════════════════════════════════════════════════════════
# QML Module — org.phosphor.settings.ui (STATIC, in-tree linkable)
#
# The C++ library above intentionally avoids Qt6::Qml/Quick so non-QML
# consumers (e.g. headless tests) stay light. Foreign type registrations
# live in qml/foreigntypes.cpp, processed only by this target.
# ═══════════════════════════════════════════════════════════════════════════════

# The QML module re-compiles the same public headers + sources as the C++
# lib so qmltyperegistrar sees the QML_NAMED_ELEMENT markers and emits
# registrations into org.phosphor.settings.ui. This mirrors phosphor-animation.

set(phosphorsettingsui_qml_FILES
    qml/SettingsAppWindow.qml
    qml/Sidebar.qml
    qml/Breadcrumbs.qml
    qml/UnsavedChangesFooter.qml
    qml/PageHost.qml
    qml/DiscardChangesDialog.qml
    qml/AboutPageShell.qml
)

qt_add_qml_module(PhosphorSettingsUiQml
    URI org.phosphor.settings.ui
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    SOURCES
        ${phosphorsettingsui_public_HDRS}
        ${phosphorsettingsui_SRCS}
    QML_FILES ${phosphorsettingsui_qml_FILES}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/org/phosphor/settings/ui
)

# The QML target compiles the same .cpp files as the C++ SHARED library;
# set the export define so PHOSPHORSETTINGSUI_EXPORT resolves to
# visibility("default") on both builds. Mirrors phosphor-animation.
# Also surface PHOSPHORSETTINGSUI_VERSION_STR so the shared
# phosphorsettingsui.cpp's version() accessor compiles in this target too.
target_compile_definitions(PhosphorSettingsUiQml PRIVATE
    PhosphorSettingsUi_EXPORTS
    PHOSPHORSETTINGSUI_VERSION_STR="${PHOSPHORSETTINGSUI_VERSION}"
)

add_library(PhosphorSettingsUi::PhosphorSettingsUiQml ALIAS PhosphorSettingsUiQml)

target_include_directories(PhosphorSettingsUiQml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorSettingsUiQml PUBLIC cxx_std_20)

target_link_libraries(PhosphorSettingsUiQml
    PUBLIC
        PhosphorSettingsUi::PhosphorSettingsUi
        # The chrome QML files import org.phosphor.animation for the
        # spring-physics motion profiles (panel.fadeIn/Out,
        # widget.accordionExpand/Collapse, widget.tint, widget.pulse).
        # Linking the QML plugin here pulls phosphor-animation into
        # the consumer's QML import path automatically.
        PhosphorAnimation::PhosphorAnimationQml
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

set_target_properties(PhosphorSettingsUiQml PROPERTIES
    VERSION ${PHOSPHORSETTINGSUI_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# Examples — opt-in via -DBUILD_PHOSPHOR_SETTINGS_UI_EXAMPLES=ON
# ═══════════════════════════════════════════════════════════════════════════════

option(BUILD_PHOSPHOR_SETTINGS_UI_EXAMPLES
    "Build the phosphor-settings-ui example apps"
    ${PROJECT_IS_TOP_LEVEL})

if(BUILD_PHOSPHOR_SETTINGS_UI_EXAMPLES AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_LIBDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(NOT DEFINED KDE_INSTALL_BINDIR)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorSettingsUi
    EXPORT PhosphorSettingsUiTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorSettingsUiTargets
    FILE PhosphorSettingsUiTargets.cmake
    NAMESPACE PhosphorSettingsUi::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSettingsUi
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorSettingsUiConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSettingsUiConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSettingsUi
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSettingsUiConfigVersion.cmake"
    VERSION ${PHOSPHORSETTINGSUI_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSettingsUiConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSettingsUiConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSettingsUi
)

install(DIRECTORY include/PhosphorSettingsUi/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorSettingsUi
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorsettingsui_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorSettingsUi
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
