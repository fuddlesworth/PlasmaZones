# PlasmaZones Layouts sub-KCM
# SPDX-License-Identifier: GPL-3.0-or-later

set(kcm_layouts_SRCS
    kcmlayouts.cpp
    kcmlayouts.h
)

kcmutils_add_qml_kcm(kcm_plasmazones_layouts
    SOURCES ${kcm_layouts_SRCS}
)

# Embed shared QML components used by this KCM
set(_kcm_qml_prefix "/kcm/kcm_plasmazones_layouts")

qt_add_resources(kcm_plasmazones_layouts "kcm_layouts_shared_qml"
    PREFIX "${_kcm_qml_prefix}"
    BASE "../ui"
    FILES
        ../ui/LayoutThumbnail.qml
        ../ui/LayoutComboBox.qml
        ../ui/ScreenComboBox.qml
        ../ui/WideComboBox.qml
)

# Embed tab components used by this KCM
qt_add_resources(kcm_plasmazones_layouts "kcm_layouts_tabs_qml"
    PREFIX "${_kcm_qml_prefix}"
    BASE "../ui/tabs"
    FILES
        ../ui/tabs/LayoutToolbar.qml
        ../ui/tabs/LayoutGridDelegate.qml
        ../ui/tabs/AlgorithmPreview.qml
)

# Embed shared QML types (ZonePreview, CategoryBadge) so that
# `import org.plasmazones.common` resolves from qrc without needing
# the static plugin (which cannot be linked into sub-KCMs).
qt_add_resources(kcm_plasmazones_layouts "kcm_layouts_shared_types"
    PREFIX "/qt/qml/org/plasmazones/common"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/shared"
    FILES
        ../../src/shared/ZonePreview.qml
        ../../src/shared/CategoryBadge.qml
)
qt_add_resources(kcm_plasmazones_layouts "kcm_layouts_shared_qmldir"
    PREFIX "/qt/qml/org/plasmazones/common"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../common/org_plasmazones_common"
    FILES
        ../common/org_plasmazones_common/qmldir
)

target_link_libraries(kcm_plasmazones_layouts
    PRIVATE
        plasmazones_kcm_common
        plasmazones_core
        Qt6::DBus
        KF6::KCMUtils
        KF6::ConfigWidgets
)
