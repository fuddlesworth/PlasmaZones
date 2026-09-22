# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
function(_phosphorservicebrightness_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceBrightness::PhosphorServiceBrightness
    )
    add_test(NAME ${target} COMMAND ${target})
    phosphor_apply_test_isolation(${target})
    # APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
    # ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
    # sandbox `phosphor_apply_test_isolation` just installed.
    phosphor_append_test_environment(${target} "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(${target} PROPERTIES
            LABELS "phosphorservicebrightness"
    )
endfunction()

# Smoke test: exercises the library contract against a fake sysfs tree and an
# in-process fake logind session. Covers QML registration idempotency, sysfs
# enumeration with the kbd_backlight filter, percentage math with zero-max
# safety, the logind write path (including the late session-path binding), the
# external-display (DDC/CI) device surface, live watcher updates, and the
# model's add/remove/dataChanged transactions.
_phosphorservicebrightness_test(test_phosphorservicebrightness_smoke test_smoke.cpp)
