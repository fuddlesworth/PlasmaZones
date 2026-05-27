# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-popout-demo, in-window driver for PhosphorPopout's arbitration.
#
# Renders cooperative / modal / detached popouts inside a QQuickWindow
# using an InAppPopoutTransport. This is the acceptance test for
# libs/phosphor-popout. The real shell will swap in a layer-shell
# transport without changing the controller.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

set(CMAKE_AUTOMOC ON)

# Bundle demo QML into its own static module so qmlcachegen compiles
# it and the resources are reachable via qrc:/qt/qml/Phosphor/PopoutDemo/.
qt_add_qml_module(phosphor_popout_demo_qml
    URI Phosphor.PopoutDemo
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/PopoutDemo
    QML_FILES
        Main.qml
        CalendarPopout.qml
        NotePopout.qml
        AlertPopout.qml
        PhosphorButton.qml
    SOURCES
        DemoController.h
        democontroller.cpp
        InAppPopoutTransport.h
        inapppopouttransport.cpp
)

target_link_libraries(phosphor_popout_demo_qml
    PRIVATE
        PhosphorPopout::PhosphorPopout
        PhosphorPopout::PhosphorPopoutQml
        Qt6::Quick
)

add_executable(phosphor-popout-demo
    main.cpp
)

target_link_libraries(phosphor-popout-demo
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        # Static QML modules ship both a library and a plugin registrar.
        # The registrar is what publishes the module to QQmlEngine at
        # runtime; without it `import Phosphor.PopoutDemo` would fail.
        # Link both our demo module's targets plus the lib's QML module
        # plus PhosphorTheme's QML module (the demo imports
        # `Phosphor.Theme` for token bindings).
        phosphor_popout_demo_qmlplugin
        PhosphorPopout::PhosphorPopout
        PhosphorPopout::PhosphorPopoutQml
        PhosphorPopoutQmlplugin
        PhosphorTheme::PhosphorTheme
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)
