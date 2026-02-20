# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones Shared QML Components

# Let qt_add_qml_module create the static library target directly
qt_add_qml_module(plasmazones_shared_qml
    URI org.plasmazones.common
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    SOURCES
        shared_qml_plugin.cpp
    QML_FILES
        CategoryBadge.qml
        LayoutCard.qml
        PopupFrame.qml
        ZonePreview.qml
        ZoneShaderRenderer.qml
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/org/plasmazones/common
)

target_link_libraries(plasmazones_shared_qml
    PUBLIC
        Qt6::Quick
        Qt6::QuickControls2
)

# Make headers available
target_include_directories(plasmazones_shared_qml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
