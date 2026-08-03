# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorScrollEngine unit tests. Headless throughout, in three categories:
# two files exercise the pure strip model, four drive the engine through null
# daemon dependencies and the geometry-provider seam, and one covers the pure
# template-vocabulary extractor. None needs a QPA platform, so the strip and
# extractor tests run APPLESS and the engine tests GUILESS (they need an event
# dispatcher for the coalesced retile and the prunes' deleteLater).
#
# These route through phosphor_apply_test_isolation like every other
# libs/phosphor-*/tests tree. Nothing here touches the session bus today, so
# the dbus-run-session launcher is inert for now, but the helper also installs
# the per-target XDG sandbox and the default 120s TIMEOUT. The timeout is the
# point: a hung test with no bound wedges the whole ctest run, which is this
# module's founding premise.

find_package(Qt6 REQUIRED COMPONENTS Test)

# Every sibling tests tree includes this explicitly. Relying on an earlier
# add_subdirectory having already defined the function would make this tree's
# configure depend on the order of the libs block.
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)

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
pse_add_test(test_scrollengine_zonenumbers test_scrollengine_zonenumbers.cpp)
pse_add_test(test_scrollengine_perscreen test_scrollengine_perscreen.cpp)
pse_add_test(test_scrolltemplate test_scrolltemplate.cpp)

# Shared fixture header, listed so IDE/target file listings see it (every
# strip and engine test includes it — the pure extractor test does not; it
# carries no Q_OBJECT, so automoc scans it and produces nothing).
target_sources(test_scrollstrip_core PRIVATE scrollstriptestutils.h)
target_sources(test_scrollstrip_ops PRIVATE scrollstriptestutils.h)
target_sources(test_scrollengine_smoke PRIVATE scrollstriptestutils.h)
target_sources(test_scrollengine_persistence PRIVATE scrollstriptestutils.h)
target_sources(test_scrollengine_zonenumbers PRIVATE scrollstriptestutils.h)
target_sources(test_scrollengine_perscreen PRIVATE scrollstriptestutils.h)
