# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorRules unit tests. Headless — QCoreApplication only; every
# suite uses QTEST_GUILESS_MAIN so the guiless contract is pinned in source
# rather than left contingent on link flags. `Qt6::Gui` is intentionally NOT
# a link dependency (the library itself is Core-only — see ../CMakeLists.txt's
# `Core only — no Gui` comment), so no `QT_QPA_PLATFORM` env var is needed:
# there is no QPA layer to choose.

find_package(Qt6 REQUIRED COMPONENTS Test)

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
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_LOGGING_RULES=org.phosphor.rule.*=false"
        LABELS "phosphorrules"
    )
endfunction()

pwr_add_test(phosphorrules_test_matchtypes test_matchtypes.cpp)
pwr_add_test(phosphorrules_test_windowquery test_windowquery.cpp)
pwr_add_test(phosphorrules_test_matchexpression test_matchexpression.cpp)
pwr_add_test(phosphorrules_test_ruleaction test_ruleaction.cpp)
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
