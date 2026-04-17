# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# PhosphorLayer unit tests. Headless — uses offscreen QPA.

find_package(Qt6 REQUIRED COMPONENTS Test)

# Shared includes: mocks/ (in-tree header-only) + library public headers (via target).
function(pl_add_test _name)
    set(_sources ${ARGN})
    add_executable(${_name} ${_sources})
    target_include_directories(${_name}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${_name}
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            PhosphorLayer::PhosphorLayer
    )
    add_test(NAME ${_name} COMMAND ${_name})
    # Offscreen QPA so QQuickWindow can construct without a display server.
    # LABELS lets `ctest -L phosphorlayer` select only our tests in a build
    # that also runs PlasmaZones unit tests; keep in sync with the docs.
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=phosphorlayer.debug=false;phosphorlayer.warning=false"
        LABELS "phosphorlayer"
    )
endfunction()

pl_add_test(pl_test_role test_role.cpp)
pl_add_test(pl_test_surface test_surface.cpp)
pl_add_test(pl_test_factory test_factory.cpp)
pl_add_test(pl_test_registry test_registry.cpp)
pl_add_test(pl_test_topology test_topology.cpp)
pl_add_test(pl_test_content_url test_content_url.cpp)
pl_add_test(pl_test_default_screen_provider test_default_screen_provider.cpp)
pl_add_test(pl_test_extensions test_extensions.cpp)
