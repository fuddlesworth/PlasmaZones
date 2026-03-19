# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later

set(plasmazones_shader_editor_SRCS
    main.cpp
    shadereditorwindow.cpp
    shadereditorwindow.h
    shaderpackageio.cpp
    shaderpackageio.h
    previewcontroller.cpp
    previewcontroller.h
    metadataeditorwidget.cpp
    metadataeditorwidget.h
    addparameterdialog.cpp
    addparameterdialog.h
    newpackagedialog.cpp
    newpackagedialog.h
    parameterpanel.cpp
    parameterpanel.h
    outputpanel.cpp
    outputpanel.h
    glslcompletionmodel.cpp
    glslcompletionmodel.h
    presetpanel.cpp
    presetpanel.h
    # CAVA integration for real audio spectrum in preview
    ../daemon/cavaservice.cpp
    ../daemon/cavaservice.h
)

# QML resources for live preview pane
qt_add_resources(plasmazones_shader_editor_SRCS qml/resources.qrc)

add_executable(plasmazones-shader-editor ${plasmazones_shader_editor_SRCS})

target_link_libraries(plasmazones-shader-editor
    PRIVATE
        plasmazones_core
        plasmazones_rendering
        Qt6::Widgets
        Qt6::Quick
        Qt6::QuickWidgets
        Qt6::QuickControls2
        KF6::TextEditor
        KF6::I18n
        KF6::CoreAddons
        KF6::ConfigWidgets
        KF6::WindowSystem
)

install(TARGETS plasmazones-shader-editor DESTINATION ${KDE_INSTALL_BINDIR})

# Precompiled headers for faster compilation
if(ENABLE_PCH)
    target_precompile_headers(plasmazones-shader-editor
        PRIVATE
            <QObject>
            <QString>
            <QList>
            <QVariant>
            <QHash>
            <QDebug>
            <QMainWindow>
    )
endif()

# Desktop file
install(FILES
    org.plasmazones.shader-editor.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)
