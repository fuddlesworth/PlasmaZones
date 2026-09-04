# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-control-center-demo, the Phase 4.4 acceptance demo for the
# Phosphor.ControlCenter surface. A tile grid driven by real services
# (NetworkManager, BlueZ, PipeWire, logind, the idle ladder), supplied by
# ControlCenterController's Registry<IControlCenterTileFactory>.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickShapes)

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# Directory-scope suppression of -Wmissing-include-dirs. Full rationale:
# src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(phosphor_control_center_demo_qml
    URI Phosphor.ControlCenterDemo
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/ControlCenterDemo
    IMPORTS
        Phosphor.Theme
        Phosphor.Widgets
        Phosphor.ControlCenter
    QML_FILES
        Main.qml
)

target_link_libraries(phosphor_control_center_demo_qml
    PRIVATE
        PhosphorShellControlCenter::PhosphorShellControlCenterQml
        PhosphorShellWidgets::PhosphorShellWidgetsQml
        Qt6::Quick
)

target_compile_features(phosphor_control_center_demo_qml PRIVATE cxx_std_20)

add_executable(phosphor-control-center-demo
    main.cpp
    ControlCenterController.h
    controlcentercontroller.cpp
    QmlComponentTileFactory.h
    qmlcomponenttilefactory.cpp
)

target_compile_features(phosphor-control-center-demo PRIVATE cxx_std_20)

target_link_libraries(phosphor-control-center-demo
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickShapes
        # C++ cores: the registry behind the controller, and the idle
        # service the controller hands to IdleTile.
        PhosphorRegistry::PhosphorRegistry
        PhosphorServiceIdle::PhosphorServiceIdle
        # Static QML modules + their plugin registrars so every import in
        # Main.qml resolves at runtime.
        phosphor_control_center_demo_qmlplugin
        PhosphorShellControlCenter::PhosphorShellControlCenterQml
        PhosphorShellControlCenterQmlplugin
        PhosphorShellWidgets::PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
        # The tiles import Phosphor.Service.* at runtime. Those modules are
        # NOT qt_add_qml_module modules and ship no plugin registrar: each
        # service exposes a hand-rolled registerQmlTypes() that the host
        # process must call before loading QML (see main.cpp, and
        # the block near src/shell/main.cpp:130 for the shell's own). So the service
        # libraries are linked here and registered explicitly there.
        PhosphorServiceNetwork::PhosphorServiceNetwork
        PhosphorServiceBluetooth::PhosphorServiceBluetooth
        PhosphorServicePipeWire::PhosphorServicePipeWire
        PhosphorServiceBrightness::PhosphorServiceBrightness
)
