# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test DBus)

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
function(phosphorcontrol_add_test name)
    add_executable(${name} ${name}.cpp)
    # Qt6::Gui is still pulled in transitively by Qt6::Test (which
    # links Qt6::Gui in its INTERFACE) AND by Qt6::Quick via the
    # lib's QML target chain, so we don't need it explicitly. The
    # tests run under QT_QPA_PLATFORM=offscreen so the platform
    # plugin loads without a display. QTEST_MAIN builds a QGuiApplication
    # here (Gui is linked, so it is the QT_GUI_LIB variant), which is
    # exactly why a platform plugin is needed; no test creates its own
    # application object or touches widgets.
    target_link_libraries(${name}
        PRIVATE
            PhosphorControl::PhosphorControl
            Qt6::Core
            Qt6::DBus
            Qt6::Test
    )
    # LABELS lets `ctest -L phosphorcontrol` filter to just this
    # lib's tests in a workspace that runs many ctest suites.
    add_test(NAME ${name} COMMAND ${name})
    phosphor_apply_test_isolation(${name})
    # APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
    # ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
    # sandbox `phosphor_apply_test_isolation` just installed.
    phosphor_append_test_environment(${name} "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(${name} PROPERTIES
        LABELS "phosphorcontrol"
    )
endfunction()

phosphorcontrol_add_test(test_page_registry)
phosphorcontrol_add_test(test_application_controller)
phosphorcontrol_add_test(test_application_controller_history)
phosphorcontrol_add_test(test_localized_context)
phosphorcontrol_add_test(test_dbus_bridge)
phosphorcontrol_add_test(test_search_ranker)
phosphorcontrol_add_test(test_search_controller)
phosphorcontrol_add_test(test_sidebar_rows)
