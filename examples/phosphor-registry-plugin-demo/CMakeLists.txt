# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-registry-plugin-demo — driver for PhosphorRegistry's
# plugin loader path. Built-in widgets plus a third widget loaded
# from a separate .so. Demonstrates the plugin ABI shape +
# hot-reload on directory change.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# Build the CPU-meter plugin .so first; the demo's --plugin-root
# argument will point at the directory the plugin's CMakeLists
# writes the .so + manifest.json into.
add_subdirectory(plugins/cpu-meter)

# Directory-scope suppression of -Wmissing-include-dirs for the targets
# qt_add_qml_module generates here: Qt adds <target>_autogen/include to each
# sub-target's -I list, but when a module carries no Q_OBJECT sources AUTOMOC
# never creates that directory (and --clean-first deletes it), so cc1plus
# warns on a Qt-managed scratch path. Full rationale: src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(phosphor_registry_plugin_demo_qml
    URI Phosphor.RegistryPluginDemo
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/RegistryPluginDemo
    QML_FILES
        Main.qml
        ClockWidget.qml
        ColorSquareWidget.qml
    SOURCES
        DemoController.h
        democontroller.cpp
        QmlComponentBarWidgetFactory.h
        qmlcomponentbarwidgetfactory.cpp
)

target_link_libraries(phosphor_registry_plugin_demo_qml
    PRIVATE
        PhosphorRegistry::PhosphorRegistry
        Qt6::Quick
)

target_compile_features(phosphor_registry_plugin_demo_qml PRIVATE cxx_std_20)

add_executable(phosphor-registry-plugin-demo
    main.cpp
)

target_compile_features(phosphor-registry-plugin-demo PRIVATE cxx_std_20)

target_link_libraries(phosphor-registry-plugin-demo
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        phosphor_registry_plugin_demo_qmlplugin
        PhosphorRegistry::PhosphorRegistry
        PhosphorTheme::PhosphorTheme
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)

# Build-tree default plugin root. When the demo runs from ./bin/
# without --plugin-root, main.cpp falls back to this path so the
# CPU-meter plugin loads out of the box. CI / dev workflow only;
# installed deployment still uses the XDG default
# (${GenericDataLocation}/phosphor/plugins/).
target_compile_definitions(phosphor-registry-plugin-demo
    PRIVATE
        PHOSPHOR_REGISTRY_PLUGIN_DEMO_DEFAULT_ROOT="${CMAKE_BINARY_DIR}/registry-plugin-demo-plugins"
)

# The CPU-meter plugin must be built before the demo runs (the demo
# default plugin root won't have it, but CI / dev runs typically use
# --plugin-root pointing at the build tree).
add_dependencies(phosphor-registry-plugin-demo phosphor_registry_demo_cpu_meter_plugin)
