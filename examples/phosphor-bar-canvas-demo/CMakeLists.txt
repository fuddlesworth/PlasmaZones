# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-bar-canvas-demo, the Phase 3.2 acceptance demo for the
# connected-corner geometry. A floating bar whose "Control Center" button
# opens a popout that grows out of the bar as one painted Shape. The
# toggle routes through a real PopoutController (PopoutService, Phase 1.2)
# via a minimal SocketPopoutTransport.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickShapes)

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

# Bundle the demo QML into its own static module so qmlcachegen compiles
# it and the resources are reachable via qrc:/qt/qml/Phosphor/BarCanvasDemo/.
qt_add_qml_module(phosphor_bar_canvas_demo_qml
    URI Phosphor.BarCanvasDemo
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/BarCanvasDemo
    IMPORTS
        Phosphor.Theme
        Phosphor.Widgets
    QML_FILES
        Main.qml
)

target_link_libraries(phosphor_bar_canvas_demo_qml
    PRIVATE
        PhosphorShellWidgets::PhosphorShellWidgetsQml
        Qt6::Quick
)

target_compile_features(phosphor_bar_canvas_demo_qml PRIVATE cxx_std_20)

add_executable(phosphor-bar-canvas-demo
    main.cpp
    BarDemoController.h
    bardemocontroller.cpp
    SocketPopoutTransport.h
    socketpopouttransport.cpp
)

target_compile_features(phosphor-bar-canvas-demo PRIVATE cxx_std_20)

target_link_libraries(phosphor-bar-canvas-demo
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickShapes
        # The demo C++ drives the real PopoutController + PopoutRequest +
        # IPopoutTransport from phosphor-popout's C++ core.
        PhosphorPopout::PhosphorPopout
        # Static QML modules ship a library and a plugin registrar; the
        # registrar publishes the module to QQmlEngine at runtime. Link the
        # demo module, the widgets module + plugin, and the theme module +
        # plugin (the demo and BarCanvas import both).
        phosphor_bar_canvas_demo_qmlplugin
        PhosphorShellWidgets::PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)
