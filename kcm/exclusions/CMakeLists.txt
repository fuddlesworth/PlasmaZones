# PlasmaZones Exclusions sub-KCM
# SPDX-License-Identifier: GPL-3.0-or-later

set(kcm_exclusions_SRCS
    kcmexclusions.cpp
    kcmexclusions.h
)

kcmutils_add_qml_kcm(kcm_plasmazones_exclusions
    SOURCES ${kcm_exclusions_SRCS}
)

# Embed shared QML components used by this KCM
set(_kcm_qml_prefix "/kcm/kcm_plasmazones_exclusions")

qt_add_resources(kcm_plasmazones_exclusions "kcm_exclusions_shared_qml"
    PREFIX "${_kcm_qml_prefix}"
    BASE "../ui/tabs"
    FILES
        ../ui/tabs/ExclusionListCard.qml
        ../ui/tabs/WindowPickerDialog.qml
)

target_link_libraries(kcm_plasmazones_exclusions
    PRIVATE
        plasmazones_kcm_common
        plasmazones_core
        Qt6::DBus
        KF6::KCMUtils
        KF6::ConfigWidgets
)
