# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# Tiny end-to-end demo proving the phosphor-settings-ui framework boots
# and routes pages. Run with:
#   cmake --build build --target phosphor-settings-ui-minimal
#   ./build/bin/phosphor-settings-ui-minimal

# Inherit the QML policies set by the parent lib's CMakeLists.txt. When
# the example is built standalone (top-level project), set them locally.
if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif()

add_executable(phosphor-settings-ui-minimal
    main.cpp
    demoapp.cpp
    demoapp.h
    generalpage.cpp
    generalpage.h
)

set(phosphor_settings_ui_minimal_qml_FILES
    qml/Main.qml
    qml/GeneralPage.qml
    qml/AboutPage.qml
)

qt_add_qml_module(phosphor-settings-ui-minimal
    URI org.phosphor.settings.ui.examples.minimal
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${phosphor_settings_ui_minimal_qml_FILES}
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/org/phosphor/settings/ui/examples/minimal
)

target_link_libraries(phosphor-settings-ui-minimal
    PRIVATE
        PhosphorSettingsUi::PhosphorSettingsUi
        PhosphorSettingsUi::PhosphorSettingsUiQml
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

set_target_properties(phosphor-settings-ui-minimal PROPERTIES
    AUTOMOC ON
)
