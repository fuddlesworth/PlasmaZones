# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
add_executable(test_phosphorsurfaces
    test_phosphorsurfaces.cpp
)

target_link_libraries(test_phosphorsurfaces
    PRIVATE
        PhosphorSurfaces::PhosphorSurfaces
        Qt6::Test
        Qt6::Gui
)

add_test(NAME test_phosphorsurfaces COMMAND test_phosphorsurfaces)
phosphor_apply_test_isolation(test_phosphorsurfaces)
set_property(TEST test_phosphorsurfaces APPEND PROPERTY LABELS "phosphorsurfaces")
phosphor_append_test_environment(test_phosphorsurfaces "QT_QPA_PLATFORM=offscreen")
