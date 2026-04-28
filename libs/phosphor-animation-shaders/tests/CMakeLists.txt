# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

function(pas_add_test _name)
    set(_sources ${ARGN})
    add_executable(${_name} ${_sources})
    set_target_properties(${_name} PROPERTIES AUTOMOC ON)
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            PhosphorAnimationShaders::PhosphorAnimationShaders
            PhosphorAnimation::PhosphorAnimation
    )
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "phosphoranimationshaders"
    )
endfunction()

pas_add_test(pas_test_animationshadereffect test_animationshadereffect.cpp)
pas_add_test(pas_test_shaderprofile test_shaderprofile.cpp)
pas_add_test(pas_test_shaderprofiletree test_shaderprofiletree.cpp)
pas_add_test(pas_test_animationshaderregistry test_animationshaderregistry.cpp)

# Integration test against the actual data/animations/ packs in the source tree.
# Needs PLASMAZONES_SOURCE_DIR so the test can locate the data directory
# regardless of the build directory layout.
pas_add_test(pas_test_builtin_effects test_builtin_effects.cpp)
target_compile_definitions(pas_test_builtin_effects PRIVATE
    "PLASMAZONES_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\""
)
