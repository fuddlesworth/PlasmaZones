# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test Quick)

# ─── Fake plugin fixture for test_pluginloader ─────────────────────────────────
#
# Build a tiny .so exporting the canonical Phase-1.3 plugin entry
# point. The test_pluginloader binary copies this .so + the
# accompanying manifest.json into a per-test temporary plugin root
# and exercises the loader against it. Built as MODULE because we
# load it via QLibrary, not link against it.
add_library(phosphor_registry_test_fake_plugin MODULE
    fake_plugin/fakeplugin.cpp
)
target_link_libraries(phosphor_registry_test_fake_plugin
    PRIVATE
        PhosphorRegistry::PhosphorRegistry
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)
set_target_properties(phosphor_registry_test_fake_plugin PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fake_plugin
    UNITY_BUILD OFF
)
# Copy the manifest next to the .so so the test fixture is
# self-contained at one path.
configure_file(fake_plugin/manifest.json
               ${CMAKE_CURRENT_BINARY_DIR}/fake_plugin/manifest.json COPYONLY)

# ─── Fake plugin with id-mismatch fixture ──────────────────────────────────────
#
# Identical shape to the canonical fixture, but the factory returns
# id="fake-other" while the manifest says id="id-mismatch-plugin".
# Used by test_phosphor_registry_pluginloader::rejectsFactoryIdMismatch.
add_library(phosphor_registry_test_fake_plugin_idmismatch MODULE
    fake_plugin_idmismatch/fakeplugin_idmismatch.cpp
)
target_link_libraries(phosphor_registry_test_fake_plugin_idmismatch
    PRIVATE
        PhosphorRegistry::PhosphorRegistry
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)
set_target_properties(phosphor_registry_test_fake_plugin_idmismatch PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fake_plugin_idmismatch
    UNITY_BUILD OFF
)
configure_file(fake_plugin_idmismatch/manifest.json
               ${CMAKE_CURRENT_BINARY_DIR}/fake_plugin_idmismatch/manifest.json COPYONLY)

function(phosphorregistry_add_test name)
    add_executable(${name} ${name}.cpp)
    # Core / Gui / Qml propagate PUBLIC from PhosphorRegistry. Qt6::Test
    # is test-specific. Qt6::Quick is needed by the QQuickItem helpers
    # used in test_registry_helpers.h.
    target_link_libraries(${name}
        PRIVATE
            PhosphorRegistry::PhosphorRegistry
            Qt6::Test
            Qt6::Quick
    )
    target_compile_definitions(${name}
        PRIVATE
            PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/fake_plugin"
            PHOSPHOR_REGISTRY_FAKE_PLUGIN_IDMISMATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/fake_plugin_idmismatch"
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphorregistry_add_test(test_phosphor_registry)
phosphorregistry_add_test(test_phosphor_registry_manifest)
phosphorregistry_add_test(test_phosphor_registry_pluginloader)

# test_phosphor_registry_pluginloader needs the fixture .so built before it runs.
add_dependencies(test_phosphor_registry_pluginloader
    phosphor_registry_test_fake_plugin
    phosphor_registry_test_fake_plugin_idmismatch)
