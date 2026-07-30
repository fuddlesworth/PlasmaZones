# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorScrollEngine unit tests. Headless — pure strip-model logic, no QPA.
#
# These route through phosphor_apply_test_isolation like every other
# libs/phosphor-*/tests tree. Nothing here touches the session bus today, so
# the dbus-run-session launcher is inert for now, but the helper also installs
# the per-target XDG sandbox and the default 120s TIMEOUT — and the timeout is
# the point: the persistence test drives serialize/deserialize loops, and a
# hang in one would otherwise wedge the whole ctest run with no bound.

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
    phosphor_apply_test_isolation(${_name})
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
