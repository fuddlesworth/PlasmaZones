# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
function(phosphorshortcuts_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name}
        PRIVATE
            PhosphorShortcuts::PhosphorShortcuts
            Qt6::Core
            Qt6::Gui
            Qt6::Test
    )
    add_test(NAME ${name} COMMAND ${name})
    phosphor_apply_test_isolation(${name})
    # Tests use a signals-only IBackend fake — no D-Bus, no display needed,
    # but Qt still wants a platform plugin at QGuiApplication startup. Use
    # offscreen so headless CI doesn't break.
    phosphor_append_test_environment(${name} "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphorshortcuts_add_test(test_registry)
# Opt-in live-daemon test: QSKIPs unless PHOSPHORSHORTCUTS_KGLOBALACCEL_TESTS=1
# (and again if no kglobalaccel answers). Uses only transient registrations so
# an opted-in run leaves no kglobalshortcutsrc residue.
phosphorshortcuts_add_test(test_kglobalaccel_backend)
