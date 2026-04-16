# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

function(phosphorconfig_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name}
        PRIVATE
            PhosphorConfig::PhosphorConfig
            Qt6::Core
            Qt6::Gui
            Qt6::Test
    )
    add_test(NAME ${name} COMMAND ${name})
    # Qt requires an offscreen platform plugin when tests run under CI
    # without a display server. The parent project installs a global
    # environment hook that sets this; duplicate it here for standalone
    # builds where PhosphorConfig is the top-level project.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphorconfig_add_test(test_json_backend)
phosphorconfig_add_test(test_migration_runner)
phosphorconfig_add_test(test_store)
