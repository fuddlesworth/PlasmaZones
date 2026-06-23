# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# CPU meter plugin for phosphor-registry-plugin-demo. Builds a .so
# alongside its manifest.json into a plugin root the demo points at
# via --plugin-root. The demo's CMakeLists places the resulting
# pair under ${CMAKE_BINARY_DIR}/registry-plugin-demo-plugins/cpu-meter/.

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

# The plugin loader looks for *.so files alongside manifest.json.
# Drop the "lib" prefix so the .so basename matches the directory
# basename (cpu-meter.so under cpu-meter/), keeping the example
# layout self-explanatory.
set_target_properties(phosphor_registry_demo_cpu_meter_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "cpu-meter"
    SUFFIX ".so"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/registry-plugin-demo-plugins/cpu-meter
    UNITY_BUILD OFF
)

# Copy the manifest next to the .so. CONFIGURE_DEPENDS so editing
# the manifest re-triggers the copy at next build.
configure_file(manifest.json
               ${CMAKE_BINARY_DIR}/registry-plugin-demo-plugins/cpu-meter/manifest.json COPYONLY)
