# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorLayer unit tests. Headless — uses offscreen QPA.

find_package(Qt6 REQUIRED COMPONENTS Test)

# Shared includes: mocks/ (in-tree header-only) + library public headers (via target).

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
function(pl_add_test _name)
    set(_sources ${ARGN})
    add_executable(${_name} ${_sources})
    target_include_directories(${_name}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            PhosphorLayer::PhosphorLayer
    )
    add_test(NAME ${_name} COMMAND ${_name})
    phosphor_apply_test_isolation(${_name})
    # Offscreen QPA so QQuickWindow can construct without a display server.
    # LABELS lets `ctest -L phosphorlayer` select only our tests in a build
    # that also runs Phosphor unit tests; keep in sync with the docs.
    # Silence debug chatter (qCDebug) but KEEP warnings visible. Several
    # tests (rejectedAttach, jsonStore rejections, isValid rejections)
    # exercise paths that log qCWarning on the validation boundary; a
    # regression that moved a check to qCDebug would still pass under a
    # warning-silenced rules string, masking the regression.
    # APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
    # ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
    # sandbox `phosphor_apply_test_isolation` just installed.
    phosphor_append_test_environment(${_name} "QT_QPA_PLATFORM=offscreen" "QT_LOGGING_RULES=phosphorlayer.debug=false")
    set_tests_properties(${_name} PROPERTIES
        LABELS "phosphorlayer"
    )
endfunction()

pl_add_test(pl_test_role test_role.cpp)
pl_add_test(pl_test_surface test_surface.cpp)
pl_add_test(pl_test_factory test_factory.cpp)
pl_add_test(pl_test_registry test_registry.cpp)
pl_add_test(pl_test_topology test_topology.cpp)
pl_add_test(pl_test_content_url test_content_url.cpp)
pl_add_test(pl_test_default_screen_provider test_default_screen_provider.cpp)
pl_add_test(pl_test_extensions test_extensions.cpp)
pl_add_test(pl_test_surface_animator_dispatch test_surface_animator_dispatch.cpp)

# Two named outputs for the tests that cannot observe anything with one.
#
# test_registry's syncToScreens legs need a SECOND output to observe one
# going away; with a single screen they skipped themselves at
# test_registry.cpp's `screens().size() < 2` guard. (The identifierFor()
# rationale belongs to phosphor-screens, whose block this was copied from;
# this lib's tests never call it.) Append AFTER the per-test environment
# above: the list is read last-wins, so this overrides the plain offscreen
# entry rather than fighting it.
# No if(TEST ...) guard: pl_test_registry is registered unconditionally above,
# and set_property(TEST <missing>) is a hard CMake error. Guarding it would turn
# a loud configure failure into a silently dropped fixture, which puts the
# assertions back to skipping -- the exact thing this file exists to stop.
phosphor_append_test_environment(pl_test_registry
    "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/two-screens.json")
