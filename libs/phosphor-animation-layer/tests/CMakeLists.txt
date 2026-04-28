# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorAnimationLayer unit tests. Headless — uses offscreen QPA.

find_package(Qt6 REQUIRED COMPONENTS Test)

function(pal_add_test _name)
    set(_sources ${ARGN})
    add_executable(${_name} ${_sources})
    target_include_directories(${_name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            # Reuse the in-tree phosphor-layer test mocks (MockTransport,
            # MockScreenProvider, makeDeps) instead of duplicating them.
            # The mocks are header-only and live next to the upstream
            # library's own tests; consuming them from here keeps every
            # Surface-driving test wired against the same harness.
            ${CMAKE_SOURCE_DIR}/libs/phosphor-layer/tests
    )
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            PhosphorAnimationLayer::PhosphorAnimationLayer
    )
    add_test(NAME ${_name} COMMAND ${_name})
    set_tests_properties(${_name} PROPERTIES
        LABELS "phosphoranimationlayer"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=*.debug=false"
    )
endfunction()

pal_add_test(pal_test_surface_animator test_surface_animator.cpp)
