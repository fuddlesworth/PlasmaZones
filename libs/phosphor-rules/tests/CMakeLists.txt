# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorRules unit tests. Headless — QCoreApplication only; every
# suite uses QTEST_GUILESS_MAIN so the guiless contract is pinned in source
# rather than left contingent on link flags. `Qt6::Gui` is intentionally NOT
# a link dependency (the library itself is Core-only — see ../CMakeLists.txt's
# `Core only — no Gui` comment), so no `QT_QPA_PLATFORM` env var is needed:
# there is no QPA layer to choose.

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
function(pwr_add_test _name)
    set(_sources ${ARGN})
    add_executable(${_name} ${_sources})
    set_target_properties(${_name} PROPERTIES AUTOMOC ON)
    target_include_directories(${_name}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            PhosphorRules::PhosphorRules
    )
    add_test(NAME ${_name} COMMAND ${_name})
    phosphor_apply_test_isolation(${_name})
    # APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
    # ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
    # sandbox `phosphor_apply_test_isolation` just installed.
    phosphor_append_test_environment(${_name} "QT_LOGGING_RULES=org.phosphor.rule.*=false")
    set_tests_properties(${_name} PROPERTIES
        LABELS "phosphorrules"
    )
endfunction()

pwr_add_test(phosphorrules_test_matchtypes test_matchtypes.cpp)
pwr_add_test(phosphorrules_test_windowquery test_windowquery.cpp)
pwr_add_test(phosphorrules_test_matchexpression test_matchexpression.cpp)
pwr_add_test(phosphorrules_test_ruleaction test_ruleaction.cpp)
pwr_add_test(phosphorrules_test_ruleaction_contextbools test_ruleaction_contextbools.cpp)
pwr_add_test(phosphorrules_test_ruleaction_tilingparams test_ruleaction_tilingparams.cpp)
pwr_add_test(phosphorrules_test_ruleaction_placement test_ruleaction_placement.cpp)
pwr_add_test(phosphorrules_test_actionregistry test_actionregistry.cpp)
pwr_add_test(phosphorrules_test_rule test_rule.cpp)
pwr_add_test(phosphorrules_test_ruleset test_ruleset.cpp)
pwr_add_test(phosphorrules_test_rulestore test_rulestore.cpp)
pwr_add_test(phosphorrules_test_rulestorewatcher test_rulestorewatcher.cpp)
pwr_add_test(phosphorrules_test_ruleevaluator test_ruleevaluator.cpp)
pwr_add_test(phosphorrules_test_ruleevaluator_cascade test_ruleevaluator_cascade.cpp)
pwr_add_test(phosphorrules_test_ruleevaluator_benchmark test_ruleevaluator_benchmark.cpp)

# Phase 2 in-memory bridges.
pwr_add_test(phosphorrules_test_contextrulebridge test_contextrulebridge.cpp)
# Slicers for the `Exclude` / `ExcludeAnimations` rule subsets that the
# snap-engine, the KWin effect's drag gate, and the effect's
# `shouldAnimateWindow` gate bind to their evaluators. Covers the disabled-
# rule skip in `rulesWithAction`, the action-type filter shared by
# `excludeRulesFrom` / `excludeAnimationsRulesFrom`, and the leaf-shape
# harvest in `applicationExcludePatternsFrom` (Field::AppId + Operator::
# AppIdMatches + non-empty pattern, with disabled / non-leaf / non-AppId
# rules dropped). The v3→v4 migration tests exercise the END-TO-END id
# derivation; this file covers the helpers in isolation.
pwr_add_test(phosphorrules_test_exclusionrules test_exclusionrules.cpp)
