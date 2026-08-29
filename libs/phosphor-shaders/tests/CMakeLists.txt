# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)

function(phosphorshaders_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name}
        PRIVATE
            PhosphorShaders::PhosphorShaders
            Qt6::Core
            Qt6::Gui
            Qt6::Test
    )
    add_test(NAME ${name} COMMAND ${name})
    phosphor_apply_test_isolation(${name})
    set_property(TEST ${name} APPEND PROPERTY LABELS "phosphorshaders")
    # Qt requires an offscreen platform plugin when tests run under CI
    # without a display server.
    # APPEND, never PROPERTIES ENVIRONMENT: a plain set would replace the XDG
    # sandbox phosphor_apply_test_isolation just installed.
    phosphor_append_test_environment(${name} "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphorshaders_add_test(test_baseuniformprofile)
phosphorshaders_add_test(test_pixelunits)
phosphorshaders_add_test(test_shaderregistry)
