# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

function(phosphortheme_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name}
        PRIVATE
            PhosphorTheme::PhosphorTheme
            Qt6::Core
            Qt6::Gui
            Qt6::Test
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphortheme_add_test(test_palettestore)
# test_palettestore_files holds the loadFromFile slots; test_palettestore_hotreload
# holds the hot-reload slots + the white-box resetToDefaults_releasesDirectoryWatch
# slot. All three share tests/test_palettestore_helpers.h for the
# QTemporaryDir + QFile fixture-write scaffolding.
phosphortheme_add_test(test_palettestore_files)
phosphortheme_add_test(test_palettestore_hotreload)
phosphortheme_add_test(test_templateengine)
phosphortheme_add_test(test_matugenrunner)
