# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
add_executable(test_overlay_smoke
    test_overlay_smoke.cpp
)

# Reuse the in-tree phosphor-layer test mocks, so the syncSurfaceState cases
# can drive a real PhosphorLayer::Surface over a recording transport instead
# of needing a live Wayland connection.
target_include_directories(test_overlay_smoke
    PRIVATE ${CMAKE_SOURCE_DIR}/libs/phosphor-layer/tests
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
