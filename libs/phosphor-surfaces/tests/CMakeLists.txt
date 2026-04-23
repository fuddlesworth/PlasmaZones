# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

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
set_tests_properties(test_phosphorsurfaces PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
