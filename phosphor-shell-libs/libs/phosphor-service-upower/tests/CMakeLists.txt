# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

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
function(_phosphorserviceupower_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceUPower::PhosphorServiceUPower
    )
    add_test(NAME ${target} COMMAND ${target})
    phosphor_apply_test_isolation(${target})
    # APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
    # ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
    # sandbox `phosphor_apply_test_isolation` just installed.
    phosphor_append_test_environment(${target} "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(${target} PROPERTIES
            LABELS "phosphorserviceupower"
    )
endfunction()

# Smoke test: model role names + lifecycle on an idle host (no UPower
# on the bus, no devices to enumerate). The CI runners have no UPower,
# so this is what we can verify deterministically; live D-Bus coverage
# requires a fake org.freedesktop.UPower fixture and is its own
# follow-up.
_phosphorserviceupower_test(test_phosphorserviceupower_smoke test_smoke.cpp)
