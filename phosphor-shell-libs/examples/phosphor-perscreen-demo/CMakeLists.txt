# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-perscreen-demo. Acceptance harness for the Phosphor.Shell
# QML module's PerScreen helper (Phase 1.5). Opens one small floating
# window per monitor; hot-plug add/remove correctly mirrors in the
# instantiated delegates.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick)

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# Bundle the demo QML into its own static module so qmlcachegen
# compiles it and the resources land under
# qrc:/qt/qml/Phosphor/PerScreenDemo/.
# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here: Qt adds <target>_autogen/include to each
# sub-target's -I list, but when a module carries no Q_OBJECT sources AUTOMOC
# never creates that directory (and --clean-first deletes it), so cc1plus
# warns on a Qt-managed scratch path. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(phosphor_perscreen_demo_qml
    URI Phosphor.PerScreenDemo
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/PerScreenDemo
    QML_FILES
        Main.qml
)

target_link_libraries(phosphor_perscreen_demo_qml
    PRIVATE
        Qt6::Quick
)

target_compile_features(phosphor_perscreen_demo_qml PRIVATE cxx_std_20)

add_executable(phosphor-perscreen-demo
    main.cpp
)

target_compile_features(phosphor-perscreen-demo PRIVATE cxx_std_20)

target_link_libraries(phosphor-perscreen-demo
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        # Static QML modules ship a library + a plugin registrar;
        # link both this demo's module and phosphor-shell's
        # PhosphorShellQml (which exports PerScreen).
        phosphor_perscreen_demo_qmlplugin
        PhosphorShell::PhosphorShell
        PhosphorShell::PhosphorShellQml
        PhosphorShellQmlplugin
        # DefaultScreenProvider lives in phosphor-layer.
        PhosphorLayer::PhosphorLayer
)
