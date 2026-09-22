# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-launcher-demo, the Phase 4.2 acceptance demo for the
# Phosphor.Launcher surface. The spotlight launcher in a plain window,
# driven by the real providers over this machine's applications,
# clipboard and PATH, supplied through a Registry<ILauncherProviderFactory>.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickShapes)

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# Directory-scope suppression of -Wmissing-include-dirs. Full rationale:
# src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(phosphor_launcher_demo_qml
    URI Phosphor.LauncherDemo
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/LauncherDemo
    IMPORTS
        Phosphor.Theme
        Phosphor.Widgets
        Phosphor.Launcher
    QML_FILES
        Main.qml
)

target_link_libraries(phosphor_launcher_demo_qml
    PRIVATE
        PhosphorShellLauncher::PhosphorShellLauncherQml
        PhosphorShellWidgets::PhosphorShellWidgetsQml
        Qt6::Quick
)

target_compile_features(phosphor_launcher_demo_qml PRIVATE cxx_std_20)

add_executable(phosphor-launcher-demo
    main.cpp
    LauncherController.h
    launchercontroller.cpp
)

target_compile_features(phosphor-launcher-demo PRIVATE cxx_std_20)

target_link_libraries(phosphor-launcher-demo
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickShapes
        # The registry behind the controller, the launcher core (model +
        # providers), and the clipboard service the clipboard provider
        # reads by name.
        PhosphorRegistry::PhosphorRegistry
        PhosphorShellLauncher::PhosphorShellLauncher
        PhosphorServiceClipboard::PhosphorServiceClipboard
        # Static QML modules + their plugin registrars so every import in
        # Main.qml resolves at runtime.
        phosphor_launcher_demo_qmlplugin
        PhosphorShellLauncher::PhosphorShellLauncherQml
        PhosphorShellLauncherQmlplugin
        PhosphorShellWidgets::PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)
