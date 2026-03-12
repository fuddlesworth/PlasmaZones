# PlasmaZones Assignments sub-KCM
# SPDX-License-Identifier: GPL-3.0-or-later

set(kcm_assignments_SRCS
    kcmassignments.cpp
    kcmassignments.h
)

kcmutils_add_qml_kcm(kcm_plasmazones_assignments
    SOURCES ${kcm_assignments_SRCS}
)

# Embed shared QML components used by this KCM
set(_kcm_qml_prefix "/kcm/kcm_plasmazones_assignments")

qt_add_resources(kcm_plasmazones_assignments "kcm_assignments_shared_qml"
    PREFIX "${_kcm_qml_prefix}"
    BASE "../ui"
    FILES
        ../ui/AssignmentRow.qml
        ../ui/LayoutComboBox.qml
        ../ui/ScreenComboBox.qml
        ../ui/WideComboBox.qml
)

# Embed tab components used by this KCM
qt_add_resources(kcm_plasmazones_assignments "kcm_assignments_tabs_qml"
    PREFIX "${_kcm_qml_prefix}"
    BASE "../ui/tabs"
    FILES
        ../ui/tabs/MonitorAssignmentsCard.qml
        ../ui/tabs/ActivityAssignmentsCard.qml
        ../ui/tabs/QuickLayoutSlotsCard.qml
        ../ui/tabs/AppRulesCard.qml
        ../ui/tabs/WindowPickerDialog.qml
)

# Embed shared QML types (ZonePreview, CategoryBadge) so that
# `import org.plasmazones.common` resolves from qrc without needing
# the static plugin (which cannot be linked into sub-KCMs).
qt_add_resources(kcm_plasmazones_assignments "kcm_assignments_shared_types"
    PREFIX "/qt/qml/org/plasmazones/common"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/shared"
    FILES
        ../../src/shared/ZonePreview.qml
        ../../src/shared/CategoryBadge.qml
)
qt_add_resources(kcm_plasmazones_assignments "kcm_assignments_shared_qmldir"
    PREFIX "/qt/qml/org/plasmazones/common"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../common/org_plasmazones_common"
    FILES
        ../common/org_plasmazones_common/qmldir
)

target_link_libraries(kcm_plasmazones_assignments
    PRIVATE
        plasmazones_kcm_common
        plasmazones_core
        Qt6::DBus
        KF6::KCMUtils
        KF6::ConfigWidgets
)
