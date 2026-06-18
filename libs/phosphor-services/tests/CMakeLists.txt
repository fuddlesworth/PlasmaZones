# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

# Helper — every PhosphorServices test runs with the same offscreen
# QPA + label so ctest filtering / the support-report bundle picks
# them up uniformly.
function(_phosphorservices_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServices::PhosphorServices
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "phosphorservices"
    )
endfunction()

# Smoke test — singletons + role plumbing.
_phosphorservices_test(test_phosphorservices_smoke test_smoke.cpp)

# Full XDG icon-theme resolver coverage against a fixture theme tree.
_phosphorservices_test(test_phosphorservices_iconthemeresolver test_iconthemeresolver.cpp)

# StatusNotifierItemModel lifecycle (host attach/detach, role names
# as public contract, invalid-row no-ops).
_phosphorservices_test(test_phosphorservices_itemmodel test_itemmodel.cpp)

# DBusMenuModel without a real DBus source — just contract + no-op
# invariants. Live-source coverage requires a fake session bus + a
# fixture menu app, which is its own follow-up.
_phosphorservices_test(test_phosphorservices_dbusmenumodel test_dbusmenumodel.cpp)
