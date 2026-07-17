# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorScreens unit tests. Headless — uses offscreen QPA.

find_package(Qt6 REQUIRED COMPONENTS Test)

function(ps_add_test _name)
    set(_sources ${ARGN})
    add_executable(${_name} ${_sources})
    # Explicit per-target properties rather than relying on directory/
    # parent-project inheritance: (1) AUTOMOC because each test has its
    # own Q_OBJECT class that needs MOC generation, (2) cxx_std_20 so a
    # future CMake preset scoping CXX_STANDARD differently doesn't
    # quietly downgrade the tests to 17.
    set_target_properties(${_name} PROPERTIES AUTOMOC ON)
    target_compile_features(${_name} PRIVATE cxx_std_20)
    target_include_directories(${_name}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
    # Default to the QtDBus-free core; D-Bus tests opt into the full
    # PhosphorScreens target below.
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::Gui
            PhosphorScreens::Core
    )
    add_test(NAME ${_name} COMMAND ${_name})
    # Offscreen QPA so QTimer-based tests work without a display server.
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=phosphor.screens.debug=false"
        LABELS "phosphorscreens"
    )
endfunction()

ps_add_test(ps_test_virtualscreen test_virtualscreen.cpp)
ps_add_test(ps_test_screeninfo_variantlist test_screeninfo_variantlist.cpp)
ps_add_test(ps_test_inmemoryconfigstore test_inmemoryconfigstore.cpp)
ps_add_test(ps_test_swapper test_swapper.cpp)
ps_add_test(ps_test_screenidentity_variants test_screenidentity_variants.cpp)
ps_add_test(ps_test_screeninfo_variant test_screeninfo_variant.cpp)
ps_add_test(ps_test_manager_lifecycle test_manager_lifecycle.cpp)
ps_add_test(ps_test_screenmanager_geometry test_screenmanager_geometry.cpp FakeScreenProvider.cpp FakeScreenProvider.h)
ps_add_test(ps_test_manager_virtualscreens test_manager_virtualscreens.cpp)
ps_add_test(ps_test_resolver test_resolver.cpp)
ps_add_test(ps_test_plasma_panel_source test_plasma_panel_source.cpp)
ps_add_test(ps_test_dbusscreenadaptor test_dbusscreenadaptor.cpp)

# These construct/exercise ScreenManager, which lives in the Runtime target
# (its geometry sensors need PhosphorWayland); the pure-POD Core no longer
# carries ScreenManager. Runtime re-exports Core, so the POD types stay available.
target_link_libraries(ps_test_manager_lifecycle PRIVATE PhosphorScreens::Runtime)
target_link_libraries(ps_test_manager_virtualscreens PRIVATE PhosphorScreens::Runtime)
target_link_libraries(ps_test_screenmanager_geometry PRIVATE PhosphorScreens::Runtime)

# These exercise the D-Bus surface (DBusScreenAdaptor, PlasmaPanelSource,
# Resolver), so they link the full PhosphorScreens target rather than the
# QtDBus-free core. PhosphorScreens carries Qt6::DBus PUBLIC.
target_link_libraries(ps_test_resolver PRIVATE PhosphorScreens::PhosphorScreens)
target_link_libraries(ps_test_plasma_panel_source PRIVATE PhosphorScreens::PhosphorScreens)
target_link_libraries(ps_test_dbusscreenadaptor PRIVATE PhosphorScreens::PhosphorScreens)
