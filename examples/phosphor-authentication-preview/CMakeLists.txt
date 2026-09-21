# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(phosphor-authentication-preview
    main.cpp
)

qt_add_qml_module(phosphor-authentication-preview
    URI Phosphor.AuthenticationPreview
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/AuthenticationPreview
    DEPENDENCIES
        TARGET PhosphorShellPolkitQml
        TARGET PhosphorThemeQml
        TARGET PhosphorIpcQml
    QML_FILES
        Main.qml
        AuthenticationFixture.qml
)

target_compile_features(phosphor-authentication-preview PRIVATE cxx_std_20)

# This preview deliberately has no authentication service or controller link.
# Its only agent is AuthenticationFixture.qml, so even direct launch cannot
# register with Polkit, start PAM, or authorize a system operation.
target_link_libraries(phosphor-authentication-preview
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        PhosphorShellPolkitQml
        PhosphorShellPolkitQmlplugin
        PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
        PhosphorIpc::PhosphorIpc
        PhosphorIpc::PhosphorIpcQml
        PhosphorIpcQmlplugin
)
