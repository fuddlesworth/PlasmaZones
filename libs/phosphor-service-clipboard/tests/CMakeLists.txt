# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
function(_phosphorserviceclipboard_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceClipboard::PhosphorServiceClipboard
    )
    add_test(NAME ${target} COMMAND ${target})
    phosphor_apply_test_isolation(${target})
    # XDG_DATA_HOME is NOT overridden here. Constructing ClipboardService loads the
    # on-disk history, so this tree must never read the real user's clipboard
    # directory — and `phosphor_apply_test_isolation` above already points
    # XDG_DATA_HOME at a per-target sandbox AND creates it at configure time.
    #
    # A second XDG_DATA_HOME entry appended here would WIN (ctest applies the list
    # in order) while pointing at a directory nothing creates, and
    # `QStandardPaths::writableLocation` does not mkpath — so the override silently
    # replaced a created sandbox with a missing one, for exactly the tree that most
    # needs a working one.
    phosphor_append_test_environment(${target} "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(${target} PROPERTIES
        LABELS "phosphorserviceclipboard"
    )
endfunction()

# Smoke test: exercises the deterministic facade contract (QML-registration
# idempotency and inert construction with no live compositor). The history
# model and persistence are covered by the dedicated history-model and store
# tests below; the live clipboard read / write path is exercised by the CLI demo
# against a real session.
_phosphorserviceclipboard_test(test_phosphorserviceclipboard_smoke test_smoke.cpp)

# QML facade test: a real QQmlEngine imports the module and proves the imperative
# registration yields a usable facade (ClipboardService instantiates, its
# history / count / supported surface reads through QML, and the invokables run).
# The shared helper isolates XDG_DATA_HOME to a build-tree dir so the service
# never touches real user clipboard history.
_phosphorserviceclipboard_test(test_phosphorserviceclipboard_qmlfacade test_qmlfacade.cpp)

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
phosphor_apply_test_isolation(test_phosphorserviceclipboard_historymodel)
# APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
# ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
# sandbox `phosphor_apply_test_isolation` just installed.
phosphor_append_test_environment(test_phosphorserviceclipboard_historymodel "QT_QPA_PLATFORM=offscreen")
set_tests_properties(test_phosphorserviceclipboard_historymodel PROPERTIES
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
phosphor_apply_test_isolation(test_phosphorserviceclipboard_store)
# APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
# ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
# sandbox `phosphor_apply_test_isolation` just installed.
phosphor_append_test_environment(test_phosphorserviceclipboard_store "QT_QPA_PLATFORM=offscreen")
set_tests_properties(test_phosphorserviceclipboard_store PROPERTIES
    LABELS "phosphorserviceclipboard"
)
