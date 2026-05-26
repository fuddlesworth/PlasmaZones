# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 REQUIRED COMPONENTS Test DBus)

function(phosphorsettingsui_add_test name)
    add_executable(${name} ${name}.cpp)
    # `Qt6::Gui` was previously listed here for QGuiApplication, but
    # every existing test extends QObject (not QGuiApplication) — the
    # offscreen-QPA env var is enough. Dropped to keep the test
    # binaries headless and to skip the platform plugin's startup
    # cost on CI runners.
    target_link_libraries(${name}
        PRIVATE
            PhosphorSettingsUi::PhosphorSettingsUi
            Qt6::Core
            Qt6::DBus
            Qt6::Test
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

phosphorsettingsui_add_test(test_page_registry)
phosphorsettingsui_add_test(test_application_controller)
phosphorsettingsui_add_test(test_localized_context)
phosphorsettingsui_add_test(test_dbus_bridge)
