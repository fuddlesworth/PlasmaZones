# PlasmaZones Editor sub-KCM
# SPDX-License-Identifier: GPL-3.0-or-later

set(kcm_editor_SRCS
    kcmeditor.cpp
    kcmeditor.h
)

kcmutils_add_qml_kcm(kcm_plasmazones_editor
    SOURCES ${kcm_editor_SRCS}
)

# Embed shared QML components used by this KCM
set(_kcm_qml_prefix "/kcm/kcm_plasmazones_editor")

qt_add_resources(kcm_plasmazones_editor "kcm_editor_shared_qml"
    PREFIX "${_kcm_qml_prefix}"
    BASE "../ui"
    FILES
        ../ui/KeySequenceInput.qml
        ../ui/ModifierAndMouseCheckBoxes.qml
        ../ui/InputCapture.qml
)

target_link_libraries(kcm_plasmazones_editor
    PRIVATE
        plasmazones_core
        plasmazones_kcm_common
        Qt6::DBus
        KF6::KCMUtils
        KF6::ConfigWidgets
        KF6::ConfigCore
)
