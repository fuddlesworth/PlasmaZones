# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Qt Test harness for the picker's C++: the candidate scan over the
# fixture tree under fixtures/ (compiled in, so ctest needs no working
# directory), the retint controller against a fake runner and the real
# PaletteStore, and the preset list over a fixture palette directory.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Test)

# Shared test isolation: a private D-Bus session plus a per-target XDG
# sandbox. Guarded so a standalone configure of this library still works
# without the top-level module.
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake")
    include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
endif()

function(phosphorshellpicker_add_test name)
    add_executable(${name} ${name}.cpp)
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_compile_definitions(${name}
        PRIVATE PHOSPHOR_PICKER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    target_link_libraries(${name}
        PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Test
            PhosphorShellPickerQml
            PhosphorShellPickerQmlplugin
            PhosphorTheme::PhosphorTheme
    )
    add_test(NAME ${name} COMMAND ${name})
    set_property(TEST ${name} APPEND PROPERTY LABELS "phosphorshellpicker")
    # The scan orders entries by name with IgnoreCase; pin the locale so a
    # tie means the same thing on every machine.
    set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT "LC_ALL=C.UTF-8")
    if(COMMAND phosphor_apply_test_isolation)
        phosphor_apply_test_isolation(${name})
    endif()
    if(COMMAND phosphor_append_test_environment)
        phosphor_append_test_environment(${name} "QT_QPA_PLATFORM=offscreen")
    endif()
endfunction()

phosphorshellpicker_add_test(test_wallpapercandidates)
phosphorshellpicker_add_test(test_retintcontroller)
phosphorshellpicker_add_test(test_themepresets)
# The QML half: Picker's preview and apply call sequence against a fake
# wallpaper service, and WallpaperSurface's crossfade against the same
# fake. Loads the real files through `import Phosphor.Picker`, so it
# links the module's registrar (already in the function above) and the
# shell library whose PanelWindow the surface is built on.
phosphorshellpicker_add_test(test_pickerwallpaper)
target_link_libraries(test_pickerwallpaper PRIVATE Qt6::Qml Qt6::Quick PhosphorShell::PhosphorShell)
