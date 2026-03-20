# PlasmaZones Standalone Settings Application
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compositor-agnostic settings app. Uses Kirigami for UI, no KDE System Settings dependency.

find_package(KF6Kirigami REQUIRED)

# NOTE: DaemonController, ScreenHelper, and ScreenProvider sources are compiled here
# directly rather than linking plasmazones_kcm_common, because that static library
# pulls in KF6::GlobalAccel and KF6::ConfigWidgets which the standalone app must avoid.
# These three classes are Qt-only (no KDE Frameworks deps).
# TODO: Extract them into a separate Qt-only static lib that both KCM and standalone
# can link, eliminating this intentional duplication.
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
        # Sub-components (copied from KCM)
        qml/MonitorAssignmentsCard.qml
        qml/ActivityAssignmentsCard.qml
        qml/AppRulesCard.qml
        qml/QuickLayoutSlotsCard.qml
        qml/WindowPickerDialog.qml
        qml/AssignmentRow.qml
        qml/LayoutComboBox.qml
        qml/ScreenComboBox.qml
        qml/WideComboBox.qml
    NO_PLUGIN
)

# Embed org.plasmazones.common QML module (ZonePreview, CategoryBadge)
qt_add_resources(plasmazones-settings "shared_common_qml"
    PREFIX "/qt/qml/org/plasmazones/common"
    BASE "${CMAKE_SOURCE_DIR}/src/shared"
    FILES
        ${CMAKE_SOURCE_DIR}/src/shared/ZonePreview.qml
        ${CMAKE_SOURCE_DIR}/src/shared/CategoryBadge.qml
)
qt_add_resources(plasmazones-settings "shared_common_qmldir"
    PREFIX "/qt/qml/org/plasmazones/common"
    BASE "${CMAKE_SOURCE_DIR}/kcm/common/org_plasmazones_common"
    FILES
        ${CMAKE_SOURCE_DIR}/kcm/common/org_plasmazones_common/qmldir
)

target_link_libraries(plasmazones-settings
    PRIVATE
        plasmazones_core
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::DBus
        Qt6::Widgets
        KF6::Kirigami
)

install(TARGETS plasmazones-settings DESTINATION ${KDE_INSTALL_BINDIR})
