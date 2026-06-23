# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorserviceicontheme_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceIconTheme::PhosphorServiceIconTheme
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "phosphorserviceicontheme"
    )
endfunction()

# Full XDG icon-theme resolver coverage against a fixture theme tree.
_phosphorserviceicontheme_test(test_phosphorserviceicontheme_resolver test_iconthemeresolver.cpp)

# IconImageProvider round-trip + URL host rename + decodePixmaps.
_phosphorserviceicontheme_test(test_phosphorserviceicontheme_imageprovider test_imageprovider.cpp)
