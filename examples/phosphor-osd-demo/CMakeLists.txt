# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-osd-demo, the Phase 3.3 acceptance demo for the Phosphor.OSD
# framework. An OSDHost overlay driven by in-window buttons and by
# `phosphorctl call osd.show <kind> <value>`. The four built-in OSDs are
# supplied by OSDController's Registry<IOSDFactory>.

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickShapes)

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# Directory-scope suppression of -Wmissing-include-dirs. Full rationale:
# src/shared/CMakeLists.txt.
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")

qt_add_qml_module(phosphor_osd_demo_qml
    URI Phosphor.OsdDemo
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/OsdDemo
    IMPORTS
        Phosphor.Theme
        Phosphor.Widgets
        Phosphor.OSD
        Phosphor.Ipc
    QML_FILES
        Main.qml
)

target_link_libraries(phosphor_osd_demo_qml
    PRIVATE
        PhosphorShellOsd::PhosphorShellOsdQml
        PhosphorShellWidgets::PhosphorShellWidgetsQml
        PhosphorIpc::PhosphorIpcQml
        Qt6::Quick
)

target_compile_features(phosphor_osd_demo_qml PRIVATE cxx_std_20)

add_executable(phosphor-osd-demo
    main.cpp
    OSDController.h
    osdcontroller.cpp
    QmlComponentOSDFactory.h
    qmlcomponentosdfactory.cpp
)

# Per-build IPC socket so concurrent demos don't collide; overridable at
# runtime via $PHOSPHOR_SOCKET.
target_compile_definitions(phosphor-osd-demo
    PRIVATE PHOSPHOR_OSD_DEMO_SOCKET="${CMAKE_BINARY_DIR}/phosphor-osd-demo.sock"
)

target_compile_features(phosphor-osd-demo PRIVATE cxx_std_20)

target_link_libraries(phosphor-osd-demo
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickShapes
        # C++ cores: the registry (controller) and the IPC router/engine.
        PhosphorRegistry::PhosphorRegistry
        PhosphorIpc::PhosphorIpc
        # Static QML modules + their plugin registrars so every import in
        # Main.qml resolves at runtime.
        phosphor_osd_demo_qmlplugin
        PhosphorShellOsd::PhosphorShellOsdQml
        PhosphorShellOsdQmlplugin
        PhosphorShellWidgets::PhosphorShellWidgetsQml
        PhosphorShellWidgetsQmlplugin
        PhosphorIpc::PhosphorIpcQml
        PhosphorIpcQmlplugin
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)
