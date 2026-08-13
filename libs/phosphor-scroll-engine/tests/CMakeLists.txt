# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorScrollEngine unit tests, thirteen targets. Headless throughout: two
# files drive the axis-free primitives directly (StripAxis and the park
# decision, which take their axis as an argument and so need no fixture), two
# exercise the pure strip model against the shared screen fixture, and nine
# drive the engine through null (or stubbed) daemon dependencies and the
# geometry-provider seam. None needs a QPA platform, so
# by convention the strip tests run APPLESS and the engine tests GUILESS
# (they need an event dispatcher for the coalesced retile and the prunes'
# deleteLater) — the convention lives in each file's *_MAIN macro; nothing
# here enforces it.
#
# The eleven fixture-driven suites register TWICE, once per axis, via
# pse_add_test. The two axis-free ones use pse_add_axis_free_test and register
# once: the shared fixture HEADER still rides their target (the function adds
# it to every target, and automoc produces nothing for it), but they never
# call AX_GUARD_SUITE, so they print no axis line, the
# FAIL_REGULAR_EXPRESSION guard below would be inert for them, and a second
# identical run would only cost wall time.
#
# These route through phosphor_apply_test_isolation like every other
# libs/phosphor-*/tests tree. Nothing here touches the session bus today, so
# the dbus-run-session launcher is inert here, but the helper also installs
# the per-target XDG sandbox and the default 120s TIMEOUT. The timeout is the
# point: a hung test with no bound wedges the whole ctest run, which is this
# module's founding premise.

find_package(Qt6 REQUIRED COMPONENTS Test)

# Every sibling tests tree includes this explicitly. Relying on an earlier
# add_subdirectory having already defined the function would make this tree's
# configure depend on the order of the libs block.
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)

# One target, one ctest registration, no transposed arm. For a test whose
# subject takes its axis as a parameter rather than reading it from the
# process.
function(pse_add_axis_free_test _name)
    add_executable(${_name} ${ARGN})
    set_target_properties(${_name} PROPERTIES AUTOMOC ON)
    target_compile_features(${_name} PRIVATE cxx_std_20)
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            PhosphorScrollEngine::PhosphorScrollEngine
    )
    # The shared fixture header rides every target here (it carries no
    # Q_OBJECT; automoc scans it and produces nothing). Listed inside the
    # function so adding a test is ONE line below — a separate per-target
    # list drifted the moment a tenth test forgot its second edit.
    target_sources(${_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/scrollstriptestutils.h)
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES LABELS "phosphorscrollengine")
    phosphor_apply_test_isolation(${_name})
endfunction()

# The fixture-driven form: everything above, plus the transposed arm.
function(pse_add_test _name)
    pse_add_axis_free_test(${_name} ${ARGN})

    # The BASE arm gets the same two-sided vacuity guard as the vertical one
    # below, in mirror image. Without the pin, an ambient
    # PZ_SCROLL_TEST_AXIS=vertical (a polluted shell, a CI job exporting it
    # for one suite and leaking it to all) runs this arm vertically under a
    # name claiming horizontal — the exact lie the vertical arm's guard
    # exists to catch, in the other direction. Only for suites registered
    # through THIS function: they all call AX_GUARD_SUITE, so the printed
    # axis word is guaranteed to exist for the regex to see.
    phosphor_append_test_environment(${_name} "PZ_SCROLL_TEST_AXIS=horizontal")
    set_tests_properties(${_name} PROPERTIES FAIL_REGULAR_EXPRESSION "axis=vertical")

    # SECOND registration of the SAME binary, transposed. The axis is a
    # property of the process (PZ_SCROLL_TEST_AXIS, read once by
    # ScrollTestUtils::Ax), so one build serves both arms and the two report
    # independent pass/fail under readable names.
    #
    # phosphor_apply_test_isolation takes the ctest test name as its second
    # argument, which is what gives this arm its own ENVIRONMENT property and
    # its own 120s timeout. The XDG sandbox directory itself is keyed on the
    # TARGET, so the two arms share one tree — acceptable here because these
    # headless suites write nothing under XDG, but a suite that starts
    # persisting state must split the sandbox before its arms can run under
    # a parallel ctest.
    add_test(NAME ${_name}_vertical COMMAND ${_name})
    set_tests_properties(${_name}_vertical PROPERTIES LABELS "phosphorscrollengine")
    phosphor_apply_test_isolation(${_name} ${_name}_vertical)
    # APPEND, never a plain set_tests_properties(... ENVIRONMENT ...) — that
    # is a SET and would drop the XDG sandbox and the timeout the call above
    # just installed. The helper exists so this cannot be got wrong.
    phosphor_append_test_environment(${_name}_vertical "PZ_SCROLL_TEST_AXIS=vertical")
    # The out-of-band half of the vacuity guard. Every suite registered
    # through THIS function calls AX_GUARD_SUITE from its first slot, which
    # prints the resolved axis; if the ENVIRONMENT property above is ever
    # dropped, the binary runs HORIZONTALLY under a name claiming otherwise
    # and every assertion still passes. Failing on the printed word catches
    # that from outside the binary that would be lying.
    #
    # FAIL_REGULAR_EXPRESSION, deliberately not PASS_REGULAR_EXPRESSION: the
    # latter REPLACES the exit-code criterion, so a genuinely failing test
    # whose output happened to match would be reported as passing.
    set_tests_properties(${_name}_vertical PROPERTIES FAIL_REGULAR_EXPRESSION "axis=horizontal")
endfunction()

pse_add_axis_free_test(test_stripaxis test_stripaxis.cpp)
# The park decision lives in a PRIVATE header, and the library builds with
# hidden visibility, so its symbols are not linkable from here. Compile that
# one TU straight into the test instead: it is pure and depends on nothing but
# StripAxis and QtCore, which is exactly why it was worth extracting.
pse_add_axis_free_test(test_scrollpark test_scrollpark.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/scrollengine/engine_park.cpp)
target_include_directories(test_scrollpark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
pse_add_test(test_scrollstrip_core test_scrollstrip_core.cpp)
pse_add_test(test_scrollstrip_ops test_scrollstrip_ops.cpp)
pse_add_test(test_scrollengine_smoke test_scrollengine_smoke.cpp)
pse_add_test(test_scrollengine_persistence test_scrollengine_persistence.cpp)
pse_add_test(test_scrollengine_zonenumbers test_scrollengine_zonenumbers.cpp)
pse_add_test(test_scrollengine_perscreen test_scrollengine_perscreen.cpp scrollstubsettings.h)
# The template channel's seed blueprint and its consumption cursor, split out
# of the per-screen suite once that file passed the size ceiling. Same stub,
# different question: which blueprint ENTRY a column takes, and which events
# leave an entry spent.
pse_add_test(test_scrollengine_template test_scrollengine_template.cpp scrollstubsettings.h)
# The behaviour suite is the only one that needs a window-tracking stub: the
# sticky gate and the client-decides width both short-circuit on a null
# tracker, so their per-screen resolution is unobservable without one.
pse_add_test(test_scrollengine_behaviour test_scrollengine_behaviour.cpp scrollstubsettings.h scrollstubtracking.h)
pse_add_test(test_scrollengine_draginsert test_scrollengine_draginsert.cpp)
pse_add_test(test_scrollengine_snapshot test_scrollengine_snapshot.cpp)
pse_add_test(test_scrollengine_boundary test_scrollengine_boundary.cpp scrollstubsettings.h)
pse_add_test(test_scrollengine_verbs test_scrollengine_verbs.cpp)
