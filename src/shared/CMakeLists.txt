# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones Shared QML Components

# Static QML module is linked into shared objects (KCM, editor) so all
# targets created by qt_add_qml_module (including resource sub-targets)
# must be compiled with -fPIC. Setting this before the call ensures all
# generated targets inherit it.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

qt_add_qml_module(plasmazones_shared_qml
    URI org.plasmazones.common
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    SOURCES
        shared_qml_plugin.cpp
    QML_FILES
        AspectRatioBadge.qml
        CategoryBadge.qml
        LayoutCard.qml
        PopupFrame.qml
        ZonePreview.qml
        ZoneShaderRenderer.qml
        ParameterDelegate.qml
        ShaderParameterSection.qml
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
