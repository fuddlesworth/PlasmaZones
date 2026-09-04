# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Qt Test harness for the launcher core. Pure C++ over the matcher and the
# .desktop parser; the scanner tests read the fixture tree under
# fixtures/, whose path is compiled in so ctest needs no working directory.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Test)

# Shared test isolation: a private D-Bus session plus a per-target XDG
# sandbox. Guarded so a standalone `git subtree split` configure of this
# library still works without the top-level module.
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
endif()

function(phosphorshelllauncher_add_test name)
    add_executable(${name} ${name}.cpp)
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_compile_definitions(${name}
        PRIVATE PHOSPHOR_LAUNCHER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    target_link_libraries(${name}
        PRIVATE
            Qt6::Core
            Qt6::Test
            PhosphorShellLauncher::PhosphorShellLauncher
    )
    add_test(NAME ${name} COMMAND ${name})
    set_property(TEST ${name} APPEND PROPERTY LABELS "phosphorshelllauncher")
    if(COMMAND phosphor_apply_test_isolation)
        phosphor_apply_test_isolation(${name})
    endif()
endfunction()

phosphorshelllauncher_add_test(test_fuzzymatcher)
phosphorshelllauncher_add_test(test_desktopentry)
phosphorshelllauncher_add_test(test_calculatorprovider)
phosphorshelllauncher_add_test(test_commandprovider)
phosphorshelllauncher_add_test(test_appsprovider)
phosphorshelllauncher_add_test(test_launchermodel)

# The surface's keyboard contract, as a QtQuickTest over a fake model.
# Links the Phosphor.Launcher module + its registrar, plus the modules it
# imports and their registrars, so the QML engine resolves every import.
find_package(Qt6 6.6 REQUIRED COMPONENTS Gui Qml Quick QuickShapes QuickTest)
add_executable(test_phosphor_shell_launcher_qml tst_launcher_qml.cpp)
target_compile_features(test_phosphor_shell_launcher_qml PRIVATE cxx_std_20)
target_link_libraries(test_phosphor_shell_launcher_qml
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickShapes
        Qt6::QuickTest
        PhosphorShellLauncherQml
        PhosphorShellLauncherQmlplugin
        PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)
add_test(
    NAME test_phosphor_shell_launcher_qml
    COMMAND test_phosphor_shell_launcher_qml -input ${CMAKE_CURRENT_SOURCE_DIR}
)
set_property(TEST test_phosphor_shell_launcher_qml APPEND PROPERTY LABELS "phosphorshelllauncher")
if(COMMAND phosphor_apply_test_isolation)
    phosphor_apply_test_isolation(test_phosphor_shell_launcher_qml)
    phosphor_append_test_environment(test_phosphor_shell_launcher_qml "QT_QPA_PLATFORM=offscreen")
else()
    set_property(TEST test_phosphor_shell_launcher_qml APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
