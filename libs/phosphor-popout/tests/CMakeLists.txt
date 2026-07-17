# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

# PopoutController's test coverage is split across three test binaries by
# concern, so a future contributor can add tests in one slice without
# re-reading the others.
#   _arbitration : open/close/toggle/closeAll arbitration paths
#   _dismiss     : dismissed-callback routing, re-entrancy guard, destructor
#   _signals     : signal-ordering invariants, slot-state consistency, DefaultScope
function(phosphorpopout_add_test name)
    add_executable(${name} ${name}.cpp)
    # Qt6::Core / Qt6::Gui / Qt6::Qml propagate via PUBLIC link from
    # PhosphorPopout::PhosphorPopout; only Qt6::Test is test-specific.
    target_link_libraries(${name}
        PRIVATE
            PhosphorPopout::PhosphorPopout
            Qt6::Test
    )
    add_test(NAME ${name} COMMAND ${name})
    # offscreen QPA keeps the tests headless on CI; the test binaries
    # transitively link Qt Gui (via QScreen on PopoutRequest) and
    # construct a QCoreApplication via QTEST_MAIN.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphorpopout_add_test(test_popoutcontroller_arbitration)
phosphorpopout_add_test(test_popoutcontroller_dismiss)
phosphorpopout_add_test(test_popoutcontroller_signals)
