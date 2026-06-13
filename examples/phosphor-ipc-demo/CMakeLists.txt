# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-ipc-demo. Acceptance harness for libs/phosphor-ipc, three
# IpcTargets declared in QML are driven from another terminal via
# phosphorctl. Demonstrates the full call / list / schema / subscribe
# wire surface.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# Bundle the demo QML into its own static module so qmlcachegen
# compiles it and the resources land under
# qrc:/qt/qml/Phosphor/IpcDemo/.
# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here: Qt adds <target>_autogen/include to each
# sub-target's -I list, but when a module carries no Q_OBJECT sources AUTOMOC
# never creates that directory (and --clean-first deletes it), so cc1plus
# warns on a Qt-managed scratch path. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(phosphor_ipc_demo_qml
    URI Phosphor.IpcDemo
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/IpcDemo
    QML_FILES
        Main.qml
    SOURCES
        DemoController.h
        democontroller.cpp
)

target_link_libraries(phosphor_ipc_demo_qml
    PRIVATE
        PhosphorIpc::PhosphorIpc
        Qt6::Quick
)

target_compile_features(phosphor_ipc_demo_qml PRIVATE cxx_std_20)

add_executable(phosphor-ipc-demo
    main.cpp
)

target_compile_features(phosphor-ipc-demo PRIVATE cxx_std_20)

# CMake-injected default socket path keeps `./bin/phosphor-ipc-demo`
# usable without --socket. CI / manual runs override via --socket
# or PHOSPHOR_SOCKET as documented in main.cpp.
target_compile_definitions(phosphor-ipc-demo
    PRIVATE
        PHOSPHOR_IPC_DEMO_SOCKET="${CMAKE_BINARY_DIR}/phosphor-ipc-demo.sock"
)

target_link_libraries(phosphor-ipc-demo
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        # Static QML modules ship a library + a plugin registrar; we
        # link both ours and PhosphorIpc's and PhosphorTheme's.
        phosphor_ipc_demo_qmlplugin
        PhosphorIpc::PhosphorIpc
        PhosphorIpc::PhosphorIpcQml
        PhosphorIpcQmlplugin
        PhosphorTheme::PhosphorTheme
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)
