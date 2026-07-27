# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
function(_phosphorservicepolkit_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServicePolkit::PhosphorServicePolkit
    )
    add_test(NAME ${target} COMMAND ${target})
    phosphor_apply_test_isolation(${target})
    # APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
    # ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
    # sandbox `phosphor_apply_test_isolation` just installed.
    phosphor_append_test_environment(${target} "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(${target} PROPERTIES
            LABELS "phosphorservicepolkit"
    )
endfunction()

# Smoke test: exercises the deterministic facade contract (QML-registration
# idempotency, the default object path, inert construction / registration against
# a bogus session so no real polkitd registration can occur, and the
# cancel / authenticate / respond no-op guards).
_phosphorservicepolkit_test(test_phosphorservicepolkit_smoke test_smoke.cpp)

# QML facade test: a real QQmlEngine imports the module and proves the
# imperative registration yields a usable facade (PolkitAgent instantiates and
# binds; its registered / activeRequest properties read through QML, with
# activeRequest null on a bare agent).
_phosphorservicepolkit_test(test_phosphorservicepolkit_qmlfacade test_qmlfacade.cpp)

# Decode unit test: drives the internal Details/Identity -> Qt-types decode with
# real polkit-qt values. It links PolkitQt6-1::Core directly (the lib links it
# PRIVATE, so it is not transitive) and includes the lib's src/ for the internal
# polkitdecode.h header.
find_package(PolkitQt6-1 REQUIRED)
_phosphorservicepolkit_test(test_phosphorservicepolkit_decode test_decode.cpp)
target_link_libraries(test_phosphorservicepolkit_decode PRIVATE PolkitQt6-1::Core)
target_include_directories(test_phosphorservicepolkit_decode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
