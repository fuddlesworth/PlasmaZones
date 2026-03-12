# PlasmaZones General sub-KCM (Animations + OSD)
# SPDX-License-Identifier: GPL-3.0-or-later

set(kcm_general_SRCS
    kcmgeneral.cpp
    kcmgeneral.h
)

kcmutils_add_qml_kcm(kcm_plasmazones_general
    SOURCES ${kcm_general_SRCS}
)

# Embed shared QML components used by this KCM
set(_kcm_qml_prefix "/kcm/kcm_plasmazones_general")

qt_add_resources(kcm_plasmazones_general "kcm_general_shared_qml"
    PREFIX "${_kcm_qml_prefix}"
    BASE "../ui"
    FILES
        ../ui/EasingPreview.qml
        ../ui/WideComboBox.qml
)

# Embed local QML components (extracted from main.qml)
qt_add_resources(kcm_plasmazones_general "kcm_general_local_qml"
    PREFIX "${_kcm_qml_prefix}"
    BASE "ui"
    FILES
        ui/EasingSettings.qml
        ui/OsdCard.qml
)

target_link_libraries(kcm_plasmazones_general
    PRIVATE
        plasmazones_kcm_common
        plasmazones_core
        Qt6::DBus
        KF6::KCMUtils
        KF6::ConfigWidgets
        KF6::ConfigCore
)
