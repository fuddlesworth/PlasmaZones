# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Tests for the Phosphor.Lock lockscreen surface.
#
#  - test_phosphor_shell_lock: a QtQuickTest harness. Cases live in the
#    tst_*.qml files; tst_lock.cpp is the QUICK_TEST_MAIN entry point. It
#    covers the largest-empty-region function (LockRegion.js), the outline
#    count of LockLayout, and the auth field's phase transitions against a
#    fake LockService.
#  - test_phosphor_shell_lock_surface: a Qt Test binary for the
#    LockSurface window's one hard rule, that it refuses to exist while no
#    session lock is held. Under the offscreen platform there is no
#    Wayland integration and so never a lock, which is exactly the refusal
#    path.
#
# Both link the lock module + the modules it imports (Phosphor.Widgets,
# Phosphor.Theme) and their plugin registrars so the QML engine can
# resolve every import.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest Test)

# Shared test isolation: a private D-Bus session (so a test that touches
# D-Bus cannot activate the INSTALLED daemon, which then holds the test's
# stdout pipe open and hangs ctest after the test itself passed) plus a
# per-target XDG sandbox (so nothing writes into the developer's real
# ~/.config/plasmazones). EXISTS-guarded explicit path so a standalone
# configure of this library still generates.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake)
    set(_phosphorshelllock_have_isolation TRUE)
else()
    set(_phosphorshelllock_have_isolation FALSE)
endif()

function(phosphorshelllock_isolate_test name)
    if(_phosphorshelllock_have_isolation)
        phosphor_apply_test_isolation(${name})
        # APPEND, not a bare SET: a plain `set_tests_properties(... ENVIRONMENT
        # ...)` would replace the whole list and drop the XDG sandbox the
        # isolation call just installed.
        phosphor_append_test_environment(${name} "QT_QPA_PLATFORM=offscreen")
    else()
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    set_property(TEST ${name} APPEND PROPERTY LABELS "phosphorshelllock")
endfunction()

set(_phosphorshelllock_test_libs
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    # The lock module + its plugin, plus the modules it imports and their
    # plugin registrars (Phosphor.Widgets pulls Phosphor.Theme).
    PhosphorShellLockQml
    PhosphorShellLockQmlplugin
    PhosphorShellWidgetsQml
    PhosphorShellWidgetsQmlplugin
    PhosphorThemeQml
    PhosphorThemeQmlplugin
)

add_executable(test_phosphor_shell_lock tst_lock.cpp)
target_compile_features(test_phosphor_shell_lock PRIVATE cxx_std_20)
target_link_libraries(test_phosphor_shell_lock
    PRIVATE
        ${_phosphorshelllock_test_libs}
        Qt6::QuickTest
)
add_test(
    NAME test_phosphor_shell_lock
    COMMAND test_phosphor_shell_lock -input ${CMAKE_CURRENT_SOURCE_DIR}
)
phosphorshelllock_isolate_test(test_phosphor_shell_lock)

add_executable(test_phosphor_shell_lock_surface test_locksurfacewindow.cpp)
target_compile_features(test_phosphor_shell_lock_surface PRIVATE cxx_std_20)
target_link_libraries(test_phosphor_shell_lock_surface
    PRIVATE
        ${_phosphorshelllock_test_libs}
        Qt6::Test
)
add_test(NAME test_phosphor_shell_lock_surface COMMAND test_phosphor_shell_lock_surface)
phosphorshelllock_isolate_test(test_phosphor_shell_lock_surface)
