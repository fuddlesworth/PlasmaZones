# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorservicesni_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceSni::PhosphorServiceSni
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "phosphorservicesni"
    )
endfunction()

# Smoke test: role plumbing on StatusNotifierItemModel.
_phosphorservicesni_test(test_phosphorservicesni_smoke test_smoke.cpp)

# StatusNotifierItemModel lifecycle (host attach/detach, role names
# as public contract, invalid-row no-ops).
_phosphorservicesni_test(test_phosphorservicesni_itemmodel test_itemmodel.cpp)

# DBusMenuModel without a real DBus source: just contract + no-op
# invariants. Live-source coverage requires a fake session bus + a
# fixture menu app, which is its own follow-up.
_phosphorservicesni_test(test_phosphorservicesni_dbusmenumodel test_dbusmenumodel.cpp)
