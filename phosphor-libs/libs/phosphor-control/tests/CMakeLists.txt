# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test DBus)

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
function(phosphorcontrol_add_test name)
    add_executable(${name} ${name}.cpp)
    # Qt6::Gui is still pulled in transitively by Qt6::Test (which
    # links Qt6::Gui in its INTERFACE) AND by Qt6::Quick via the
    # lib's QML target chain, so we don't need it explicitly. The
    # tests run under QT_QPA_PLATFORM=offscreen so the platform
    # plugin loads without a display. QTEST_MAIN builds a QGuiApplication
    # here (Gui is linked, so it is the QT_GUI_LIB variant), which is
    # exactly why a platform plugin is needed; no test creates its own
    # application object or touches widgets.
    target_link_libraries(${name}
        PRIVATE
            PhosphorControl::PhosphorControl
            Qt6::Core
            Qt6::DBus
            Qt6::Test
    )
    # LABELS lets `ctest -L phosphorcontrol` filter to just this
    # lib's tests in a workspace that runs many ctest suites.
    add_test(NAME ${name} COMMAND ${name})
    phosphor_apply_test_isolation(${name})
    # APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
    # ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
    # sandbox `phosphor_apply_test_isolation` just installed.
    phosphor_append_test_environment(${name} "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(${name} PROPERTIES
        LABELS "phosphorcontrol"
    )
endfunction()

phosphorcontrol_add_test(test_page_registry)
phosphorcontrol_add_test(test_application_controller)
phosphorcontrol_add_test(test_application_controller_history)
phosphorcontrol_add_test(test_localized_context)
phosphorcontrol_add_test(test_dbus_bridge)
phosphorcontrol_add_test(test_search_ranker)
phosphorcontrol_add_test(test_search_controller)
phosphorcontrol_add_test(test_sidebar_rows)

# ─── Facet module fixtures ─────────────────────────────────────────────────────
#
# Every test Facet module is fake_module/fakemodule.cpp built once per variant.
# A variant differs only in its generated fake_module_config.h (FAKE_MODE, see
# the source) and metadata.json, both in a per-variant directory on the
# target's include path, where moc finds the metadata file. Each library lands
# at ${PHOSPHOR_CONTROL_FIXTURE_DIR}/<variant>.so.
set(PHOSPHOR_CONTROL_FIXTURE_DIR ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
set(phosphorcontrol_fixture_targets "")

# phosphorcontrol_add_fake_module(<variant> MODE <n> METADATA <json> [TS_FILES <ts>...] [INVALID])
# The metadata goes through phosphor_add_facet_module_metadata like a real
# module's; INVALID lets it through when it is invalid on purpose.
function(phosphorcontrol_add_fake_module variant)
    cmake_parse_arguments(ARG "INVALID" "MODE;METADATA" "TS_FILES" ${ARGN})
    set(target phosphor_control_fixture_${variant})
    set(configDir ${CMAKE_CURRENT_BINARY_DIR}/fixture-config/${variant})
    set(FAKE_MODE ${ARG_MODE})
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/fake_module/fake_module_config.h.in
                   ${configDir}/fake_module_config.h @ONLY)
    file(WRITE ${configDir}/metadata.json.tmp "${ARG_METADATA}\n")
    configure_file(${configDir}/metadata.json.tmp ${configDir}/metadata.json COPYONLY)
    set(invalid "")
    if(ARG_INVALID)
        set(invalid ALLOW_INVALID)
    endif()
    add_library(${target} MODULE ${CMAKE_CURRENT_SOURCE_DIR}/fake_module/fakemodule.cpp)
    target_include_directories(${target} PRIVATE ${configDir})
    target_link_libraries(${target} PRIVATE PhosphorControl::PhosphorControl Qt6::Core)
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        OUTPUT_NAME ${variant}
        LIBRARY_OUTPUT_DIRECTORY ${PHOSPHOR_CONTROL_FIXTURE_DIR}
        AUTOMOC ON
        UNITY_BUILD OFF
    )
    phosphor_add_facet_module_metadata(${target} METADATA ${configDir}/metadata.json TS_FILES ${ARG_TS_FILES} ${invalid})
    set(phosphorcontrol_fixture_targets ${phosphorcontrol_fixture_targets} ${target} PARENT_SCOPE)
endfunction()

set(_good_pages [=[
    "Pages": [
        { "Id": "general/advanced", "Name": "Advanced", "Parent": "general" },
        { "Id": "general", "Name": "General", "Name[de]": "Allgemein", "Keywords": ["basic"],
          "Anchors": ["speed"] }
    ],
    "ExtensionPoints": [ { "Page": "general", "Id": "panel", "Version": 2 } ]]=])
phosphorcontrol_add_fake_module(good MODE 0 METADATA
    "{ \"Id\": \"good\", \"ApiVersion\": 1, \"Name\": \"Good\", \"Category\": \"input\", \"Icon\": \"good-icon\",${_good_pages} }")
phosphorcontrol_add_fake_module(child MODE 0 METADATA [=[{
    "Id": "child", "ApiVersion": 1, "Name": "Child", "Parent": "good",
    "Pages": [ { "Id": "extra", "Name": "Extra", "Parent": "good:general" } ] }]=])
phosphorcontrol_add_fake_module(orphan MODE 0 METADATA [=[{
    "Id": "orphan", "ApiVersion": 1, "Name": "Orphan", "Parent": "absent", "Category": "no-such-category",
    "Pages": [ { "Id": "page", "Name": "Page", "Parent": "absent:somewhere" } ] }]=])
phosphorcontrol_add_fake_module(missingpage MODE 1 METADATA
    "{ \"Id\": \"missingpage\", \"ApiVersion\": 1, \"Name\": \"Missing Page\",${_good_pages} }")
phosphorcontrol_add_fake_module(extrapage MODE 2 METADATA
    "{ \"Id\": \"extrapage\", \"ApiVersion\": 1, \"Name\": \"Extra Page\",${_good_pages} }")
phosphorcontrol_add_fake_module(wrongid MODE 3 METADATA
    "{ \"Id\": \"wrongid\", \"ApiVersion\": 1, \"Name\": \"Wrong Id\",${_good_pages} }")
phosphorcontrol_add_fake_module(doubleresult MODE 4 METADATA
    "{ \"Id\": \"doubleresult\", \"ApiVersion\": 1, \"Name\": \"Double Result\",${_good_pages} }")
phosphorcontrol_add_fake_module(wrongapi MODE 0 INVALID METADATA [=[{
    "Id": "wrongapi", "ApiVersion": 2, "Name": "Wrong Api", "Pages": [] }]=])
phosphorcontrol_add_fake_module(badmetadata MODE 0 INVALID METADATA [=[{
    "Id": "badmetadata", "ApiVersion": 1,
    "Pages": [ { "Id": "a:b", "Name": "Colon" } ] }]=])

# Modules that add sections to good's pages. extender's land: two at the top
# (ordered by Order, then id), one after the speed anchor, one at the panel
# point, and one aimed at a module that is not installed. badextender's are
# all refused.
phosphorcontrol_add_fake_module(extender MODE 0 METADATA [=[{
    "Id": "extender", "ApiVersion": 1, "Name": "Extender",
    "Pages": [ { "Id": "own", "Name": "Own" } ],
    "Extensions": [
        { "Id": "top-late", "Target": "good:general", "Slot": "top", "Name": "Top late", "Order": 10 },
        { "Id": "top-early", "Target": "good:general", "Slot": "top", "Name": "Top early" },
        { "Id": "after-speed", "Target": "good:general", "Slot": "after:speed", "Name": "After speed" },
        { "Id": "panel", "Target": "good:general", "Slot": "panel", "TargetVersion": 2, "Name": "Panel" },
        { "Id": "ghost", "Target": "absent:page", "Slot": "top", "Name": "Ghost" } ] }]=])
phosphorcontrol_add_fake_module(badextender MODE 0 METADATA [=[{
    "Id": "badextender", "ApiVersion": 1, "Name": "Bad Extender",
    "Extensions": [
        { "Id": "old-panel", "Target": "good:general", "Slot": "panel", "Name": "Old panel" },
        { "Id": "new-anchors", "Target": "good:general", "Slot": "top", "TargetVersion": 2, "Name": "New anchors" },
        { "Id": "no-anchor", "Target": "good:general", "Slot": "before:nope", "Name": "No anchor" },
        { "Id": "no-point", "Target": "good:general/advanced", "Slot": "panel", "Name": "No point" },
        { "Id": "no-page", "Target": "good:nowhere", "Slot": "top", "Name": "No page" } ] }]=])
set(_two_extensions [=[
    "Extensions": [
        { "Id": "first", "Target": "good:general", "Slot": "bottom", "Name": "First" },
        { "Id": "second", "Target": "good:general", "Slot": "bottom", "Name": "Second" } ]]=])
phosphorcontrol_add_fake_module(missingextension MODE 5 METADATA
    "{ \"Id\": \"missingextension\", \"ApiVersion\": 1, \"Name\": \"Missing Extension\",${_two_extensions} }")
phosphorcontrol_add_fake_module(extraextension MODE 6 METADATA
    "{ \"Id\": \"extraextension\", \"ApiVersion\": 1, \"Name\": \"Extra Extension\",${_two_extensions} }")

# Two modules writing one config file, and one opening it by a relative path.
phosphorcontrol_add_fake_module(sharera MODE 7 METADATA [=[{ "Id": "sharera", "ApiVersion": 1, "Name": "Sharer A" }]=])
phosphorcontrol_add_fake_module(sharerb MODE 7 METADATA [=[{ "Id": "sharerb", "ApiVersion": 1, "Name": "Sharer B" }]=])
phosphorcontrol_add_fake_module(relativeconfig MODE 8 METADATA
    [=[{ "Id": "relativeconfig", "ApiVersion": 1, "Name": "Relative Config" }]=])

# Metadata localized from a catalog at build time, with a search entry.
phosphorcontrol_add_fake_module(translated MODE 0
    TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/fake_module/translated_de.ts
    METADATA [=[{
    "Id": "translated", "ApiVersion": 1, "Name": "Translated", "Keywords": ["module-word"],
    "Pages": [ { "Id": "speed", "Name": "Speed", "Keywords": ["fast", "quick"], "Anchors": ["pointer"] } ],
    "Search": [ { "Page": "speed", "Anchor": "pointer", "Title": "Pointer speed", "Keywords": ["fast"] } ] }]=])

# Dynamic search providers: one well-behaved, one pointing elsewhere, and one
# registered without the metadata flag.
set(_provider_pages [=[ "Pages": [ { "Id": "rules", "Name": "Rules", "Anchors": ["list"] } ] ]=])
phosphorcontrol_add_fake_module(providers MODE 9 METADATA
    "{ \"Id\": \"providers\", \"ApiVersion\": 1, \"Name\": \"Providers\", \"SearchProviders\": true,${_provider_pages} }")
phosphorcontrol_add_fake_module(strayprovider MODE 10 METADATA
    "{ \"Id\": \"strayprovider\", \"ApiVersion\": 1, \"Name\": \"Stray\", \"SearchProviders\": true,${_provider_pages} }")
phosphorcontrol_add_fake_module(undeclaredprovider MODE 9 METADATA
    "{ \"Id\": \"undeclaredprovider\", \"ApiVersion\": 1, \"Name\": \"Undeclared\",${_provider_pages} }")

phosphorcontrol_add_test(test_facet_module_metadata)
phosphorcontrol_add_test(test_facet_metadata_tool)
target_compile_definitions(test_facet_metadata_tool PRIVATE
    PHOSPHOR_FACET_METADATA_TOOL="$<TARGET_FILE:phosphor-facet-metadata>")
add_dependencies(test_facet_metadata_tool phosphor-facet-metadata)
phosphorcontrol_add_test(test_facet_host)
phosphorcontrol_add_test(test_facet_test_host)

# FacetSlot and FacetExtensionFrame, through the static org.phosphor.control
# module the in-tree apps link.
phosphorcontrol_add_test(test_facet_slot)
target_link_libraries(test_facet_slot PRIVATE PhosphorControl::PhosphorControlQml PhosphorControlQmlplugin Qt6::Quick)
phosphorcontrol_add_test(test_page_host)
target_link_libraries(test_page_host PRIVATE
    PhosphorControl::PhosphorControlQml PhosphorControlQmlplugin PhosphorAnimationQmlplugin Qt6::Quick)
foreach(test test_facet_host test_facet_test_host)
    target_compile_definitions(${test} PRIVATE PHOSPHOR_CONTROL_FIXTURE_DIR="${PHOSPHOR_CONTROL_FIXTURE_DIR}")
    add_dependencies(${test} ${phosphorcontrol_fixture_targets})
endforeach()
