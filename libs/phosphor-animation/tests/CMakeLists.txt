# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorAnimation unit tests. Headless — uses offscreen QPA.

find_package(Qt6 REQUIRED COMPONENTS Test)

function(pa_add_test _name)
    set(_sources ${ARGN})
    add_executable(${_name} ${_sources})
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
    # Offscreen QPA so QTimer-based tests work without a display server.
    # Silence debug chatter but keep warnings: several tests exercise
    # parse-failure paths that log qCWarning on the validation boundary
    # — a regression that moved a check to qCDebug would still pass
    # under a warning-silenced rules string, masking the regression.
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=phosphoranimation.debug=false"
        LABELS "phosphoranimation"
    )
endfunction()

pa_add_test(pa_test_easing test_easing.cpp)
pa_add_test(pa_test_spring test_spring.cpp)
pa_add_test(pa_test_curve test_curve.cpp)
pa_add_test(pa_test_curveregistry test_curveregistry.cpp)
pa_add_test(pa_test_profile test_profile.cpp)
pa_add_test(pa_test_profiletree test_profiletree.cpp)
pa_add_test(pa_test_windowmotion test_windowmotion.cpp)
pa_add_test(pa_test_animationmath test_animationmath.cpp)
pa_add_test(pa_test_animationcontroller test_animationcontroller.cpp)
pa_add_test(pa_test_staggertimer test_staggertimer.cpp)
