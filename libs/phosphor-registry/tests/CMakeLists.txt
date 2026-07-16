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
#   linkQt  - "FACTORY" to PRIVATE-link the full Qt + PhosphorRegistry
#             stack (canonical fixture shape: the plugin builds a
#             real factory subclass against the public ABI);
#             "MINIMAL" for fixtures that only need Q_DECL_EXPORT
#             from QtCore (e.g. the noentry fixture that exports a
#             dummy symbol with no factory machinery)
function(phosphorregistry_add_fake_plugin target sourceDir source linkQt)
    add_library(${target} MODULE ${sourceDir}/${source})
    # All fixtures need at least Qt6::Core for Q_DECL_EXPORT.
    target_link_libraries(${target} PRIVATE Qt6::Core)
    if(linkQt STREQUAL "FACTORY")
        target_link_libraries(${target}
            PRIVATE
                PhosphorRegistry::PhosphorRegistry
                Qt6::Gui
                Qt6::Qml
                Qt6::Quick
        )
    endif()
    # Default lib<name>.so output naming; matches the install helper
    # in the test binary which copies fixtures by basename.
    # UNITY_BUILD OFF is load-bearing: each fixture's extern "C"
    # entry-point symbol (phosphor_registry_create_factory) would
    # collide with the others if they were merged into a single
    # translation unit.
    set_target_properties(${target} PROPERTIES
        PREFIX "lib"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${target}
        UNITY_BUILD OFF
    )
    # Template the manifest so abi: stays locked to
    # PhosphorRegistry::PluginAbiVersion (mirrored from CMake via
    # PHOSPHOR_PLUGIN_ABI_VERSION). Drift between header and fixture
    # would silently break every pluginloader test on the next
    # version bump.
    set(manifestIn "${CMAKE_CURRENT_SOURCE_DIR}/${sourceDir}/manifest.json.in")
    if(EXISTS "${manifestIn}")
        # Guard against a templated manifest that forgot to reference
        # the abi-version variable: the configure_file call would
        # happily produce a stale literal and the drift would only
        # surface at test runtime as a confusing abi mismatch. Read
        # the source and fail configure if the placeholder is missing.
        file(READ "${manifestIn}" _manifestInContents)
        string(FIND "${_manifestInContents}"
                    "@PHOSPHOR_PLUGIN_ABI_VERSION@"
                    _manifestAbiIdx)
        if(_manifestAbiIdx EQUAL -1)
            message(FATAL_ERROR
                "Manifest template ${manifestIn} does not reference "
                "@PHOSPHOR_PLUGIN_ABI_VERSION@. The abi value would "
                "drift from PhosphorRegistry::PluginAbiVersion on the "
                "next bump. Either reference the variable or stop "
                "templating this manifest.")
        endif()
        unset(_manifestInContents)
        unset(_manifestAbiIdx)
        configure_file(${manifestIn}
                       ${CMAKE_CURRENT_BINARY_DIR}/${target}/manifest.json @ONLY)
    endif()
endfunction()

phosphorregistry_add_fake_plugin(phosphor_registry_test_fake_plugin
                                 fake_plugin
                                 fakeplugin.cpp
                                 FACTORY)
phosphorregistry_add_fake_plugin(phosphor_registry_test_fake_plugin_secondary
                                 fake_plugin_secondary
                                 fakeplugin_secondary.cpp
                                 FACTORY)
phosphorregistry_add_fake_plugin(phosphor_registry_test_fake_plugin_idmismatch
                                 fake_plugin_idmismatch
                                 fakeplugin_idmismatch.cpp
                                 FACTORY)
phosphorregistry_add_fake_plugin(phosphor_registry_test_fake_plugin_nullfactory
                                 fake_plugin_nullfactory
                                 fakeplugin_nullfactory.cpp
                                 FACTORY)
phosphorregistry_add_fake_plugin(phosphor_registry_test_fake_plugin_noentry
                                 fake_plugin_noentry
                                 fakeplugin_noentry.cpp
                                 MINIMAL)

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
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphorregistry_add_test(test_phosphor_registry)
phosphorregistry_add_test(test_phosphor_registry_metadatapack_loader)
phosphorregistry_add_test(test_phosphor_registry_manifest)
phosphorregistry_add_test(test_phosphor_registry_pluginloader)
# test_phosphor_registry_pluginloader_lifecycle holds the rescan-
# reentry + destructor-pin slots, split out of the primary
# pluginloader test TU to keep both files under the project's 1000-line
# guideline. Both share tests/test_pluginloader_helpers.h for the
# WarningCapture + plugin-fixture install scaffolding.
phosphorregistry_add_test(test_phosphor_registry_pluginloader_lifecycle)

# QT_FATAL_WARNINGS is NOT set at the process level here. A process-
# wide promotion of qWarning() to abort() conflicts with
# QTest::ignoreMessage in the sibling tests that intentionally
# trigger qWarning paths (ignoresInvalidManifest,
# rejectsAbiVersionMismatch, etc.). Qt's default handler aborts
# before the ignore-message infrastructure can match.
# rescanReentryFromPluginUnloadedSlotIsSafe and
# rescanReentryRemovingSecondPluginExercisesConstFindGuard install
# a per-test message handler to enforce the "no qWarning fires"
# contract locally instead. See those tests' implementations.

# Plugin-loader tests consume the five MODULE fixtures via per-fixture
# compile definitions + an explicit build-time dependency. Other test
# binaries don't get the macros (they don't load .so files) so the
# define surface stays minimal and parallel builds don't accidentally
# run a test before its fixtures exist.
#
# Both the primary pluginloader TU and the lifecycle TU need the full
# macro set + fixture dependency. Wrap the wiring once so adding a
# third pluginloader TU in the future is one line; otherwise the macro
# list silently drifts between binaries.
function(phosphorregistry_wire_pluginloader_fixtures target)
    target_compile_definitions(${target}
        PRIVATE
            PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/phosphor_registry_test_fake_plugin"
            PHOSPHOR_REGISTRY_FAKE_PLUGIN_SECONDARY_DIR="${CMAKE_CURRENT_BINARY_DIR}/phosphor_registry_test_fake_plugin_secondary"
            PHOSPHOR_REGISTRY_FAKE_PLUGIN_IDMISMATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/phosphor_registry_test_fake_plugin_idmismatch"
            PHOSPHOR_REGISTRY_FAKE_PLUGIN_NULLFACTORY_DIR="${CMAKE_CURRENT_BINARY_DIR}/phosphor_registry_test_fake_plugin_nullfactory"
            PHOSPHOR_REGISTRY_FAKE_PLUGIN_NOENTRY_DIR="${CMAKE_CURRENT_BINARY_DIR}/phosphor_registry_test_fake_plugin_noentry"
    )
    add_dependencies(${target}
        phosphor_registry_test_fake_plugin
        phosphor_registry_test_fake_plugin_secondary
        phosphor_registry_test_fake_plugin_idmismatch
        phosphor_registry_test_fake_plugin_nullfactory
        phosphor_registry_test_fake_plugin_noentry)
endfunction()

phosphorregistry_wire_pluginloader_fixtures(test_phosphor_registry_pluginloader)
phosphorregistry_wire_pluginloader_fixtures(test_phosphor_registry_pluginloader_lifecycle)
