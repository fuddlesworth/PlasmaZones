# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
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
    phosphor_apply_test_isolation(${name})
    set_property(TEST ${name} APPEND PROPERTY LABELS "phosphortheme")
    phosphor_append_test_environment(${name} "QT_QPA_PLATFORM=offscreen")
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
