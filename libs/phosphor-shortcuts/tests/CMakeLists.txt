# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

function(phosphorshortcuts_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name}
        PRIVATE
            PhosphorShortcuts::PhosphorShortcuts
            Qt6::Core
            Qt6::Gui
            Qt6::Test
    )
    add_test(NAME ${name} COMMAND ${name})
    # Tests use a signals-only IBackend fake — no D-Bus, no display needed,
    # but Qt still wants a platform plugin at QGuiApplication startup. Use
    # offscreen so headless CI doesn't break.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphorshortcuts_add_test(test_registry)
