# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# phosphor-registry-demo — in-process driver for PhosphorRegistry.
#
# Two built-in IBarWidgetFactory implementations register explicitly
# in main(). The bar QML enumerates the registry and instantiates a
# widget for each id. Proves the registry pattern without involving
# the plugin loader (that's the sibling plugin-demo).

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

# Bundle the demo QML + C++ glue into a static QML module so
# qmlcachegen runs on the .qml files and resources are reachable via
# qrc:/qt/qml/Phosphor/RegistryDemo/.
qt_add_qml_module(phosphor_registry_demo_qml
    URI Phosphor.RegistryDemo
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/RegistryDemo
    QML_FILES
        Main.qml
        ClockWidget.qml
        ColorSquareWidget.qml
    SOURCES
        DemoController.h
        democontroller.cpp
        QmlComponentBarWidgetFactory.h
        qmlcomponentbarwidgetfactory.cpp
)

target_link_libraries(phosphor_registry_demo_qml
    PRIVATE
        PhosphorRegistry::PhosphorRegistry
        Qt6::Quick
)

target_compile_features(phosphor_registry_demo_qml PRIVATE cxx_std_20)

add_executable(phosphor-registry-demo
    main.cpp
)

target_compile_features(phosphor-registry-demo PRIVATE cxx_std_20)

target_link_libraries(phosphor-registry-demo
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        # Static QML module plugin registrar — without this the
        # runtime `import Phosphor.RegistryDemo` would fail.
        phosphor_registry_demo_qmlplugin
        PhosphorRegistry::PhosphorRegistry
        PhosphorTheme::PhosphorTheme
        PhosphorThemeQml
        PhosphorThemeQmlplugin
)
