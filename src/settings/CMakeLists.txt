# PlasmaZones Standalone Settings Application
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compositor-agnostic settings app. Uses Kirigami for UI, no KDE System Settings dependency.

find_package(KF6Kirigami REQUIRED)

set(plasmazones_settings_SRCS
    main.cpp
    settingscontroller.h
    settingscontroller.cpp
    # DaemonController — daemon lifecycle management (start/stop/status)
    ${CMAKE_SOURCE_DIR}/src/common/daemoncontroller.h
    ${CMAKE_SOURCE_DIR}/src/common/daemoncontroller.cpp
    # Qt-only utilities (standalone-only, no KDE Framework deps)
    screenhelper.h
    screenhelper.cpp
    screenprovider.h
    screenprovider.cpp
    dbusutils.h
)

add_executable(plasmazones-settings ${plasmazones_settings_SRCS})

target_include_directories(plasmazones-settings PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

qt_add_qml_module(plasmazones-settings
    URI org.plasmazones.settings
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/GeneralPage.qml
        qml/SnappingAppearancePage.qml
        qml/SnappingEffectsPage.qml
        qml/SnappingBehaviorPage.qml
        qml/SnappingZoneSelectorPage.qml
        qml/TilingAppearancePage.qml
        qml/TilingBehaviorPage.qml
        qml/TilingAlgorithmPage.qml
        qml/ExclusionsPage.qml
        qml/EditorPage.qml
        qml/LayoutsPage.qml
        qml/AssignmentsAppRulesPage.qml
        qml/SnappingAssignmentsPage.qml
        qml/SnappingQuickShortcutsPage.qml
        qml/TilingAssignmentsPage.qml
        qml/TilingQuickShortcutsPage.qml
        qml/SnappingBridge.qml
        qml/TilingBridge.qml
        qml/AboutPage.qml
        qml/MonitorStatePage.qml
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
        qml/FontPickerDialog.qml
        qml/ColorButton.qml
        qml/InputCapture.qml
        qml/KeySequenceInput.qml
        qml/ModifierAndMouseCheckBoxes.qml
        qml/PositionPicker.qml
        qml/AlgorithmPreview.qml
        qml/LayoutThumbnail.qml
        qml/PerScreenOverrideHelper.qml
        qml/MonitorSelectorSection.qml
        qml/ExclusionListCard.qml
        qml/LayoutGridDelegate.qml
        qml/LayoutToolbar.qml
        qml/ZoneSelectorCard.qml
        qml/ZoneSelectorSection.qml
        qml/EasingPreview.qml
        qml/EasingSettings.qml
        qml/SettingsCard.qml
        qml/OsdCard.qml
        qml/ShortcutCaptureField.qml
        qml/ModifierComboBox.qml
        qml/SettingsSlider.qml
        qml/SettingsSpinBox.qml
        qml/SettingsToggleRow.qml
        qml/SettingsSwitch.qml
        qml/SettingsButtonGroup.qml
        qml/ColorSwatchRow.qml
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
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/org_plasmazones_common"
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/org_plasmazones_common/qmldir
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
