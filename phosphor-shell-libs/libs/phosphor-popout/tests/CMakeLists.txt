# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

# The popout library's test coverage is split across four binaries by concern,
# so a future contributor can add tests in one slice without re-reading the
# others.
#   _arbitration : open/close/toggle/closeAll arbitration paths
#   _dismiss     : dismissed-callback routing, re-entrancy guard, destructor
#   _signals     : signal-ordering invariants, slot-state consistency, DefaultScope
#   _qml_enums   : the QML-facing enum path (PhosphorPopout.Anchor.* and
#                  ExclusiveMode.*) and object-literal to PopoutRequest

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
# EXISTS-guarded explicit path like the sibling shell-tier test trees: a hard
# include of a path outside the extracted subtree would fail a standalone
# `git subtree split` configure at generate time.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PhosphorTestIsolation.cmake)
    set(_phosphorpopout_have_isolation TRUE)
else()
    set(_phosphorpopout_have_isolation FALSE)
endif()

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
    # APPEND, not set_tests_properties: a plain SET after the isolation call
    # would replace the whole ENVIRONMENT list and drop the XDG sandbox the
    # helper just installed. On a standalone configure (helper absent) the
    # raw SET is the only writer, so it is safe there.
    if(_phosphorpopout_have_isolation)
        phosphor_apply_test_isolation(${name})
        phosphor_append_test_environment(${name} "QT_QPA_PLATFORM=offscreen")
    else()
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    set_property(TEST ${name} APPEND PROPERTY LABELS "phosphorpopout")
    # A tighter bound than the helper's 120s default, so a hang in this suite
    # fails the run promptly. Set AFTER phosphor_apply_test_isolation, which
    # only supplies its default when no TIMEOUT is set yet — last write wins.
    set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endfunction()

phosphorpopout_add_test(test_popoutcontroller_arbitration)
phosphorpopout_add_test(test_popoutcontroller_dismiss)
phosphorpopout_add_test(test_popoutcontroller_signals)

# The QML-facing enum path needs the QML module itself, not just the C++
# library: the type registration that decides whether QML sees the values at
# PhosphorPopout.Anchor.* lives in the generated plugin, and on a STATIC
# module that registration only happens for a binary that links the plugin
# target as well as the module.
phosphorpopout_add_test(test_popout_qml_enums)
target_link_libraries(test_popout_qml_enums
    PRIVATE
        PhosphorPopout::PhosphorPopoutQml
        PhosphorPopoutQmlplugin
)
