# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-toast-demo, the Phase 3.4 acceptance demo for the
# Phosphor.Notifications toast framework. A ToastHost overlay fed by a
# real NotificationServer (so notify-send raises a toast) and by in-window
# buttons; a Do-Not-Disturb pill exercises the per-app-rules seam.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickShapes)

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# Directory-scope suppression of -Wmissing-include-dirs. Full rationale:
# src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(phosphor_toast_demo_qml
    URI Phosphor.ToastDemo
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/ToastDemo
    IMPORTS
        Phosphor.Theme
        Phosphor.Widgets
        Phosphor.Notifications
    QML_FILES
        Main.qml
)

target_link_libraries(phosphor_toast_demo_qml
    PRIVATE
        PhosphorShellNotifications::PhosphorShellNotificationsQml
        PhosphorShellWidgets::PhosphorShellWidgetsQml
        Qt6::Quick
)

target_compile_features(phosphor_toast_demo_qml PRIVATE cxx_std_20)

add_executable(phosphor-toast-demo
    main.cpp
    NotificationImageProvider.h
)

target_compile_features(phosphor-toast-demo PRIVATE cxx_std_20)

target_link_libraries(phosphor-toast-demo
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickShapes
        # The notification server (ingest) + its QML type registration.
        PhosphorServiceNotifications::PhosphorServiceNotifications
        # Static QML modules + their plugin registrars so every import in
        # Main.qml resolves at runtime.
        phosphor_toast_demo_qmlplugin
        PhosphorShellNotifications::PhosphorShellNotificationsQml
        PhosphorShellNotificationsQmlplugin
        PhosphorShellWidgets::PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)
