# PlasmaZones Standalone Settings Application
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compositor-agnostic settings app. Uses Kirigami for UI, no KDE System Settings dependency.

find_package(KF6Kirigami REQUIRED)

set(plasmazones_settings_SRCS
    main.cpp
    settingscontroller.h
    settingscontroller.cpp
    ${CMAKE_SOURCE_DIR}/kcm/common/daemoncontroller.h
    ${CMAKE_SOURCE_DIR}/kcm/common/daemoncontroller.cpp
    ${CMAKE_SOURCE_DIR}/kcm/common/screenhelper.h
    ${CMAKE_SOURCE_DIR}/kcm/common/screenhelper.cpp
    ${CMAKE_SOURCE_DIR}/kcm/common/screenprovider.h
    ${CMAKE_SOURCE_DIR}/kcm/common/screenprovider.cpp
)

add_executable(plasmazones-settings ${plasmazones_settings_SRCS})

target_include_directories(plasmazones-settings PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/kcm/common
)

qt_add_qml_module(plasmazones-settings
    URI org.plasmazones.settings
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/GeneralPage.qml
        qml/SnappingPage.qml
        qml/AutotilingPage.qml
        qml/ExclusionsPage.qml
        qml/EditorPage.qml
        qml/LayoutsPage.qml
        qml/AssignmentsPage.qml
        qml/AboutPage.qml
    NO_PLUGIN
)

target_link_libraries(plasmazones-settings
    PRIVATE
        plasmazones_core
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::DBus
        Qt6::Widgets
)

install(TARGETS plasmazones-settings DESTINATION ${KDE_INSTALL_BINDIR})
