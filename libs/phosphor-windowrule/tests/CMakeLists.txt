# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorWindowRule unit tests. Headless — uses offscreen QPA.

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
            PhosphorWindowRule::PhosphorWindowRule
    )
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=org.phosphor.windowrule.*=false"
        LABELS "phosphorwindowrule"
    )
endfunction()

# Variant for the bridge tests — the AnimationAppRuleBridge header ships in
# <PhosphorAnimation/AnimationAppRuleBridge.h> and pulls in
# <PhosphorAnimation/AnimationAppRule.h>, so the bridge test needs the
# PhosphorAnimation headers on its include path and the symbols at link.
function(pwr_add_bridge_test _name)
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
            PhosphorWindowRule::PhosphorWindowRule
            PhosphorAnimation::PhosphorAnimation
    )
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=org.phosphor.windowrule.*=false"
        LABELS "phosphorwindowrule"
    )
endfunction()

pwr_add_test(phosphorwindowrule_test_matchtypes test_matchtypes.cpp)
pwr_add_test(phosphorwindowrule_test_windowquery test_windowquery.cpp)
pwr_add_test(phosphorwindowrule_test_matchexpression test_matchexpression.cpp)
pwr_add_test(phosphorwindowrule_test_ruleaction test_ruleaction.cpp)
pwr_add_test(phosphorwindowrule_test_actionregistry test_actionregistry.cpp)
pwr_add_test(phosphorwindowrule_test_windowrule test_windowrule.cpp)
pwr_add_test(phosphorwindowrule_test_windowruleset test_windowruleset.cpp)
pwr_add_test(phosphorwindowrule_test_windowrulestore test_windowrulestore.cpp)
pwr_add_test(phosphorwindowrule_test_ruleevaluator test_ruleevaluator.cpp)
pwr_add_test(phosphorwindowrule_test_ruleevaluator_cascade test_ruleevaluator_cascade.cpp)
pwr_add_test(phosphorwindowrule_test_ruleevaluator_benchmark test_ruleevaluator_benchmark.cpp)

# Phase 2 in-memory bridges.
pwr_add_test(phosphorwindowrule_test_exclusionlistbridge test_exclusionlistbridge.cpp)
pwr_add_test(phosphorwindowrule_test_contextrulebridge test_contextrulebridge.cpp)
pwr_add_bridge_test(phosphorwindowrule_test_animationapprulebridge test_animationapprulebridge.cpp)
