# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-widgets-kitchen-sink, the Phase 3.1 acceptance demo for the
# Phosphor.Widgets atom library. One scrollable window listing every atom
# in its enabled / disabled states with a live accent-cycle header. This
# is the acceptance test for libs/phosphor-shell-widgets.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here: Qt adds <target>_autogen/include to each
# sub-target's -I list, but when a module carries no Q_OBJECT sources AUTOMOC
# never creates that directory (and --clean-first deletes it), so cc1plus
# warns on a Qt-managed scratch path. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

# Bundle the demo QML into its own static module so qmlcachegen compiles
# it and the resources are reachable via qrc:/qt/qml/Phosphor/WidgetsKitchenSink/.
qt_add_qml_module(phosphor_widgets_kitchen_sink_qml
    URI Phosphor.WidgetsKitchenSink
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/WidgetsKitchenSink
    IMPORTS
        Phosphor.Theme
        Phosphor.Widgets
    QML_FILES
        Main.qml
)

target_link_libraries(phosphor_widgets_kitchen_sink_qml
    PRIVATE
        PhosphorShellWidgets::PhosphorShellWidgetsQml
        Qt6::Quick
)

target_compile_features(phosphor_widgets_kitchen_sink_qml PRIVATE cxx_std_20)

add_executable(phosphor-widgets-kitchen-sink
    main.cpp
)

target_compile_features(phosphor-widgets-kitchen-sink PRIVATE cxx_std_20)

target_link_libraries(phosphor-widgets-kitchen-sink
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        # Static QML modules ship both a library and a plugin registrar.
        # The registrar publishes the module to QQmlEngine at runtime;
        # without it the `import` fails. Link the demo module, the widgets
        # module + plugin, and the theme module + plugin (the demo imports
        # both Phosphor.Widgets and Phosphor.Theme).
        phosphor_widgets_kitchen_sink_qmlplugin
        PhosphorShellWidgets::PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)
