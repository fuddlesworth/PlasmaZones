# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorScrollEngine unit tests. Headless — pure strip-model logic, no QPA.
#
# No D-Bus isolation launcher on ANY of these targets, deliberately:
# nothing in phosphor-scroll-engine or phosphor-engine touches the session
# bus (the dbus-run-session TEST_LAUNCHER is scoped to tests/unit, whose
# fixtures do). If a QDBus dependency ever reaches these targets, add the
# launcher.

find_package(Qt6 REQUIRED COMPONENTS Test)

function(pse_add_test _name)
    add_executable(${_name} ${ARGN})
    set_target_properties(${_name} PROPERTIES AUTOMOC ON)
    target_compile_features(${_name} PRIVATE cxx_std_20)
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            PhosphorScrollEngine::PhosphorScrollEngine
    )
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES LABELS "phosphorscrollengine")
endfunction()

pse_add_test(test_scrollstrip_core test_scrollstrip_core.cpp)
pse_add_test(test_scrollstrip_ops test_scrollstrip_ops.cpp)
pse_add_test(test_scrollengine_smoke test_scrollengine_smoke.cpp)
pse_add_test(test_scrollengine_persistence test_scrollengine_persistence.cpp)

# Shared fixture header, listed so IDE/target file listings see it (both
# strip tests include it; it carries no Q_OBJECT, so automoc scans it and
# produces nothing).
target_sources(test_scrollstrip_core PRIVATE scrollstriptestutils.h)
target_sources(test_scrollstrip_ops PRIVATE scrollstriptestutils.h)
