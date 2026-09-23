# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.10 REQUIRED COMPONENTS Test Qml Quick)

include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)

function(phosphorsurfacequick_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name}
        PRIVATE
            # The STATIC QML module and its plugin, not only the C++ library:
            # the module-load slots import org.phosphor.surface, which only
            # resolves once the plugin's registration is linked in.
            PhosphorSurfaceQuick::PhosphorSurfaceQuickQml
            PhosphorSurfaceQuickQmlplugin
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            Qt6::Test
    )
    add_test(NAME ${name} COMMAND ${name})
    phosphor_apply_test_isolation(${name})
    set_property(TEST ${name} APPEND PROPERTY LABELS "phosphorsurfacequick")
    # APPEND, never PROPERTIES ENVIRONMENT: a plain set would replace the XDG
    # sandbox phosphor_apply_test_isolation just installed.
    phosphor_append_test_environment(${name} "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphorsurfacequick_add_test(test_surfaceshaderitem)
