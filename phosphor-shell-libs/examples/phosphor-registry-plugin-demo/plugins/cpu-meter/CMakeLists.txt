# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# CPU meter plugin for phosphor-registry-plugin-demo: a bar-widget Qt
# plugin whose id and ApiVersion live in metadata.json, embedded by
# Q_PLUGIN_METADATA. It builds into the bar-widget directory of the
# per-build plugin root the demo searches by default.

add_library(phosphor_registry_demo_cpu_meter_plugin MODULE
    cpumeter.cpp
)

target_link_libraries(phosphor_registry_demo_cpu_meter_plugin
    PRIVATE
        PhosphorRegistry::PhosphorRegistry
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

set_target_properties(phosphor_registry_demo_cpu_meter_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "cpu-meter"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/registry-plugin-demo-plugins/bar-widget
    UNITY_BUILD OFF
)
