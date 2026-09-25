# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

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
add_executable(test_overlay_smoke
    test_overlay_smoke.cpp
)

# Reuse the in-tree phosphor-layer test mocks, so the syncSurfaceState cases
# can drive a real PhosphorLayer::Surface over a recording transport instead
# of needing a live Wayland connection.
target_include_directories(test_overlay_smoke
    PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-libs/libs/phosphor-layer/tests
)

target_link_libraries(test_overlay_smoke
    PRIVATE
        PhosphorOverlay::PhosphorOverlay
        Qt6::Test
)

add_test(NAME test_overlay_smoke COMMAND test_overlay_smoke)
phosphor_apply_test_isolation(test_overlay_smoke)
set_property(TEST test_overlay_smoke APPEND PROPERTY LABELS "phosphoroverlay")
phosphor_append_test_environment(test_overlay_smoke "QT_QPA_PLATFORM=offscreen")
