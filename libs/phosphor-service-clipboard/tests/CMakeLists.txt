# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorserviceclipboard_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceClipboard::PhosphorServiceClipboard
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "phosphorserviceclipboard"
    )
endfunction()

# Smoke test: exercises the deterministic facade contract (QML-registration
# idempotency and inert construction with no live compositor). The history
# model, persistence, and live clipboard read / write are exercised by the unit
# tests and the CLI demo as they land.
_phosphorserviceclipboard_test(test_phosphorserviceclipboard_smoke test_smoke.cpp)

# History-model unit test: exercises the dedup / cap / preview / sensitive-drop
# logic on a fake source. It compiles the model + seam directly (and includes the
# internal src/ headers) rather than linking the lib, so it needs no compositor
# and no phosphor-wayland; the internal classes stay unexported.
add_executable(test_phosphorserviceclipboard_historymodel
    test_historymodel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/clipboardhistorymodel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/iclipboardsource.cpp
)
target_include_directories(test_phosphorserviceclipboard_historymodel
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_phosphorserviceclipboard_historymodel PRIVATE Qt6::Test)
add_test(NAME test_phosphorserviceclipboard_historymodel COMMAND test_phosphorserviceclipboard_historymodel)
set_tests_properties(test_phosphorserviceclipboard_historymodel
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "phosphorserviceclipboard"
)

# Store unit test: round-trips entries through a temp directory (no real user
# data). Compiles clipboardstore.cpp directly; no lib link, no compositor.
add_executable(test_phosphorserviceclipboard_store
    test_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/clipboardstore.cpp
)
target_include_directories(test_phosphorserviceclipboard_store
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(test_phosphorserviceclipboard_store PRIVATE Qt6::Test)
add_test(NAME test_phosphorserviceclipboard_store COMMAND test_phosphorserviceclipboard_store)
set_tests_properties(test_phosphorserviceclipboard_store
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "phosphorserviceclipboard"
)
