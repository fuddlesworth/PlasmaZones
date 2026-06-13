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

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# AUTOMOC is enabled at the top-level CMakeLists.txt. qt_add_qml_module
# also turns AUTOMOC on for the module target it creates. Setting it
# again here is redundant.

# Bundle demo QML into its own static module so qmlcachegen compiles
# it and the resources are reachable via qrc:/qt/qml/Phosphor/PopoutDemo/.
# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here: Qt adds <target>_autogen/include to each
# sub-target's -I list, but when a module carries no Q_OBJECT sources AUTOMOC
# never creates that directory (and --clean-first deletes it), so cc1plus
# warns on a Qt-managed scratch path. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

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

target_compile_features(phosphor_popout_demo_qml PRIVATE cxx_std_20)

add_executable(phosphor-popout-demo
    main.cpp
)

target_compile_features(phosphor-popout-demo PRIVATE cxx_std_20)

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
