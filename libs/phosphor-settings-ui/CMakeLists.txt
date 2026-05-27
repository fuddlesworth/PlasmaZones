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
# QML Module — org.phosphor.settings.ui (STATIC, IN-TREE LINKABLE ONLY)
#
# The C++ library above intentionally avoids Qt6::Qml/Quick so non-QML
# consumers (e.g. headless tests) stay light. The QML module target
# below re-compiles the same public C++ sources so qmltyperegistrar
# sees the QML_NAMED_ELEMENT markers and emits the type registrations
# into the org.phosphor.settings.ui module — no separate foreigntypes
# .cpp is needed because every exposed type lives in this lib's own
# headers.
#
# IMPORTANT CONSUMPTION NOTE:
#
# This QML module is a STATIC archive. The `PhosphorSettingsUi::PhosphorSettingsUiQml`
# ALIAS works inside this source tree (when consumed via add_subdirectory).
# It is NOT installed and NOT added to the `PhosphorSettingsUiTargets`
# export set, so an out-of-tree consumer doing `find_package(PhosphorSettingsUi)`
# + `target_link_libraries(... PhosphorSettingsUi::PhosphorSettingsUiQml)`
# will fail at configure time with "Target ... not found".
#
# Out-of-tree consumers must either:
#   * Add this directory as a git submodule and consume via add_subdirectory,
#     OR
#   * Wait for the SHARED-variant install path (see the phosphor-animation
#     CMakeLists.txt PHOSPHOR_ANIMATION_QML_INSTALL pattern for the
#     reference implementation: a parallel `PhosphorSettingsUiQmlShared`
#     target whose plugin .so + qmldir + qmltypes are installed under
#     `${KDE_INSTALL_QMLDIR}/org/phosphor/settings/ui/`).
#
# A SHARED variant has not landed yet because every current consumer of
# this library is in-tree (PlasmaZones settings app + the minimal example
# in this directory). When the first out-of-tree consumer appears, copy
# the phosphor-animation Shared pattern verbatim.
# ═══════════════════════════════════════════════════════════════════════════════

# The QML module re-compiles the same public headers + sources as the C++
# lib so qmltyperegistrar sees the QML_NAMED_ELEMENT markers and emits
# registrations into org.phosphor.settings.ui. This mirrors phosphor-animation.

set(phosphorsettingsui_qml_FILES
    qml/SettingsAppWindow.qml
    qml/Sidebar.qml
    qml/SidebarRow.qml
    qml/SidebarBackButton.qml
    qml/Breadcrumbs.qml
    qml/UnsavedChangesFooter.qml
    qml/PageHost.qml
    qml/DiscardChangesDialog.qml
    qml/AboutPageShell.qml
)

# JS helpers shared by the QML chrome — kept separate from QML_FILES
# because qt_add_qml_module's qmltc / qmlcachegen pipeline expects
# `.qml`/`.mjs` for QML_FILES and `.js` for RESOURCES.
set(phosphorsettingsui_qml_RESOURCES
    qml/LoaderHelpers.js
    qml/ThemeHelpers.js
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
    RESOURCES ${phosphorsettingsui_qml_RESOURCES}
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
# QML Module — SHARED (install-only runtime plugin)
# ═══════════════════════════════════════════════════════════════════════════════
#
# The STATIC PhosphorSettingsUiQml target above is fine for in-tree
# consumers (PlasmaZones + examples/minimal) — they link it directly
# and Qt's plugin registration runs at link time. Out-of-tree
# consumers using `find_package(PhosphorSettingsUi)` get the C++
# target via the export set, but cannot consume STATIC QML modules:
# Qt only loads QML modules from the QML module search path
# (KDE_INSTALL_QMLDIR/org/phosphor/settings/ui/), which requires a
# SHARED plugin .so. Mirror phosphor-animation's pattern — build a
# parallel SHARED target gated behind PHOSPHOR_SETTINGS_UI_QML_INSTALL
# so packagers can opt in to shipping the QML module separately from
# in-tree consumers that don't need it.

option(PHOSPHOR_SETTINGS_UI_QML_INSTALL
    "Also build a SHARED variant of org.phosphor.settings.ui and install it"
    OFF)

if(PHOSPHOR_SETTINGS_UI_QML_INSTALL)
    qt_add_qml_module(PhosphorSettingsUiQmlShared
        URI org.phosphor.settings.ui
        VERSION 1.0
        SHARED
        SOURCES
            ${phosphorsettingsui_public_HDRS}
            ${phosphorsettingsui_SRCS}
        QML_FILES ${phosphorsettingsui_qml_FILES}
        RESOURCES ${phosphorsettingsui_qml_RESOURCES}
        OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml_install/org/phosphor/settings/ui
    )

    target_include_directories(PhosphorSettingsUiQmlShared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
            $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    )

    # The SHARED variant compiles the same sources under a different
    # target name; set the export define so PHOSPHORSETTINGSUI_EXPORT
    # resolves to visibility("default") on the SHARED build too.
    target_compile_definitions(PhosphorSettingsUiQmlShared PRIVATE
        PhosphorSettingsUi_EXPORTS
        PHOSPHORSETTINGSUI_VERSION_STR="${PHOSPHORSETTINGSUI_VERSION}"
    )

    target_compile_features(PhosphorSettingsUiQmlShared PUBLIC cxx_std_20)

    target_link_libraries(PhosphorSettingsUiQmlShared
        PUBLIC
            PhosphorSettingsUi::PhosphorSettingsUi
            PhosphorAnimation::PhosphorAnimationQml
            Qt6::Core
            Qt6::DBus
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
    )

    set_target_properties(PhosphorSettingsUiQmlShared PROPERTIES
        VERSION ${PHOSPHORSETTINGSUI_VERSION}
        SOVERSION 0
    )

    set(_phosphor_settings_ui_qml_shared_plugin_target PhosphorSettingsUiQmlSharedplugin)

    # The QML plugin and its backing PhosphorSettingsUiQmlShared library
    # install side-by-side in the QML module directory, which is not on
    # the linker's default search path. $ORIGIN on the plugin's
    # INSTALL_RPATH resolves the sibling library at runtime and during
    # dpkg-shlibdeps packaging scans.
    #
    # Defer the rpath set with cmake_language(DEFER) — Qt creates the
    # plugin target via qt_finalize_target() deferred to end of dir
    # scope on some 6.6.x configurations, so a direct set_target_properties
    # call here can race the target's actual creation. The DEFER guard +
    # TARGET check covers every supported Qt 6.6+ generator path.
    cmake_language(DEFER CALL _phosphor_settings_ui_apply_plugin_rpath
        ${_phosphor_settings_ui_qml_shared_plugin_target})
endif()

# Helper invoked via cmake_language(DEFER) above so the plugin target
# is fully realised by Qt's finalizer before we touch it. Defined here
# (after the if block) so it's visible to the DEFER call which executes
# at end of dir scope.
function(_phosphor_settings_ui_apply_plugin_rpath plugin_target)
    if(NOT TARGET ${plugin_target})
        message(WARNING "phosphor-settings-ui: plugin target ${plugin_target} not found at install-rpath set time"
                        " — packaging may need LD_LIBRARY_PATH for the QML module directory.")
        return()
    endif()
    set_target_properties(${plugin_target} PROPERTIES INSTALL_RPATH "$ORIGIN")
endfunction()

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

# QML module install (SHARED variant) — gated behind the option above.
if(PHOSPHOR_SETTINGS_UI_QML_INSTALL)
    if(NOT DEFINED KDE_INSTALL_QMLDIR)
        # Without ECM the QML dir falls back to a Qt-side install
        # location. Match the freedesktop convention so the consumer
        # can drop the lib under any standard prefix.
        set(KDE_INSTALL_QMLDIR ${KDE_INSTALL_LIBDIR}/qt6/qml)
    endif()
    set(_phosphor_settings_ui_qml_install_dir "${KDE_INSTALL_QMLDIR}/org/phosphor/settings/ui")

    install(TARGETS PhosphorSettingsUiQmlShared ${_phosphor_settings_ui_qml_shared_plugin_target}
        EXPORT PhosphorSettingsUiQmlTargets
        RUNTIME DESTINATION ${_phosphor_settings_ui_qml_install_dir}
        LIBRARY DESTINATION ${_phosphor_settings_ui_qml_install_dir}
        ARCHIVE DESTINATION ${_phosphor_settings_ui_qml_install_dir}
    )

    install(FILES
            ${CMAKE_BINARY_DIR}/qml_install/org/phosphor/settings/ui/qmldir
            ${CMAKE_BINARY_DIR}/qml_install/org/phosphor/settings/ui/PhosphorSettingsUiQmlShared.qmltypes
        DESTINATION ${_phosphor_settings_ui_qml_install_dir}
    )
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
