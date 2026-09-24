# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
# CMAKE_CURRENT_LIST_DIR, not CMAKE_SOURCE_DIR: the latter is the repo root
# in-tree but this LIBRARY's directory in a standalone configure, so the include
# looked for libs/<lib>/cmake/PhosphorTestIsolation.cmake and hard-failed at
# generate time. The relative walk reaches the repo root from either. The EXISTS
# guard covers the remaining case, a genuine extracted `git subtree split` where
# the repo-root module is not in the subtree at all.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake)
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
    set_property(TEST ${name} APPEND PROPERTY LABELS "phosphorshortcuts")
    # Tests use a signals-only IBackend fake — no D-Bus, no display needed,
    # but Qt still wants a platform plugin at QGuiApplication startup. Use
    # offscreen so headless CI doesn't break.
    phosphor_append_test_environment(${name} "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphorshortcuts_add_test(test_registry)
# Opt-in live-daemon test: QSKIPs unless PHOSPHORSHORTCUTS_KGLOBALACCEL_TESTS=1
# (and again if no kglobalaccel answers). Uses only transient registrations so
# an opted-in run leaves no kglobalshortcutsrc residue.
#
# NOTE: under ctest the opt-in can never reach a real kglobalaccel — the
# standard isolation launches a PRIVATE bus with no service directories, so
# the daemon neither exists there nor can activate, and the test QSKIPs even
# with the variable set. Run it manually against the live session bus instead:
#   PHOSPHORSHORTCUTS_KGLOBALACCEL_TESTS=1 ./test_kglobalaccel_backend
# The isolation is kept anyway so an un-opted run can never touch the real
# bus by accident.
phosphorshortcuts_add_test(test_kglobalaccel_backend)
