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

# Hoist GNUInstallDirs + the KDE_INSTALL_* fallbacks here so every
# install(...) / target_include_directories(... INSTALL_INTERFACE) block
# below has the variables defined unconditionally. Previously this was
# included lazily in three places (around the lib target, around the
# QML install block, and at the start of the install section), each
# guarded by a separate `if(NOT DEFINED ...)` — easy to reorder a way
# that left INSTALL_INTERFACE with an empty path until the third
# include fired.
include(GNUInstallDirs)
if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(NOT DEFINED KDE_INSTALL_LIBDIR)
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()
if(NOT DEFINED KDE_INSTALL_BINDIR)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core DBus Gui Qml Quick)

# Shared-vs-static lib type. SHARED is the LGPL-friendly default: an
# end-user can relink against a modified copy of this library without
# also having to relink the consuming application, satisfying the
# LGPL-2.1's dynamic-linking exemption. Override to OFF only for
# embedded / single-binary builds where the LGPL relinking obligation
# is handled out-of-band (e.g. shipping the .o files alongside the
# binary). Mirrors libs/phosphor-config's pattern.
option(PHOSPHOR_SETTINGS_UI_BUILD_SHARED
    "Build phosphor-settings-ui as a shared library (LGPL relinking-friendly)"
    ON)
if(PHOSPHOR_SETTINGS_UI_BUILD_SHARED)
    set(_phosphor_settings_ui_lib_type SHARED)
else()
    set(_phosphor_settings_ui_lib_type STATIC)
endif()

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
    # Async state-machine half of ApplicationController — split out so
    # applicationcontroller.cpp stays under the 800-line cap (CLAUDE.md).
    # Same class, separate TU, no API change.
    src/applicationcontroller_async.cpp
    src/localizedcontext.cpp
    src/dbusbridge.cpp
)

# NOTE: three near-identical targets (PhosphorSettingsUi C++ lib +
# PhosphorSettingsUiQml STATIC QML module + optional
# PhosphorSettingsUiQmlShared install-only plugin) compile the same
# source files three times. This mirrors phosphor-animation and is
# intentional defence-in-depth — unifying them risks Qt's
# qmltyperegistrar emitting clashing type registrations into multiple
# targets that the linker would then resolve in a generator-dependent
# order. WATCH FOR: a "Type already registered" warning at QML import
# time if anyone ever adds a `qmlRegisterUncreatableType()` (or any
# side-effecting registration) to one of the shared sources — that
# would surface the triple-registration footgun and need plumbing
# (split source between targets, or a single-target install path).

# Suppress -Wmissing-include-dirs for Qt-managed AUTOMOC scratch dirs.
# KDECompilerSettings (in the PlasmaZones parent build) enables the
# warning globally, and qt_add_qml_module adds `<target>_autogen/include`
# to every generated sub-target's -I list (qmltyperegistration, resource
# init, qmlplugin). Those sub-targets carry no Q_OBJECT, so AUTOMOC
# produces nothing and the dir ends up empty or — after `--clean-first`
# — missing entirely, so cc1plus fires before AUTOMOC recreates it.
# Set at DIRECTORY level so every implicitly-generated sub-target picks
# it up without enumerating the ever-changing list by name; placed before
# the first target so it also propagates to the examples/ and tests/
# subdirectories added below. Mirrors libs/phosphor-animation and
# src/shared — see those for the full rationale.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

add_library(PhosphorSettingsUi ${_phosphor_settings_ui_lib_type}
    ${phosphorsettingsui_public_HDRS}
    ${phosphorsettingsui_SRCS}
)

add_library(PhosphorSettingsUi::PhosphorSettingsUi ALIAS PhosphorSettingsUi)

generate_export_header(PhosphorSettingsUi
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorsettingsui_export.h
    EXPORT_MACRO_NAME PHOSPHORSETTINGSUI_EXPORT
)

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
        # Qt6::Qml is PUBLIC because the public headers include
        # `<qqmlregistration.h>` (QML_NAMED_ELEMENT etc.) — consumers
        # need the Qml include path at compile time even if they don't
        # use the registration macros themselves.
        Qt6::Qml
    PRIVATE
        # Qt6::Gui is PRIVATE: moc-generated code on the implementation
        # side may reference QColor/QPalette/etc. through Qt5 compat
        # headers, but the public API in include/PhosphorSettingsUi/
        # exposes no Gui types (QObject + QString + QUrl + QList only).
        # Leaving Gui PUBLIC would force every consumer to link
        # Qt6::Gui even when only the headless ApplicationController/
        # PageController/StagingDomain surface is used (e.g. headless
        # tests). The QML targets below explicitly add Gui PUBLIC for
        # QML consumers that DO need it.
        Qt6::Gui
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
    PRIVATE
        # The generated <module>_qmltyperegistrations.cpp guards each registered
        # type with `#if __has_include(<Header.h>)` using the BARE header name,
        # so the header's own directory must be on the include path for a
        # non-unity build (which compiles that TU alone) to resolve it. PRIVATE —
        # consumers still use the <PhosphorSettingsUi/…> spelling above.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/PhosphorSettingsUi>
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

# Ordering invariant: the function must be defined BEFORE the
# cmake_language(DEFER CALL ...) inside the if-block below schedules it.
# DEFER captures the function name now but resolves the body at end-of-
# directory; if the function were declared after the if(), CMake would
# error with "Function _phosphor_settings_ui_apply_plugin_rpath not
# found" because the deferred call runs after the if() block has
# already evaluated.
function(_phosphor_settings_ui_apply_plugin_rpath plugin_target)
    if(NOT TARGET ${plugin_target})
        message(WARNING "phosphor-settings-ui: plugin target ${plugin_target} not found at install-rpath set time"
                        " — packaging may need LD_LIBRARY_PATH for the QML module directory.")
        return()
    endif()
    set_target_properties(${plugin_target} PROPERTIES INSTALL_RPATH "$ORIGIN")
endfunction()

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
        PRIVATE
            # Same bare-`__has_include(<Header.h>)` resolution as the
            # PhosphorSettingsUiQml target above.
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/PhosphorSettingsUi>
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
#
# KDE_INSTALL_* variables are resolved at the top of this file via the
# hoisted include(GNUInstallDirs) block — no per-section fallback
# include needed here.

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
    # SameMinorVersion is the pre-1.0 compatibility contract: a
    # find_package(PhosphorSettingsUi 0.1) call accepts 0.1.x patch
    # releases only — bumping to 0.2 is treated as breaking because
    # we still expect API/ABI to churn freely under 1.0. Switch this
    # back to SameMajorVersion once we hit 1.0 (when the API is
    # stabilised and we promise within-major back-compat).
    COMPATIBILITY SameMinorVersion
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

    # NOTE: no EXPORT clause here on purpose — QML modules are consumed
    # via the QML import path at runtime, not via `find_package` +
    # `target_link_libraries`. Earlier revisions registered these to
    # an export set that was never installed via `install(EXPORT ...)`,
    # which left a dangling reference for any consumer that tried to
    # use the QML targets through CMake.
    install(TARGETS PhosphorSettingsUiQmlShared
        RUNTIME DESTINATION ${_phosphor_settings_ui_qml_install_dir}
        LIBRARY DESTINATION ${_phosphor_settings_ui_qml_install_dir}
        ARCHIVE DESTINATION ${_phosphor_settings_ui_qml_install_dir}
    )
    # The plugin target is created lazily by Qt's qt_finalize_target()
    # finalizer — on some Qt 6.6.x configurations it isn't realised at
    # the point this install() runs, in which case the variable still
    # holds the expected name but `install(TARGETS ...)` would error
    # out with "given target ... which does not exist". Gate on TARGET
    # existence so the lib install succeeds even if the plugin target
    # hasn't materialised (the rpath helper above logs a WARNING in
    # the same condition, so the failure mode is visible).
    if(TARGET ${_phosphor_settings_ui_qml_shared_plugin_target})
        install(TARGETS ${_phosphor_settings_ui_qml_shared_plugin_target}
            RUNTIME DESTINATION ${_phosphor_settings_ui_qml_install_dir}
            LIBRARY DESTINATION ${_phosphor_settings_ui_qml_install_dir}
            ARCHIVE DESTINATION ${_phosphor_settings_ui_qml_install_dir}
        )
    endif()

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
