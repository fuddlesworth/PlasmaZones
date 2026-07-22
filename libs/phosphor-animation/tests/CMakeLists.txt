# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorAnimation unit tests. Headless — uses offscreen QPA.

find_package(Qt6 REQUIRED COMPONENTS Test Quick)

function(pa_add_test _name)
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
            Qt6::Gui
            PhosphorAnimation::PhosphorAnimation
    )
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=phosphoranimation.debug=false"
        LABELS "phosphoranimation"
    )
endfunction()

# ---- Core motion runtime tests ----

pa_add_test(pa_test_easing test_easing.cpp)
pa_add_test(pa_test_spring test_spring.cpp)
pa_add_test(pa_test_curve test_curve.cpp)
pa_add_test(pa_test_curveregistry test_curveregistry.cpp)
pa_add_test(pa_test_profile test_profile.cpp)
pa_add_test(pa_test_profiletree test_profiletree.cpp)
pa_add_test(pa_test_imotionclock test_imotionclock.cpp)
pa_add_test(pa_test_animatedvalue_scalar test_animatedvalue_scalar.cpp)
pa_add_test(pa_test_animatedvalue_geometry test_animatedvalue_geometry.cpp)
pa_add_test(pa_test_animatedvalue_color test_animatedvalue_color.cpp)
pa_add_test(pa_test_curveloader test_curveloader.cpp)
pa_add_test(pa_test_profileloader test_profileloader.cpp)
pa_add_test(pa_test_staggertimer test_staggertimer.cpp)
pa_add_test(pa_test_loader_integration test_loader_integration.cpp)
pa_add_test(pa_test_animationuniformextension test_animationuniformextension.cpp)

# QtQuickClock test gated on the same CMake flag that gates the class.
if(PHOSPHOR_ANIMATION_QUICK)
    pa_add_test(pa_test_qtquickclock test_qtquickclock.cpp)
    target_link_libraries(pa_test_qtquickclock PRIVATE Qt6::Quick)
endif()

# ---- QML wrapper tests (ex phosphor-animation-qml) ----

function(paq_add_test _name)
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
            Qt6::Gui
            Qt6::Quick
            PhosphorAnimation::PhosphorAnimation
            PhosphorAnimation::PhosphorAnimationQml
    )
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=phosphoranimation.debug=false"
        LABELS "phosphoranimation;phosphoranimation-qml"
    )
endfunction()

paq_add_test(paq_test_phosphoreasing test_phosphoreasing.cpp)
paq_add_test(paq_test_phosphorspring test_phosphorspring.cpp)
paq_add_test(paq_test_phosphorcurve test_phosphorcurve.cpp)
paq_add_test(paq_test_phosphorprofile test_phosphorprofile.cpp)
paq_add_test(paq_test_qtquickclockmanager test_qtquickclockmanager.cpp)
paq_add_test(paq_test_phosphorprofileregistry test_phosphorprofileregistry.cpp)
paq_add_test(paq_test_phosphormotionanimation test_phosphormotionanimation.cpp)
paq_add_test(paq_test_motionanimation_behavior test_motionanimation_behavior.cpp)
target_link_libraries(paq_test_motionanimation_behavior
    PRIVATE PhosphorAnimation::PhosphorAnimationQml PhosphorAnimationQmlplugin Qt6::Qml)

# ---- Shader pipeline tests (ex phosphor-animation-shaders) ----

function(pas_add_test _name)
    set(_sources ${ARGN})
    add_executable(${_name} ${_sources})
    set_target_properties(${_name} PROPERTIES AUTOMOC ON)
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            PhosphorAnimation::PhosphorAnimation
            PhosphorFsLoader::PhosphorFsLoader
    )
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "phosphoranimation;phosphoranimationshaders"
    )
endfunction()

pas_add_test(pas_test_animationshadereffect test_animationshadereffect.cpp)
pas_add_test(pas_test_shaderprofile test_shaderprofile.cpp)
pas_add_test(pas_test_shaderprofiletree test_shaderprofiletree.cpp)
pas_add_test(pas_test_animationshaderregistry test_animationshaderregistry.cpp)

# Shader-parameter translation contract, split out of the registry test
# above. Pure translateAnimationParams / slot-key logic with no filesystem
# scan, so it uses the core pa_add_test harness (no PhosphorFsLoader link).
pa_add_test(pa_test_shaderparamtranslation test_shaderparamtranslation.cpp)

# Integration test against the actual data/animations/ packs.
pas_add_test(pas_test_builtin_effects test_builtin_effects.cpp)
target_compile_definitions(pas_test_builtin_effects PRIVATE
    "PLASMAZONES_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\""
)

# ---- Layer animator tests (ex phosphor-animation-layer) ----

function(pal_add_test _name)
    set(_sources ${ARGN})
    add_executable(${_name} ${_sources})
    target_include_directories(${_name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            # Reuse the in-tree phosphor-layer test mocks.
            ${CMAKE_SOURCE_DIR}/libs/phosphor-layer/tests
    )
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            PhosphorAnimation::PhosphorAnimation
            PhosphorShellPatterns::PhosphorShellPatterns
    )
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES
        LABELS "phosphoranimation;phosphoranimationlayer"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=*.debug=false"
    )
endfunction()

pal_add_test(pal_test_surface_animator test_surface_animator.cpp)
