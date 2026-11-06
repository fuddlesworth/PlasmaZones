# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test Quick Gui Qml)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
# CMAKE_CURRENT_LIST_DIR, not CMAKE_SOURCE_DIR: the latter is the repo root
# in-tree but this LIBRARY's directory in a standalone configure, so the include
# looked for <tier>/libs/<lib>/cmake/PhosphorTestIsolation.cmake and hard-failed at
# generate time. The relative walk reaches the repo root from either. The EXISTS
# guard covers the remaining case, a genuine extracted `git subtree split` where
# the repo-root module is not in the subtree at all.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/PhosphorTestIsolation.cmake)
else()
    # Stand-ins so the call sites below need no guard of their own.
    # phosphor_append_test_environment is reproduced EXACTLY, because it is
    # self-contained and because callers use it for things like
    # QT_QPA_PLATFORM=offscreen that the tests need to pass at all — a no-op
    # there would break them rather than merely un-isolate them. The isolation
    # helper degrades to nothing: its private D-Bus session and XDG sandbox need
    # module-level state that does not exist without the module.
    function(phosphor_apply_test_isolation _target)
    endfunction()
    function(phosphor_append_test_environment _test_name)
        set_property(TEST ${_test_name} APPEND PROPERTY ENVIRONMENT ${ARGN})
    endfunction()
endif()

# ─── Test plugin fixtures ──────────────────────────────────────────────────────
#
# Every bar-widget test plugin is fake_plugin/fakeplugin.cpp built once per
# variant. A variant differs only in its generated fake_plugin_config.h (IID,
# factory id, null factory) and metadata.json (embedded by Q_PLUGIN_METADATA),
# both written to a per-variant directory on the target's include path, where
# moc finds the metadata file. Each library lands at
# ${PHOSPHOR_REGISTRY_FIXTURE_DIR}/<variant>.so, and the tests copy it into
# temporary search paths.
set(PHOSPHOR_REGISTRY_FIXTURE_DIR ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
set(PHOSPHOR_REGISTRY_BAR_WIDGET_IID "org.phosphor_works.Lambent.BarWidget/1")
set(phosphorregistry_fixture_targets "")

# phosphorregistry_add_fake_plugin(<variant> IID <iid> FACTORY_ID <id>
#     METADATA <json body> [NULL_FACTORY])
function(phosphorregistry_add_fake_plugin variant)
    cmake_parse_arguments(ARG "NULL_FACTORY" "IID;FACTORY_ID;METADATA" "" ${ARGN})
    set(target phosphor_registry_fixture_${variant})
    set(configDir ${CMAKE_CURRENT_BINARY_DIR}/fixture-config/${variant})
    set(FAKE_PLUGIN_IID "${ARG_IID}")
    set(FAKE_FACTORY_ID "${ARG_FACTORY_ID}")
    if(ARG_NULL_FACTORY)
        set(FAKE_NULL_FACTORY 1)
    else()
        set(FAKE_NULL_FACTORY 0)
    endif()
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/fake_plugin/fake_plugin_config.h.in
                   ${configDir}/fake_plugin_config.h @ONLY)
    file(WRITE ${configDir}/metadata.json.tmp "${ARG_METADATA}\n")
    configure_file(${configDir}/metadata.json.tmp ${configDir}/metadata.json COPYONLY)

    add_library(${target} MODULE ${CMAKE_CURRENT_SOURCE_DIR}/fake_plugin/fakeplugin.cpp)
    target_include_directories(${target} PRIVATE ${configDir})
    target_link_libraries(${target}
        PRIVATE
            PhosphorRegistry::PhosphorRegistry
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
    )
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        OUTPUT_NAME ${variant}
        LIBRARY_OUTPUT_DIRECTORY ${PHOSPHOR_REGISTRY_FIXTURE_DIR}
        AUTOMOC ON
        UNITY_BUILD OFF
    )
    set(phosphorregistry_fixture_targets ${phosphorregistry_fixture_targets} ${target} PARENT_SCOPE)
endfunction()

phosphorregistry_add_fake_plugin(fake-plugin
    IID "${PHOSPHOR_REGISTRY_BAR_WIDGET_IID}" FACTORY_ID "fake-plugin"
    METADATA [=[{ "Id": "fake-plugin", "ApiVersion": 1 }]=])
phosphorregistry_add_fake_plugin(fake-plugin-secondary
    IID "${PHOSPHOR_REGISTRY_BAR_WIDGET_IID}" FACTORY_ID "fake-plugin-secondary"
    METADATA [=[{ "Id": "fake-plugin-secondary", "ApiVersion": 1 }]=])
# The metadata says one id and the factory another.
phosphorregistry_add_fake_plugin(fake-plugin-idmismatch
    IID "${PHOSPHOR_REGISTRY_BAR_WIDGET_IID}" FACTORY_ID "fake-other"
    METADATA [=[{ "Id": "fake-plugin-idmismatch", "ApiVersion": 1 }]=])
phosphorregistry_add_fake_plugin(fake-plugin-nullfactory
    IID "${PHOSPHOR_REGISTRY_BAR_WIDGET_IID}" FACTORY_ID "fake-plugin-nullfactory" NULL_FACTORY
    METADATA [=[{ "Id": "fake-plugin-nullfactory", "ApiVersion": 1 }]=])
phosphorregistry_add_fake_plugin(fake-plugin-wrongapi
    IID "${PHOSPHOR_REGISTRY_BAR_WIDGET_IID}" FACTORY_ID "fake-plugin-wrongapi"
    METADATA [=[{ "Id": "fake-plugin-wrongapi", "ApiVersion": 2 }]=])
phosphorregistry_add_fake_plugin(fake-plugin-wrongiid
    IID "org.phosphor_works.Test.NotABarWidget/1" FACTORY_ID "fake-plugin-wrongiid"
    METADATA [=[{ "Id": "fake-plugin-wrongiid", "ApiVersion": 1 }]=])
phosphorregistry_add_fake_plugin(fake-plugin-noid
    IID "${PHOSPHOR_REGISTRY_BAR_WIDGET_IID}" FACTORY_ID "fake-plugin-noid"
    METADATA [=[{ "ApiVersion": 1 }]=])
phosphorregistry_add_fake_plugin(fake-plugin-badid
    IID "${PHOSPHOR_REGISTRY_BAR_WIDGET_IID}" FACTORY_ID "../escape"
    METADATA [=[{ "Id": "../escape", "ApiVersion": 1 }]=])

# A shared library that is not a Qt plugin.
add_library(phosphor_registry_fixture_plain MODULE ${CMAKE_CURRENT_SOURCE_DIR}/fake_plugin/plain.cpp)
set_target_properties(phosphor_registry_fixture_plain PROPERTIES
    PREFIX ""
    OUTPUT_NAME plain
    LIBRARY_OUTPUT_DIRECTORY ${PHOSPHOR_REGISTRY_FIXTURE_DIR}
    UNITY_BUILD OFF
)
list(APPEND phosphorregistry_fixture_targets phosphor_registry_fixture_plain)

# ─── Tests ─────────────────────────────────────────────────────────────────────

function(phosphorregistry_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name}
        PRIVATE
            PhosphorRegistry::PhosphorRegistry
            Qt6::Test
            Qt6::Quick
    )
    add_test(NAME ${name} COMMAND ${name})
    phosphor_apply_test_isolation(${name})
    set_property(TEST ${name} APPEND PROPERTY LABELS "phosphorregistry")
    phosphor_append_test_environment(${name} "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphorregistry_add_test(test_phosphor_registry)
phosphorregistry_add_test(test_path_keyed_overrides)
phosphorregistry_add_test(test_phosphor_registry_metadatapack_loader)
phosphorregistry_add_test(test_phosphor_registry_plugincatalog)
phosphorregistry_add_test(test_phosphor_registry_pluginloader)

foreach(test test_phosphor_registry_plugincatalog test_phosphor_registry_pluginloader)
    target_compile_definitions(${test}
        PRIVATE PHOSPHOR_REGISTRY_FIXTURE_DIR="${PHOSPHOR_REGISTRY_FIXTURE_DIR}")
    add_dependencies(${test} ${phosphorregistry_fixture_targets})
endforeach()
