# PlasmaZones Snapping sub-KCM
# SPDX-License-Identifier: GPL-3.0-or-later

set(kcm_snapping_SRCS
    kcmsnapping.cpp
    kcmsnapping_appearance.cpp
    kcmsnapping_zoneselector.cpp
    kcmsnapping.h
)

# KConfigXT symbols are provided by plasmazones_kcm_common (linked below)

kcmutils_add_qml_kcm(kcm_plasmazones_snapping
    SOURCES ${kcm_snapping_SRCS}
)

# Embed shared QML components used by this KCM
set(_kcm_qml_prefix "/kcm/kcm_plasmazones_snapping")

qt_add_resources(kcm_plasmazones_snapping "kcm_snapping_shared_qml"
    PREFIX "${_kcm_qml_prefix}"
    BASE "../ui"
    FILES
        ../ui/ColorButton.qml
        ../ui/FontPickerDialog.qml
        ../ui/InputCapture.qml
        ../ui/ModifierAndMouseCheckBoxes.qml
        ../ui/PerScreenOverrideHelper.qml
        ../ui/MonitorSelectorSection.qml
        ../ui/ScreenComboBox.qml
        ../ui/WideComboBox.qml
        ../ui/PositionPicker.qml
)

# Embed local QML components (extracted from main.qml)
qt_add_resources(kcm_plasmazones_snapping "kcm_snapping_local_qml"
    PREFIX "${_kcm_qml_prefix}"
    BASE "ui"
    FILES
        ui/AppearanceCard.qml
        ui/ZoneSelectorCard.qml
)

# Embed ZoneSelectorSection from tabs
qt_add_resources(kcm_plasmazones_snapping "kcm_snapping_tabs_qml"
    PREFIX "${_kcm_qml_prefix}"
    BASE "../ui/tabs"
    FILES
        ../ui/tabs/ZoneSelectorSection.qml
)

target_link_libraries(kcm_plasmazones_snapping
    PRIVATE
        plasmazones_core
        plasmazones_kcm_common
        Qt6::DBus
        KF6::KCMUtils
        KF6::ConfigWidgets
)
