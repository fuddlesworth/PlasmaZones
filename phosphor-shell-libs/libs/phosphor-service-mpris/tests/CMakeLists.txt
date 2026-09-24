# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
function(_phosphorservicempris_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            # The wire test exports a fake player onto the private session
            # bus, so it needs QtDBus directly rather than only through the
            # library's own link.
            Qt6::DBus
            PhosphorServiceMpris::PhosphorServiceMpris
    )
    add_test(NAME ${target} COMMAND ${target})
    phosphor_apply_test_isolation(${target})
    # APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
    # ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
    # sandbox `phosphor_apply_test_isolation` just installed.
    phosphor_append_test_environment(${target} "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(${target} PROPERTIES
            LABELS "phosphorservicempris"
    )
endfunction()

_phosphorservicempris_test(test_phosphorservicempris_smoke test_smoke.cpp)
# A real MPRIS service on the private bus, read by a real MprisHost: the
# suite had only a smoke test, so nothing checked that a player's metadata
# actually reaches MprisPlayer.
_phosphorservicempris_test(test_phosphorservicempris_wire test_wire.cpp)
