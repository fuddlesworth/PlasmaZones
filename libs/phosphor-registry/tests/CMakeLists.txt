# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test Quick)

# ─── Fake plugin fixture helper ────────────────────────────────────────────────
#
# Builds one MODULE library + copies its templated manifest into a
# per-fixture build directory the test binaries pick up via CMake-
# defined preprocessor macros. Used for every plugin variant the
# pluginloader tests need.
#
# Args:
#   target  - CMake target name (also the subdir name under
#             ${CMAKE_CURRENT_BINARY_DIR}/)
#   source  - .cpp file relative to ${CMAKE_CURRENT_SOURCE_DIR}/
#   linkQt  - "QT" to PRIVATE-link Qt + PhosphorRegistry (factories);
#             "NOQT" to skip both (no-entry-point fixture; doesn't
#             need anything beyond its own translation unit)
function(phosphorregistry_add_fake_plugin target sourceDir source linkQt)
    add_library(${target} MODULE ${sourceDir}/${source})
    if(linkQt STREQUAL "QT")
        target_link_libraries(${target}
            PRIVATE
                PhosphorRegistry::PhosphorRegistry
                Qt6::Core
                Qt6::Gui
                Qt6::Qml
                Qt6::Quick
        )
    endif()
    # Default lib<name>.so output naming; matches the install helper
    # in the test binary which copies fixtures by basename.
    set_target_properties(${target} PROPERTIES
        PREFIX "lib"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${target}
        UNITY_BUILD OFF
    )
    # Template the manifest so abi: stays locked to
    # PhosphorRegistry::kPluginAbiVersion (mirrored from CMake via
    # PHOSPHOR_PLUGIN_ABI_VERSION). Drift between header and fixture
    # would silently break every pluginloader test on the next
    # version bump.
    set(manifestIn "${CMAKE_CURRENT_SOURCE_DIR}/${sourceDir}/manifest.json.in")
    if(EXISTS "${manifestIn}")
        configure_file(${manifestIn}
                       ${CMAKE_CURRENT_BINARY_DIR}/${target}/manifest.json @ONLY)
    endif()
endfunction()

phosphorregistry_add_fake_plugin(phosphor_registry_test_fake_plugin
                                 fake_plugin
                                 fakeplugin.cpp
                                 QT)
phosphorregistry_add_fake_plugin(phosphor_registry_test_fake_plugin_idmismatch
                                 fake_plugin_idmismatch
                                 fakeplugin_idmismatch.cpp
                                 QT)
phosphorregistry_add_fake_plugin(phosphor_registry_test_fake_plugin_nullfactory
                                 fake_plugin_nullfactory
                                 fakeplugin_nullfactory.cpp
                                 QT)
phosphorregistry_add_fake_plugin(phosphor_registry_test_fake_plugin_noentry
                                 fake_plugin_noentry
                                 fakeplugin_noentry.cpp
                                 NOQT)

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
            PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/phosphor_registry_test_fake_plugin"
            PHOSPHOR_REGISTRY_FAKE_PLUGIN_IDMISMATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/phosphor_registry_test_fake_plugin_idmismatch"
            PHOSPHOR_REGISTRY_FAKE_PLUGIN_NULLFACTORY_DIR="${CMAKE_CURRENT_BINARY_DIR}/phosphor_registry_test_fake_plugin_nullfactory"
            PHOSPHOR_REGISTRY_FAKE_PLUGIN_NOENTRY_DIR="${CMAKE_CURRENT_BINARY_DIR}/phosphor_registry_test_fake_plugin_noentry"
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphorregistry_add_test(test_phosphor_registry)
phosphorregistry_add_test(test_phosphor_registry_manifest)
phosphorregistry_add_test(test_phosphor_registry_pluginloader)

# test_phosphor_registry_pluginloader needs every fixture .so built
# before it runs.
add_dependencies(test_phosphor_registry_pluginloader
    phosphor_registry_test_fake_plugin
    phosphor_registry_test_fake_plugin_idmismatch
    phosphor_registry_test_fake_plugin_nullfactory
    phosphor_registry_test_fake_plugin_noentry)
